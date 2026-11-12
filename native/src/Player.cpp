// Personal Gallery - a Skyrim Special Edition mod (SKSE plugin PersonalGalleryNative.dll)
// Copyright (C) 2026 the Personal Gallery author
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program (see
// LICENSE.txt). If not, see <https://www.gnu.org/licenses/>.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Player.h"
#include "Place.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <thread>

// CommonLib's PCH targets an older Windows; XAudio2 2.9 headers need Windows 8+ (the game needs 10 anyway).
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <x3daudio.h>
#include <xaudio2.h>

#include "Apply.h"
#include "Catalog.h"
#include "Filters.h"
#include "Pictures.h"
#include "Video.h"

namespace Player
{
	namespace
	{
		constexpr int    kSlots = 16;  // tools/build_gallery.py VIDEO_SLOTS
		std::atomic<double> frameStep{ 1.0 / 30 };  // shown at up to this rate (MCM: 15, 24 or 30 fps)
		std::atomic<int>    widthLimit{ 1440 };      // no video wider than this (MCM quality limit)
		constexpr float  kUnitsPerMetre = 70.0f;
		constexpr float  kRange = 9000.0f;       // play only within this distance (about 130 m)
		using Clock = std::chrono::steady_clock;

		struct Decoded
		{
			double                    time;  // seconds since playback started (keeps growing across loops)
			std::vector<std::uint8_t> rgba;
		};

		// Decoder thread and its output; shared with the thread so stopping never waits for it.
		struct Feed
		{
			std::atomic<bool>   run{ true };
			std::atomic<bool>   audioReady{ false };
			std::atomic<bool>   audioDone{ false };  // the soundtrack has been read (its length sets the loop)
			std::atomic<bool>   failed{ false };
			std::atomic<bool>   ended{ false };  // "play once" reached the end
			std::atomic<double> clock{ -1.0 };   // where playback is now (seconds), told by the render thread
			std::mutex          m;
			std::deque<Decoded> frames;
			Video::Audio        audio;
			int                 w{ 0 };
			int                 h{ 0 };
		};

		struct Stream
		{
			std::shared_ptr<Feed>               feed;
			int                                 slot{ -1 };
			IXAudio2SourceVoice*                voice{ nullptr };
			int                                 channels{ 0 };
			int                                 rate{ 0 };
			double                              silentClock{ 0 };  // used when the video has no sound (or not yet)
			double                              soundStart{ 0 };   // where the picture was when its sound came in
			float                               gain{ 1.0f };      // this painting's volume (Video tab)
			bool                                once{ false };
			Clock::time_point                   lastTick{ Clock::now() };
			ID3D11Texture2D*                    tex{ nullptr };
			ID3D11ShaderResourceView*           srv{ nullptr };
			RE::NiPointer<RE::NiSourceTexture>  slotTex;
			REX::W32::ID3D11Resource*           origTex{ nullptr };
			REX::W32::ID3D11ShaderResourceView* origSrv{ nullptr };
			bool                                swapped{ false };
			bool                                muted{ false };  // silenced while placing
		};

		struct Entry
		{
			RE::ObjectRefHandle     ref;
			int                     pic{ -1 };
			int                     filter{ 0 };
			int                     tone{ 0 };
			int                     mode{ kWakes };
			int                     forced{ -1 };  // spell: 1 awake, 0 asleep, -1 follow the mode
			Clock::time_point       awakeUntil{};
			std::unique_ptr<Stream> stream;
			bool                    restart{ false };  // asked from a script thread: the next frame starts it again
			bool                    drop{ false };     // asked from a script thread: the next frame lets it go
			bool                    broken{ false };   // its video can't be played: never takes a slot again
		};

		std::mutex                    lock;
		std::map<RE::FormID, Entry>   entries;

		// per-painting video options (Video tab), kept apart from entries: they can arrive first
		struct Options
		{
			float gain{ 1.0f };
			bool  once{ false };
			int   maxWidth{ 960 };
			int   extra{ 0 };  // tint and vignette (Catalog::PictureExtra)
			int   fps{ 0 };    // its own frame rate: 0 as the mod menu says, 1..4 = 15, 24, 30, 60
		};
		std::map<RE::FormID, Options> options;
		bool                          slotUsed[kSlots]{};
		std::set<RE::FormID>          muted, held;  // while placing (M / P)
		float                         volume{ 0.8f };
		int                           maxPlaying{ 2 };

		IXAudio2*               xaudio{ nullptr };
		IXAudio2MasteringVoice* master{ nullptr };
		X3DAUDIO_HANDLE         x3d{};
		UINT32                  outChannels{ 2 };

		std::string SlotPath(int slot) { return "textures\\PersonalGallery\\VideoSlots\\slot" + std::to_string(slot) + ".dds"; }

		// Crop to the canvas shape, scale down (nearest sample via lookup tables), apply the tone.
		// BGRA in, RGBA out. A few milliseconds for a 1080p source.
		// a_forceW / a_forceH: make it exactly this size (the plain frame mixed into a filtered one must match it).
		std::vector<std::uint8_t> Prepare(const Video::Frame& f, float aspect, int tone, int extra, int maxWidth, int& outW, int& outH,
			int a_forceW = 0, int a_forceH = 0)
		{
			int cw = f.w, ch = f.h;
			if (static_cast<float>(cw) / ch > aspect) {
				cw = static_cast<int>(ch * aspect);
			} else {
				ch = static_cast<int>(cw / aspect);
			}
			const int   x0 = (f.w - cw) / 2, y0 = (f.h - ch) / 2;
			const float k = std::min(1.0f, static_cast<float>(maxWidth) / cw);
			outW = std::max(4, static_cast<int>(cw * k)) & ~3;
			outH = std::max(4, static_cast<int>(ch * k)) & ~3;
			if (a_forceW > 0 && a_forceH > 0) {
				outW = a_forceW;
				outH = a_forceH;
			}
			thread_local std::vector<int> xs, ys;
			xs.resize(outW);
			ys.resize(outH);
			for (int x = 0; x < outW; ++x) {
				xs[x] = (x0 + static_cast<int>((x + 0.5f) * cw / outW)) * 4;
			}
			for (int y = 0; y < outH; ++y) {
				ys[y] = y0 + static_cast<int>((y + 0.5f) * ch / outH);
			}
			std::vector<std::uint8_t> out(static_cast<size_t>(outW) * outH * 4);
			std::uint8_t*             o = out.data();
			for (int y = 0; y < outH; ++y) {
				const std::uint8_t* row = &f.bgra[static_cast<size_t>(ys[y]) * f.w * 4];
				for (int x = 0; x < outW; ++x, o += 4) {
					const std::uint8_t* s = row + xs[x];
					o[0] = s[2];
					o[1] = s[1];
					o[2] = s[0];
					o[3] = 255;
				}
			}
			// (a video plays as it is: recolouring every frame as it played cost more than anything else here, and
			// a clip is better graded once in a video editor)
			(void)tone;
			(void)extra;
			return out;
		}
		// step: seconds between the frames shown (0: as the mod menu says, read as it plays)
		void Decode(std::shared_ptr<Feed>& feed, const std::wstring& path, float aspect, int tone, int extra, int maxWidth, bool once, double step);

		void DecodeLoop(std::shared_ptr<Feed> feed, std::wstring path, float aspect, int tone, int extra, int maxWidth, bool once, double step)
		{
			Video::Startup();
			struct Done
			{
				~Done() { Video::Shutdown(); }
			} done;
			try {
				Decode(feed, path, aspect, tone, extra, maxWidth, once, step);
			} catch (...) {
				feed->failed = true;  // a bad file never takes the game down
			}
		}

		void Decode(std::shared_ptr<Feed>& feed, const std::wstring& path, float aspect, int tone, int extra, int maxWidth, bool once, double step)
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);  // keep up with playback
			// the soundtrack is read alongside: the picture starts at once (reading it first left the canvas black
			// for seconds after every filter or tone change)
			std::thread([feed, sound = path]() {
				Video::Startup();
				try {
					feed->audio = Video::ReadAudio(sound, &feed->run);
				} catch (...) {
				}
				feed->audioDone = true;
				feed->audioReady = true;
				Video::Shutdown();
			}).detach();
			auto dec = Video::Open(path, aspect, maxWidth);
			if (!dec) {
				feed->failed = true;
				return;
			}
			// the picture loops on the soundtrack's own length, so the two can never drift apart however
			// often it loops (a sound a touch longer just holds the last frame that moment longer)
			double length = std::max(0.1, dec->Duration());
			auto   soundLength = [&]() {  // once the soundtrack is in, the loop follows its length
				if (feed->audioDone) {
					if (const auto& a = feed->audio; a.channels > 0 && a.rate > 0 && !a.samples.empty()) {
						length = std::max(0.1, static_cast<double>(a.samples.size()) / a.channels / a.rate);
					}
				}
			};
			int    loop = 0;
			double lastQueued = -1;
			int    framesThisLoop = 0;
			Video::Frame f;
			while (feed->run) {
				bool full;
				{
					std::lock_guard l(feed->m);
					full = feed->frames.size() >= 4;
				}
				if (full) {
					std::this_thread::sleep_for(std::chrono::milliseconds(4));
					continue;
				}
				// 60 fps sources: every frame is decoded, but only those that will be shown are copied out
				double skipBefore = lastQueued < 0 ? -1.0 : lastQueued + (step > 0 ? step : frameStep.load()) * 0.9 - loop * length;
				// fallen behind (a heavy video, a busy moment): frames already too late to show are passed over, so the
				// picture catches the sound up instead of trailing further and further behind it
				if (const double now = feed->clock.load(); now >= 0 && lastQueued >= 0) {
					skipBefore = std::max(skipBefore, now - 0.03 - loop * length);
				}
				if (!dec->Next(f, skipBefore)) {
					if (once) {
						feed->ended = true;
						return;
					}
					if (framesThisLoop == 0) {
						feed->failed = true;  // not one frame from start to end: a broken file - stop, don't spin
						return;
					}
					framesThisLoop = 0;
					soundLength();
					dec->Rewind();
					++loop;
					continue;
				}
				++framesThisLoop;
				if (f.bgra.empty()) {
					continue;
				}
				{
					const double t = loop * length + f.time;
					lastQueued = t;
					int  w = 0, h = 0;
					auto rgba = Prepare(f, aspect, tone, extra, maxWidth, w, h);
					std::lock_guard l(feed->m);
					feed->w = w;
					feed->h = h;
					feed->frames.push_back({ t, std::move(rgba) });
				}
			}
		}

		RE::BSGeometry* Canvas(RE::TESObjectREFR* ref)
		{
			RE::BSGeometry* found = nullptr;
			if (auto* root = ref->Get3D()) {
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
					if (_stricmp(g->name.c_str(), "Canvas") == 0) {
						found = g;
						return RE::BSVisit::BSVisitControl::kStop;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			return found;
		}

		RE::NiSourceTexture* CanvasDiffuse(RE::TESObjectREFR* ref)
		{
			auto* geo = Canvas(ref);
			auto* lighting = geo ? netimmerse_cast<RE::BSLightingShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get()) : nullptr;
			auto* mat = lighting ? static_cast<RE::BSLightingShaderMaterialBase*>(lighting->material) : nullptr;
			return mat ? mat->diffuseTexture.get() : nullptr;
		}

		void Unswap(Stream& s)
		{
			if (s.swapped && s.slotTex && s.slotTex->rendererTexture) {
				s.slotTex->rendererTexture->texture = s.origTex;
				s.slotTex->rendererTexture->resourceView = s.origSrv;
				s.tex->Release();  // the references we lent the engine
				s.srv->Release();
			}
			s.swapped = false;
			s.slotTex.reset();
		}

		void StopStream(Entry& e)
		{
			if (!e.stream) {
				return;
			}
			Stream& s = *e.stream;
			s.feed->run = false;
			Unswap(s);
			if (s.voice) {
				s.voice->DestroyVoice();
			}
			if (s.srv) {
				s.srv->Release();
			}
			if (s.tex) {
				s.tex->Release();
			}
			if (s.slot >= 0) {
				slotUsed[s.slot] = false;
			}
			e.stream.reset();
			SKSE::GetTaskInterface()->AddTask([h = e.ref, pic = e.pic, filter = e.filter, tone = e.tone]() {
				if (auto ref = h.get()) {
					Apply::Set(ref.get(), pic, filter, tone);  // back to the still
				}
			});
		}

		bool StartStream(Entry& e)
		{
			int slot = -1;
			for (int i = 0; i < kSlots; ++i) {
				if (!slotUsed[i]) {
					slot = i;
					break;
				}
			}
			if (slot < 0) {
				return false;
			}
			slotUsed[slot] = true;
			auto s = std::make_unique<Stream>();
			s->slot = slot;
			s->feed = std::make_shared<Feed>();
			// (a video plays as it is: painted filters are for still pictures - its colour mood and fine-tuning are
			// put on each frame as it plays)
			const std::wstring video = Pictures::Path(e.pic);
			Options opt;
			if (auto it = options.find(e.ref.get() ? e.ref.get()->GetFormID() : 0); it != options.end()) {
				opt = it->second;
			}
			s->gain = opt.gain;
			s->once = opt.once;
			static constexpr double kOwnRates[]{ 0.0, 15.0, 24.0, 30.0, 60.0 };
			const int               own = std::clamp(opt.fps, 0, 4);
			std::thread(DecodeLoop, s->feed, video, Catalog::kAspects[Pictures::Aspect(e.pic)], e.tone, opt.extra, std::min(opt.maxWidth, widthLimit.load()), opt.once,
				own > 0 ? 1.0 / kOwnRates[own] : 0.0).detach();
			e.stream = std::move(s);
			return true;
		}

		void StartAudio(Stream& s)
		{
			auto& a = s.feed->audio;
			if (!xaudio || a.samples.empty() || a.channels <= 0) {
				return;
			}
			WAVEFORMATEX fmt{};
			fmt.wFormatTag = WAVE_FORMAT_PCM;
			fmt.nChannels = static_cast<WORD>(a.channels);
			fmt.nSamplesPerSec = a.rate;
			fmt.wBitsPerSample = 16;
			fmt.nBlockAlign = static_cast<WORD>(a.channels * 2);
			fmt.nAvgBytesPerSec = a.rate * fmt.nBlockAlign;
			if (a.rate <= 0) {
				return;
			}
			// the picture has been playing while the sound was read: the sound joins in where the picture is now
			// (starting it from the top made the picture stand still until the sound caught up)
			const double total = static_cast<double>(a.samples.size() / a.channels) / a.rate;
			double       begin = s.silentClock;
			if (s.once && begin >= total - 0.05) {
				return;  // played once and already past its sound: it stays on the silent clock
			}
			if (!s.once && total > 0) {
				begin = std::fmod(begin, total);
			}
			if (FAILED(xaudio->CreateSourceVoice(&s.voice, &fmt))) {
				s.voice = nullptr;
				return;
			}
			s.soundStart = s.silentClock;
			XAUDIO2_BUFFER buf{};
			buf.AudioBytes = static_cast<UINT32>(a.samples.size() * sizeof(std::int16_t));
			buf.pAudioData = reinterpret_cast<const BYTE*>(a.samples.data());
			buf.PlayBegin = static_cast<UINT32>(begin * a.rate);
			buf.LoopCount = s.once ? 0 : XAUDIO2_LOOP_INFINITE;
			s.voice->SubmitSourceBuffer(&buf);
			s.voice->SetVolume(volume * s.gain);
			s.voice->Start();
			s.channels = a.channels;
			s.rate = a.rate;
		}

		// Skyrim is right-handed Z-up; X3DAudio is left-handed Y-up: swap Y and Z.
		X3DAUDIO_VECTOR ToX3D(const RE::NiPoint3& p, float scale = 1.0f) { return { p.x * scale, p.z * scale, p.y * scale }; }

		void Spatialise(Stream& s, const RE::NiPoint3& pos, const RE::NiTransform& cam)
		{
			if (!s.voice) {
				return;
			}
			X3DAUDIO_LISTENER listener{};
			listener.Position = ToX3D(cam.translate, 1 / kUnitsPerMetre);
			listener.OrientFront = ToX3D({ cam.rotate.entry[0][1], cam.rotate.entry[1][1], cam.rotate.entry[2][1] });
			listener.OrientTop = ToX3D({ cam.rotate.entry[0][2], cam.rotate.entry[1][2], cam.rotate.entry[2][2] });
			X3DAUDIO_EMITTER emitter{};
			emitter.Position = ToX3D(pos, 1 / kUnitsPerMetre);
			emitter.OrientFront = { 0, 0, 1 };
			emitter.OrientTop = { 0, 1, 0 };
			emitter.ChannelCount = s.channels;
			emitter.ChannelRadius = 0.6f;
			float azimuths[8]{ -0.6f, 0.6f };  // stereo pair either side of the painting
			emitter.pChannelAzimuths = azimuths;
			emitter.CurveDistanceScaler = 3.0f;  // full volume within about 3 m
			float                 matrix[2 * 8]{};
			X3DAUDIO_DSP_SETTINGS dsp{};
			dsp.SrcChannelCount = s.channels;
			dsp.DstChannelCount = outChannels;
			dsp.pMatrixCoefficients = matrix;
			X3DAudioCalculate(x3d, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);
			s.voice->SetOutputMatrix(master, s.channels, outChannels, matrix);
			s.voice->SetVolume(s.muted ? 0.0f : volume * s.gain);
		}

		double PlayClock(Stream& s, bool paused)
		{
			const auto now = Clock::now();
			const double dt = std::chrono::duration<double>(now - s.lastTick).count();
			s.lastTick = now;
			if (s.voice) {
				XAUDIO2_VOICE_STATE st{};
				s.voice->GetState(&st, 0);
				return s.soundStart + static_cast<double>(st.SamplesPlayed) / s.rate;
			}
			if (!paused) {
				s.silentClock += dt;
			}
			return s.silentClock;
		}

		void Upload(Stream& s, ID3D11Device* device, ID3D11DeviceContext* ctx, double clock)
		{
			s.feed->clock = clock;
			Decoded show;
			bool    have = false;
			{
				std::lock_guard l(s.feed->m);
				while (!s.feed->frames.empty() && s.feed->frames.front().time <= clock) {
					show = std::move(s.feed->frames.front());
					s.feed->frames.pop_front();
					have = true;
				}
				if (!have) {
					return;
				}
				if (!s.tex) {
					D3D11_TEXTURE2D_DESC d{};
					d.Width = s.feed->w;
					d.Height = s.feed->h;
					d.MipLevels = 1;
					d.ArraySize = 1;
					d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					d.SampleDesc.Count = 1;
					d.Usage = D3D11_USAGE_DYNAMIC;
					d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
					if (FAILED(device->CreateTexture2D(&d, nullptr, &s.tex)) || FAILED(device->CreateShaderResourceView(s.tex, nullptr, &s.srv))) {
						return;
					}
				}
			}
			D3D11_TEXTURE2D_DESC d{};
			s.tex->GetDesc(&d);
			D3D11_MAPPED_SUBRESOURCE m{};
			if (show.rgba.size() != static_cast<size_t>(d.Width) * d.Height * 4 || FAILED(ctx->Map(s.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
				return;
			}
			for (UINT y = 0; y < d.Height; ++y) {
				std::memcpy(static_cast<std::uint8_t*>(m.pData) + y * m.RowPitch, &show.rgba[static_cast<size_t>(y) * d.Width * 4], d.Width * 4);
			}
			ctx->Unmap(s.tex, 0);
		}

		// Make sure the canvas shows this stream's slot and the slot shows our texture.
		void Bind(Stream& s, RE::TESObjectREFR* ref)
		{
			if (!s.tex) {
				return;
			}
			auto* current = CanvasDiffuse(ref);
			if (!s.swapped || current != s.slotTex.get()) {
				Unswap(s);
				Apply::SetNow(ref, SlotPath(s.slot));
				current = CanvasDiffuse(ref);
				if (!current || !current->rendererTexture) {
					return;  // slot texture still loading; try next frame
				}
				s.slotTex.reset(current);
				s.origTex = current->rendererTexture->texture;
				s.origSrv = current->rendererTexture->resourceView;
				s.tex->AddRef();  // the engine now holds these
				s.srv->AddRef();
				current->rendererTexture->texture = reinterpret_cast<REX::W32::ID3D11Resource*>(s.tex);
				current->rendererTexture->resourceView = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(s.srv);
				s.swapped = true;
			}
		}

		// May this painting hold a stream at all (playing or paused on its current frame)?
		bool Candidate(const Entry& e, float dist)
		{
			return dist <= kRange && e.forced != 0 && (e.forced == 1 || e.mode != kStill);
		}

		// Is it moving right now? "Wakes when seen" plays only while you look at it and pauses where it
		// was when you look away.
		bool Awake(Entry& e, bool looking, Clock::time_point now)
		{
			if (e.forced == 1 || e.mode == kAlways) {
				return true;
			}
			if (looking) {
				e.awakeUntil = now + std::chrono::milliseconds(600);  // ignore a quick glance away
			}
			return now < e.awakeUntil;
		}
	}

	void Init()
	{
		Video::Startup();
		if (SUCCEEDED(XAudio2Create(&xaudio, 0)) && SUCCEEDED(xaudio->CreateMasteringVoice(&master))) {
			DWORD mask = 0;
			master->GetChannelMask(&mask);
			XAUDIO2_VOICE_DETAILS d{};
			master->GetVoiceDetails(&d);
			outChannels = std::min<UINT32>(d.InputChannels, 8);  // the mix matrix below holds 2 x 8
			X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, x3d);
		} else {
			xaudio = nullptr;
		}
	}

	void SetLive(RE::TESObjectREFR* a_ref, int a_pic, int a_filter, int a_tone, int a_mode)
	{
		if (!a_ref || !Pictures::IsVideo(a_pic)) {
			Stop(a_ref);
			return;
		}
		std::lock_guard l(lock);
		auto& e = entries[a_ref->GetFormID()];
		if (e.pic != a_pic) {
			e.broken = false;  // another picture: worth a try
		}
		if (e.stream && (e.pic != a_pic || e.tone != a_tone || e.filter != a_filter)) {
			e.restart = true;  // the next frame starts it again with these
		}
		e.drop = false;
		e.ref = a_ref->CreateRefHandle();
		e.pic = a_pic;
		e.filter = a_filter;
		e.tone = a_tone;
		e.mode = a_mode;
	}

	void StopAll()
	{
		std::lock_guard l(lock);
		for (auto& [id, e] : entries) {
			if (e.stream) {
				StopStream(e);
			}
		}
	}

	void Stop(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		if (auto it = entries.find(a_ref->GetFormID()); it != entries.end()) {
			it->second.drop = true;  // the next frame lets it go (with the renderer, not from a script thread)
		}
	}

	int ToggleAwake(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		auto it = a_ref ? entries.find(a_ref->GetFormID()) : entries.end();
		if (it == entries.end()) {
			return -1;  // not a video painting
		}
		auto& e = it->second;
		const bool awake = e.stream != nullptr || e.forced == 1;
		e.forced = awake ? 0 : 1;
		return e.forced;
	}

	void SetSettings(float a_volume, int a_maxPlaying, int a_quality, int a_fps)
	{
		static constexpr double kRates[]{ 15.0, 24.0, 30.0, 60.0 };
		frameStep = 1.0 / kRates[std::clamp(a_fps, 0, 3)];
		widthLimit = Catalog::kQualityWidths[std::clamp(a_quality, 0, static_cast<int>(Catalog::kQualityWidths.size()) - 1)];
		std::lock_guard l(lock);
		volume = std::clamp(a_volume, 0.0f, 1.0f);
		maxPlaying = std::clamp(a_maxPlaying, 1, kSlots);
	}

	void SetOptions(RE::TESObjectREFR* a_ref, float a_gain, bool a_once, int a_maxWidth, int a_extra, int a_fps)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		auto& o = options[a_ref->GetFormID()];
		const bool restart = o.once != a_once || o.maxWidth != a_maxWidth || o.extra != a_extra || o.fps != a_fps;
		o = { a_gain, a_once, a_maxWidth, a_extra, a_fps };
		if (auto it = entries.find(a_ref->GetFormID()); it != entries.end() && it->second.stream) {
			it->second.stream->gain = a_gain;  // volume changes at once
			if (restart) {
				it->second.restart = true;  // quality, play mode, tint: the next frame starts it again with them
			}
		}
	}

	int ToggleMute(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		if (!a_ref || !entries.count(a_ref->GetFormID())) {
			return -1;
		}
		const auto id = a_ref->GetFormID();
		if (!muted.erase(id)) {  // (the set holds the ones switched away from the usual: silent while placed)
			muted.insert(id);
		}
		return (a_ref == Place::Current()) != (muted.count(id) > 0) ? 1 : 0;
	}

	int TogglePause(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		if (!a_ref || !entries.count(a_ref->GetFormID())) {
			return -1;
		}
		const auto id = a_ref->GetFormID();
		return held.erase(id) ? 0 : (held.insert(id), 1);
	}

	bool IsMuted(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		return a_ref && (a_ref == Place::Current()) != (muted.count(a_ref->GetFormID()) > 0);
	}

	bool IsHeld(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		return a_ref && held.count(a_ref->GetFormID());
	}

	void ClearHolds()
	{
		std::lock_guard l(lock);
		muted.clear();
		held.clear();
	}

	bool IsPlaying(RE::FormID a_ref)
	{
		std::lock_guard l(lock);
		auto it = entries.find(a_ref);
		return it != entries.end() && it->second.stream;
	}

	void Frame(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		if (!camera || !camera->cameraRoot) {
			return;
		}
		const bool          paused = ui && ui->GameIsPaused();
		const RE::NiTransform cam = camera->cameraRoot->world;
		RE::NiPoint3          fwd{ cam.rotate.entry[0][1], cam.rotate.entry[1][1], cam.rotate.entry[2][1] };
		const auto            now = Clock::now();

		std::lock_guard l(lock);
		// which paintings should be alive, nearest first
		std::vector<std::pair<float, RE::FormID>> want;  // awake ones sort first (key - 1e6)
		std::set<RE::FormID>                      awake;
		for (auto it = entries.begin(); it != entries.end();) {
			auto& e = it->second;
			auto  ref = e.ref.get();
			if (!ref || e.drop) {
				StopStream(e);
				it = entries.erase(it);
				continue;
			}
			if (e.restart) {
				e.restart = false;
				StopStream(e);
			}
			if (!ref->Get3D() || ref->IsDisabled()) {
				StopStream(e);
				++it;
				continue;
			}
			const RE::NiPoint3 pos = ref->Get3D()->world.translate;  // where it is drawn (the gallery's preview moves it)
			RE::NiPoint3       to = pos - cam.translate;
			const float        dist = to.Length();
			to.Unitize();
			// looking at it: the crosshair is on the painting (or within a few degrees of its edge)
			const float radius = std::max(10.0f, ref->Get3D()->worldBound.radius);
			const float cone = std::atan2(radius * 0.8f, std::max(dist, 1.0f)) + 0.06f;
			// the painting being edited is always looked at: stopping and starting it made the preview flicker
			const bool  looking = ref.get() == Place::Current() || (dist < kRange && fwd.Dot(to) > std::cos(std::min(cone, 1.2f)));
			if (!e.broken && Candidate(e, dist)) {
				const bool on = Awake(e, looking, now);
				if (on) {
					awake.insert(it->first);
				}
				want.emplace_back(on ? dist - 1.0e6f : dist, it->first);
			}
			++it;
		}
		std::sort(want.begin(), want.end());
		std::map<RE::FormID, bool> play;
		for (size_t i = 0; i < want.size() && static_cast<int>(i) < maxPlaying; ++i) {
			play[want[i].second] = true;
		}

		for (auto& [id, e] : entries) {
			if (!play.count(id)) {
				StopStream(e);
				continue;
			}
			if (!e.stream && !StartStream(e)) {
				continue;
			}
			auto  ref = e.ref.get();
			auto& s = *e.stream;
			if (s.feed->failed) {
				e.broken = true;  // can't be played: give its slot to another video
				StopStream(e);
				continue;
			}
			if (s.feed->ended) {  // "play once" finished: hold the last frame until you look away
				bool empty;
				{
					std::lock_guard fl(s.feed->m);
					empty = s.feed->frames.empty();
				}
				if (empty) {
					if (!awake.count(id)) {
						StopStream(e);  // back to the still; the next look plays it again
					} else {
						Bind(s, ref.get());
					}
					continue;
				}
			}
			if (!s.voice && s.feed->audioReady) {
				StartAudio(s);
				s.feed->audioReady = false;  // only once
			}
			const bool frozen = paused || !awake.count(id) || held.count(id);
			// silent while you pick and place it (M turns it on); it sounds once it hangs
			s.muted = (e.ref.get().get() == Place::Current()) != (muted.count(id) > 0);
			if (s.voice) {
				if (frozen) {
					s.voice->Stop();
				} else {
					s.voice->Start();
				}
				Spatialise(s, ref->GetPosition(), cam);
			}
			Upload(s, a_device, a_context, PlayClock(s, frozen));
			Bind(s, ref.get());
		}
	}
}
