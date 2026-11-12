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

#include "Video.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <thread>

#include "Filters.h"
#include "Dev.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Windows 8+ attribute; hidden when the plugin's PCH targets an older _WIN32_WINNT.
static const GUID kAdvancedVideoProcessing = { 0x0f81da2c, 0xb537, 0x4672, { 0xa8, 0xb2, 0xa6, 0x81, 0xb1, 0x73, 0x07, 0xa3 } };
#define MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING kAdvancedVideoProcessing

namespace Video
{
	namespace
	{
		std::wstring Ext(const std::wstring& path)
		{
			std::wstring e = std::filesystem::path(path).extension().wstring();
			std::transform(e.begin(), e.end(), e.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return e;
		}

		// ---- Media Foundation -------------------------------------------------------------

		class MFDecoder : public Decoder
		{
		public:
			bool Init(const std::wstring& path, float aspect, int maxWidth)
			{
				ComPtr<IMFAttributes> attr;
				MFCreateAttributes(&attr, 2);
				attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);  // YUV -> RGB in MF
				if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attr.Get(), &reader))) {
					return false;
				}
				reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
				reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
				ComPtr<IMFMediaType> type;
				MFCreateMediaType(&type);
				type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
				type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
				if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get()))) {
					return false;
				}
				ComPtr<IMFMediaType> current;
				reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
				UINT32 w = 0, h = 0;
				MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
				width = static_cast<int>(w);
				height = static_cast<int>(h);
				stride = static_cast<INT32>(MFGetAttributeUINT32(current.Get(), MF_MT_DEFAULT_STRIDE, w * 4));
				{
					UINT32 num = 0, den = 0;
					if (SUCCEEDED(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &num, &den)) && den > 0) {
						frameRate = static_cast<double>(num) / den;
					}
				}
				if (aspect > 0 && maxWidth > 0 && w > 0 && h > 0) {
					// the part of the frame the canvas shows is cw wide: scale so that part is maxWidth wide
					const float cw = std::min(static_cast<float>(w), h * aspect);
					const float k = static_cast<float>(maxWidth) / cw;
					if (k < 0.9f) {  // worth it: ask Media Foundation for smaller frames (it scales as it converts)
						const UINT32 sw = (static_cast<UINT32>(w * k) + 1) & ~1u, sh = (static_cast<UINT32>(h * k) + 1) & ~1u;
						ComPtr<IMFMediaType> scaled;
						MFCreateMediaType(&scaled);
						scaled->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
						scaled->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
						MFSetAttributeSize(scaled.Get(), MF_MT_FRAME_SIZE, sw, sh);
						if (sw >= 16 && sh >= 16 &&
							SUCCEEDED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, scaled.Get()))) {
							ComPtr<IMFMediaType> now;
							if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &now))) {
								UINT32 nw = 0, nh = 0;
								MFGetAttributeSize(now.Get(), MF_MT_FRAME_SIZE, &nw, &nh);
								width = static_cast<int>(nw);
								height = static_cast<int>(nh);
								stride = static_cast<INT32>(MFGetAttributeUINT32(now.Get(), MF_MT_DEFAULT_STRIDE, nw * 4));
							}
						} else {  // this PC's converter won't scale: full-size frames, as before
							reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());
						}
					}
				}

				PROPVARIANT var;
				PropVariantInit(&var);
				if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var))) {
					duration = var.uhVal.QuadPart / 1e7;
				}
				PropVariantClear(&var);
				return width > 0 && height > 0;
			}

			bool Next(Frame& out, double skipBefore) override
			{
				for (;;) {
					DWORD              flags = 0;
					LONGLONG           ts = 0;
					ComPtr<IMFSample>  sample;
					if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &ts, &sample)) ||
						(flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
						return false;
					}
					if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {  // the video changed size partway: read it again
						ComPtr<IMFMediaType> current;
						if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current))) {
							UINT32 w = 0, h = 0;
							MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
							width = static_cast<int>(w);
							height = static_cast<int>(h);
							stride = static_cast<INT32>(MFGetAttributeUINT32(current.Get(), MF_MT_DEFAULT_STRIDE, w * 4));
						}
					}
					if (!sample) {
						continue;  // stream tick / gap
					}
					out.time = ts / 1e7;
					if (out.time < skipBefore) {
						out.bgra.clear();
						return true;
					}
					ComPtr<IMFMediaBuffer> buf;
					if (FAILED(sample->ConvertToContiguousBuffer(&buf))) {
						return false;
					}
					BYTE* data = nullptr;
					DWORD len = 0;
					if (FAILED(buf->Lock(&data, nullptr, &len)) || !data) {
						return false;
					}
					const int absRow = std::abs(stride);
					if (width <= 0 || height <= 0 || absRow < width * 4 || static_cast<size_t>(len) < static_cast<size_t>(absRow) * height) {
						buf->Unlock();  // a frame smaller than its stated size: skip it rather than read past it
						continue;
					}
					out.w = width;
					out.h = height;
					out.bgra.resize(static_cast<size_t>(width) * height * 4);
					const int absStride = std::abs(stride);
					for (int y = 0; y < height; ++y) {
						// negative stride = bottom-up rows
						const BYTE* row = stride >= 0 ? data + static_cast<size_t>(y) * absStride : data + static_cast<size_t>(height - 1 - y) * absStride;
						std::uint8_t* dst = &out.bgra[static_cast<size_t>(y) * width * 4];
						std::memcpy(dst, row, static_cast<size_t>(width) * 4);  // alpha is undefined in RGB32; nothing reads it
					}
					buf->Unlock();
					return true;
				}
			}

			void Rewind() override { Seek(0.0); }

			void Seek(double a_seconds) override
			{
				PROPVARIANT var;
				InitPropVariantFromInt64(static_cast<LONGLONG>(std::max(0.0, a_seconds) * 1e7), &var);
				reader->SetCurrentPosition(GUID_NULL, var);
				PropVariantClear(&var);
			}

			double FrameRate() const override { return frameRate; }
			double Duration() const override { return duration; }
			int    Width() const override { return width; }
			int    Height() const override { return height; }

		private:
			ComPtr<IMFSourceReader> reader;
			int                     width{ 0 };
			int                     height{ 0 };
			INT32                   stride{ 0 };
			double                  duration{ 0 };
			double                  frameRate{ 0 };
		};

		// ---- animated GIF (WIC) -----------------------------------------------------------

		class GifDecoder : public Decoder
		{
		public:
			bool Init(const std::wstring& path)
			{
				ComPtr<IWICImagingFactory> factory;
				if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
					return false;
				}
				ComPtr<IWICBitmapDecoder> dec;
				if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) {
					return false;
				}
				UINT count = 0;
				dec->GetFrameCount(&count);
				// logical screen size
				ComPtr<IWICMetadataQueryReader> meta;
				PROPVARIANT                     v;
				PropVariantInit(&v);
				if (SUCCEEDED(dec->GetMetadataQueryReader(&meta))) {
					if (SUCCEEDED(meta->GetMetadataByName(L"/logscrdesc/Width", &v))) width = v.uiVal;
					PropVariantClear(&v);
					if (SUCCEEDED(meta->GetMetadataByName(L"/logscrdesc/Height", &v))) height = v.uiVal;
					PropVariantClear(&v);
				}
				std::vector<std::uint8_t> canvas;
				double                    t = 0;
				for (UINT i = 0; i < count; ++i) {
					ComPtr<IWICBitmapFrameDecode> f;
					if (FAILED(dec->GetFrame(i, &f))) {
						break;
					}
					UINT fw = 0, fh = 0;
					f->GetSize(&fw, &fh);
					if (width == 0) {
						width = static_cast<int>(fw);
						height = static_cast<int>(fh);
					}
					if (canvas.empty()) {
						canvas.assign(static_cast<size_t>(width) * height * 4, 0);
					}
					int  left = 0, top = 0;
					UINT delay = 10;  // 1/100 s
					UINT disposal = 0;  // what becomes of this frame's patch afterwards: 2 cleared, 3 put back as it was
					ComPtr<IWICMetadataQueryReader> fm;
					if (SUCCEEDED(f->GetMetadataQueryReader(&fm))) {
						if (SUCCEEDED(fm->GetMetadataByName(L"/imgdesc/Left", &v))) left = v.uiVal;
						PropVariantClear(&v);
						if (SUCCEEDED(fm->GetMetadataByName(L"/imgdesc/Top", &v))) top = v.uiVal;
						PropVariantClear(&v);
						// (a delay of 0 or 1 means "as fast as you can" in the file, and every viewer plays those at 10)
						if (SUCCEEDED(fm->GetMetadataByName(L"/grctlext/Delay", &v))) delay = v.uiVal < 2 ? 10 : v.uiVal;
						PropVariantClear(&v);
						if (SUCCEEDED(fm->GetMetadataByName(L"/grctlext/Disposal", &v))) disposal = v.bVal;
						PropVariantClear(&v);
					}
					ComPtr<IWICFormatConverter> conv;
					factory->CreateFormatConverter(&conv);
					if (FAILED(conv->Initialize(f.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
						break;
					}
					std::vector<std::uint8_t> px(static_cast<size_t>(fw) * fh * 4);
					conv->CopyPixels(nullptr, fw * 4, static_cast<UINT>(px.size()), px.data());
					std::vector<std::uint8_t> before;
					if (disposal == 3) {
						before = canvas;
					}
					// composite onto the running canvas (transparent pixels keep the previous frame)
					for (UINT y = 0; y < fh && top + static_cast<int>(y) < height; ++y) {
						for (UINT x = 0; x < fw && left + static_cast<int>(x) < width; ++x) {
							const std::uint8_t* s = &px[(static_cast<size_t>(y) * fw + x) * 4];
							if (s[3] > 0) {
								std::memcpy(&canvas[(static_cast<size_t>(top + y) * width + left + x) * 4], s, 4);
							}
						}
					}
					Frame fr;
					fr.time = t;
					fr.w = width;
					fr.h = height;
					fr.bgra = canvas;
					for (size_t p = 3; p < fr.bgra.size(); p += 4) {
						fr.bgra[p] = 255;
					}
					frames.push_back(std::move(fr));
					t += delay / 100.0;
					if (disposal == 3 && before.size() == canvas.size()) {
						canvas = std::move(before);
					} else if (disposal == 2) {  // its patch is wiped before the next frame (GIFs that redraw moving parts)
						for (UINT y = 0; y < fh && top + static_cast<int>(y) < height; ++y) {
							for (UINT x = 0; x < fw && left + static_cast<int>(x) < width; ++x) {
								std::memset(&canvas[(static_cast<size_t>(top + y) * width + left + x) * 4], 0, 4);
							}
						}
					}
					if (frames.size() * canvas.size() > (512ull << 20)) {
						break;  // a huge GIF: play its first half-gigabyte of frames rather than run out of memory
					}
				}
				duration = t;
				return !frames.empty();
			}

			bool Next(Frame& out, double) override
			{
				if (pos >= frames.size()) {
					return false;
				}
				out = frames[pos++];
				return true;
			}
			void   Rewind() override { pos = 0; }
			void   Seek(double a_seconds) override
			{
				pos = 0;
				while (pos + 1 < frames.size() && frames[pos].time < a_seconds) {
					++pos;
				}
			}
			double FrameRate() const override { return duration > 0 ? frames.size() / duration : 0.0; }
			double Duration() const override { return duration; }
			int    Width() const override { return width; }
			int    Height() const override { return height; }

		private:
			std::vector<Frame> frames;
			size_t             pos{ 0 };
			int                width{ 0 };
			int                height{ 0 };
			double             duration{ 0 };
		};
	}

	void Startup()
	{
		(void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	}

	void Shutdown()
	{
		MFShutdown();
		CoUninitialize();
	}

	bool IsVideoFile(const std::wstring& a_path)
	{
		const auto e = Ext(a_path);
		return e == L".mp4" || e == L".m4v" || e == L".mov" || e == L".wmv" || e == L".avi" || e == L".mkv" || e == L".webm" || e == L".gif";
	}

	std::unique_ptr<Decoder> Open(const std::wstring& a_path, float a_aspect, int a_maxWidth)
	{
		if (Ext(a_path) == L".gif") {
			auto d = std::make_unique<GifDecoder>();
			return d->Init(a_path) ? std::move(d) : nullptr;
		}
		auto d = std::make_unique<MFDecoder>();
		return d->Init(a_path, a_aspect, a_maxWidth) ? std::move(d) : nullptr;
	}

	Audio ReadAudio(const std::wstring& a_path, const std::atomic<bool>* a_run)
	{
		Audio                   out;
		ComPtr<IMFSourceReader> reader;
		if (Ext(a_path) == L".gif" || FAILED(MFCreateSourceReaderFromURL(a_path.c_str(), nullptr, &reader))) {
			return out;
		}
		reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
		if (FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE))) {
			return out;
		}
		ComPtr<IMFMediaType> type;
		MFCreateMediaType(&type);
		type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
		if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, type.Get()))) {
			return out;
		}
		ComPtr<IMFMediaType> current;
		reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &current);
		const int srcChannels = static_cast<int>(MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0));
		if (srcChannels <= 0) {
			return out;
		}
		out.channels = std::min(srcChannels, 2);
		out.rate = static_cast<int>(MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0));
		auto pcm = [](float v) { return static_cast<std::int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };
		for (;;) {
			if (a_run && !a_run->load()) {
				return {};  // the video was stopped: no use finishing its sound
			}
			DWORD             flags = 0;
			ComPtr<IMFSample> sample;
			if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample)) ||
				(flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
				break;
			}
			if (!sample) {
				continue;
			}
			ComPtr<IMFMediaBuffer> buf;
			if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) {
				Dev::Log("odd", "audio buffer unreadable part-way; keeping the sound so far");
				break;  // keep the sound read so far
			}
			BYTE* data = nullptr;
			DWORD len = 0;
			if (FAILED(buf->Lock(&data, nullptr, &len)) || !data) {
				break;
			}
			const float* f = reinterpret_cast<const float*>(data);
			const size_t frames = len / sizeof(float) / srcChannels;
			for (size_t i = 0; i < frames; ++i, f += srcChannels) {
				if (srcChannels == 1) {
					out.samples.push_back(pcm(f[0]));
				} else if (srcChannels == 2) {
					out.samples.push_back(pcm(f[0]));
					out.samples.push_back(pcm(f[1]));
				} else {  // 5.1 / 7.1: front left/right plus some of the centre
					out.samples.push_back(pcm(f[0] + 0.7f * f[2]));
					out.samples.push_back(pcm(f[1] + 0.7f * f[2]));
				}
			}
			buf->Unlock();
		}
		return out;
	}
}
