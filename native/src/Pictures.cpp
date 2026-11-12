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

#include "Pictures.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include <DirectXTex.h>
#include <objbase.h>

#include "Catalog.h"
#include "Dev.h"
#include "Filters.h"
#include "Video.h"

namespace fs = std::filesystem;

int Catalog::NearestAspect(float ratio)
{
	int   best = 0;
	float bestDiff = 1e9f;
	for (int i = 0; i < static_cast<int>(kAspects.size()); ++i) {
		const float d = std::abs(std::log(ratio / kAspects[i]));
		if (d < bestDiff) {
			bestDiff = d;
			best = i;
		}
	}
	return best;
}

namespace Pictures
{
	namespace
	{
		constexpr int kClearMaxHigh = 4096;  // long side for the clear texture: High detail (MCM) ...
		constexpr int kClearMax = 2048;      // ... and Standard (a quarter of the video memory)
		constexpr int kFilterMax = 1536;  // crisp enough on a large frame; one core, low priority (no stutter)
		constexpr int kThumbMax = 256;
		constexpr int kDraftMax = 512;    // a filtered look's quick draft, shown while the full one is painted
		constexpr int kFilters = static_cast<int>(Catalog::kFilters.size());
		constexpr int kTones = static_cast<int>(Catalog::kTones.size());
		constexpr int kLooks = kFilters * kTones;  // look = filter * kTones + tone

		struct Picture
		{
			std::string  key;
			std::string  name;
			fs::path     path;
			std::string  stamp;  // date + size: tells a rescan the file changed
			std::string  id;     // content fingerprint: survives renames and moves, names the cache files
			int          aspect{ 5 };
			bool         video{ false };
			bool         removed{ false };  // the file is gone; the slot stays so indices never shift
			std::string  texture[kLooks];  // set once the DDS exists
			std::map<std::int64_t, std::string> tuned;   // look + the player's adjustments -> DDS
			std::set<std::int64_t>              queuedTuned;
			std::shared_ptr<const Thumb> thumbs[kLooks];
			bool         queuedTex[kLooks]{};
			bool         queuedThumb[kLooks]{};
		};

		struct Job
		{
			int  pic;
			int  filter;
			bool thumb;
			int  adjust{ 0 };  // the player's colour adjustments (Filters::ToneAdjust)
			int  extra{ 0 };   // tint, vignette, sharpness (Catalog::PictureExtra)
		};

		std::int64_t TunedKey(int look, int adjust, int extra)
		{
			return (static_cast<std::int64_t>(extra) << 33) | (static_cast<std::int64_t>(adjust) << 8) | look;
		}

		std::mutex                lock;
		std::condition_variable   wake;
		std::vector<Picture>      pictures;
		std::deque<Job>           jobs;
		int                       jobsDone{ 0 };
		int                       jobsTotal{ 0 };
		std::function<void()>     onReady;

		fs::path                  cacheDir;
		fs::path                  folder;
		std::mutex                scanLock;  // one scan at a time
		bool                      highDetail{ false };
		std::uint64_t             generation{ 1 };  // bumped whenever the library or favourites change (UI caches)
		std::set<std::string>     favourites;  // fingerprints; Data\PersonalGallery\favourites.txt

		fs::path FavouritesFile() { return folder.parent_path() / "favourites.txt"; }

		void LoadFavourites()
		{
			if (FILE* f = _wfopen(FavouritesFile().c_str(), L"r")) {
				char line[128];
				while (fgets(line, sizeof(line), f)) {
					std::string id(line);
					while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) {
						id.pop_back();
					}
					if (!id.empty() && id[0] != '#') {
						favourites.insert(id);
					}
				}
				fclose(f);
			}
		}

		void SaveFavourites()
		{
			if (FILE* f = _wfopen(FavouritesFile().c_str(), L"w")) {
				fputs("# Personal Gallery favourites (picture fingerprints; they survive renames)\n", f);
				for (const auto& id : favourites) {
					fprintf(f, "%s\n", id.c_str());
				}
				fclose(f);
			}
		}

		std::string CacheName(const Picture& p, int look)
		{
			// clear looks come in two sizes; Standard ones carry "_s" (older caches without it are High)
			const bool standard = look / kTones == 0 && !highDetail;
			return p.id + "_" + std::string(Catalog::kFilterKeys[look / kTones]) + "_" + std::string(Catalog::kToneKeys[look % kTones]) +
			       (standard ? "_s" : "") + ".dds";
		}

		// A path as UTF-8: any language, emoji included (path::string() throws for what the code page can't hold).
		std::string Utf8(const fs::path& p)
		{
			const auto u = p.u8string();
			return std::string(u.begin(), u.end());
		}

		// album (subfolder) and file name of a picture key ("Album\\file.png"), without going through fs::path
		std::string ParentOf(const std::string& key)
		{
			const auto s = key.find_last_of("\\/");
			return s == std::string::npos ? std::string{} : key.substr(0, s);
		}

		std::string FileOf(const std::string& key)
		{
			const auto s = key.find_last_of("\\/");
			return s == std::string::npos ? key : key.substr(s + 1);
		}

		// Never let one bad file take the game down: an exception in a worker would end the process.
		template <class F, class R>
		R Guarded(F&& f, R fallback)
		{
			try {
				return f();
			} catch (const std::exception& e) {
				Dev::Log("error", "exception in a picture job: {}", e.what());
				return fallback;
			} catch (...) {
				Dev::Log("error", "unknown exception in a picture job");
				return fallback;
			}
		}

		std::string GamePath(const std::string& file)
		{
			return "textures\\PersonalGallery\\Cache\\" + file;
		}

		std::wstring LowerExt(const fs::path& f)
		{
			auto e = f.extension().wstring();
			std::transform(e.begin(), e.end(), e.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return e;
		}

		// Any still image: TGA and DDS through DirectXTex, everything else through Windows (WIC: PNG, JPEG,
		// BMP, WebP, TIFF, and HEIC / AVIF where Windows has those codecs).
		HRESULT LoadImageFile(const fs::path& f, DirectX::TexMetadata* meta, DirectX::ScratchImage& out)
		{
			const auto ext = LowerExt(f);
			if (ext == L".tga") {
				return DirectX::LoadFromTGAFile(f.c_str(), meta, out);
			}
			if (ext == L".dds") {
				DirectX::ScratchImage raw;
				HRESULT               hr = DirectX::LoadFromDDSFile(f.c_str(), DirectX::DDS_FLAGS_NONE, meta, raw);
				if (FAILED(hr)) {
					return hr;
				}
				if (DirectX::IsCompressed(raw.GetMetadata().format)) {  // block-compressed: unpack the top level
					hr = DirectX::Decompress(*raw.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, out);
				} else {
					hr = out.InitializeFromImage(*raw.GetImage(0, 0, 0));
				}
				if (meta && SUCCEEDED(hr)) {
					*meta = out.GetMetadata();
				}
				return hr;
			}
			return DirectX::LoadFromWICFile(f.c_str(), DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, meta, out);
		}

		HRESULT ImageSize(const fs::path& f, DirectX::TexMetadata& meta)
		{
			const auto ext = LowerExt(f);
			if (ext == L".tga") {
				return DirectX::GetMetadataFromTGAFile(f.c_str(), meta);
			}
			if (ext == L".dds") {
				return DirectX::GetMetadataFromDDSFile(f.c_str(), DirectX::DDS_FLAGS_NONE, meta);
			}
			return DirectX::GetMetadataFromWICFile(f.c_str(), DirectX::WIC_FLAGS_NONE, meta);
		}

		// A frame a little way into a video (skips black fade-ins), as RGBA.
		bool LoadVideoStill(const Picture& p, DirectX::ScratchImage& out, DirectX::TexMetadata& meta)
		{
			auto dec = Video::Open(p.path.wstring());
			if (!dec) {
				return false;
			}
			// ... and on past a dark opening (up to eight seconds): a video that fades in from black gave a black
			// still, shown whenever the video was not playing
			const double want = std::min(1.0, dec->Duration() * 0.1);
			Video::Frame f, best;
			while (dec->Next(f)) {
				best = std::move(f);
				if (best.time < want) {
					continue;
				}
				std::uint64_t sum = 0, n = 0;
				for (size_t i = 0; i + 2 < best.bgra.size(); i += 4 * 37) {
					sum += best.bgra[i] + best.bgra[i + 1] + best.bgra[i + 2];
					++n;
				}
				if (n == 0 || sum / (n * 3) >= 24 || best.time >= want + 8.0) {
					break;
				}
			}
			if (best.bgra.empty() || FAILED(out.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, best.w, best.h, 1, 1))) {
				return false;
			}
			std::uint8_t* px = out.GetImage(0, 0, 0)->pixels;
			for (size_t i = 0; i < best.bgra.size(); i += 4) {  // BGRA -> RGBA
				px[i] = best.bgra[i + 2];
				px[i + 1] = best.bgra[i + 1];
				px[i + 2] = best.bgra[i];
				px[i + 3] = 255;
			}
			meta = out.GetMetadata();
			return true;
		}

		// Load, crop to the picture's canvas shape and scale so the long side is at most maxSide.
		bool LoadCropped(const Picture& p, int maxSide, DirectX::ScratchImage& out)
		{
			DirectX::TexMetadata  meta{};
			DirectX::ScratchImage raw;
			if (!p.video) {  // a giant image would need gigabytes as raw pixels: refuse it rather than run out of memory
				DirectX::TexMetadata size{};
				if (FAILED(ImageSize(p.path, size)) || size.width > 16384 || size.height > 16384 ||
					static_cast<double>(size.width) * size.height > 120.0e6) {
					return false;
				}
			}
			if (p.video ? !LoadVideoStill(p, raw, meta)
			            : FAILED(LoadImageFile(p.path, &meta, raw))) {
				return false;
			}
			DirectX::ScratchImage rgba;
			const DirectX::Image* src = raw.GetImage(0, 0, 0);
			if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
				if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba))) {
					return false;
				}
				src = rgba.GetImage(0, 0, 0);
			}

			const float aspect = Catalog::kAspects[p.aspect];
			size_t      cw = src->width, ch = src->height;
			if (static_cast<float>(cw) / ch > aspect) {
				cw = static_cast<size_t>(ch * aspect);
			} else {
				ch = static_cast<size_t>(cw / aspect);
			}
			DirectX::ScratchImage crop;
			if (FAILED(crop.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, cw, ch, 1, 1))) {
				return false;
			}
			const DirectX::Rect rect((src->width - cw) / 2, (src->height - ch) / 2, cw, ch);
			if (FAILED(DirectX::CopyRectangle(*src, rect, *crop.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, 0))) {
				return false;
			}

			const float k = std::min(1.0f, static_cast<float>(maxSide) / std::max(cw, ch));
			size_t      tw = std::max<size_t>(4, static_cast<size_t>(cw * k) & ~size_t(3));
			size_t      th = std::max<size_t>(4, static_cast<size_t>(ch * k) & ~size_t(3));
			return SUCCEEDED(DirectX::Resize(*crop.GetImage(0, 0, 0), tw, th, DirectX::TEX_FILTER_FANT, out));
		}

		void FilterInPlace(DirectX::ScratchImage& img, int look, int adjust = 0, int extra = 0)
		{
			const int filter = look / kTones, tone = look % kTones + adjust * 16;
			if (filter == 0 && tone == 0 && extra == 0) {
				return;
			}
			const DirectX::Image* im = img.GetImage(0, 0, 0);
			const auto src = Filters::FromRGBA8(im->pixels, static_cast<int>(im->width), static_cast<int>(im->height), static_cast<int>(im->rowPitch));
			auto       f = Filters::Apply(src, filter);
			if (const float k = Catalog::FilterStrength(extra); filter > 0 && k < 0.999f && f.px.size() == src.px.size()) {
				for (size_t i = 0; i < f.px.size(); ++i) {  // the player's strength: part filtered, part the picture itself
					f.px[i] = src.px[i] + (f.px[i] - src.px[i]) * k;
				}
			}
			Filters::Tone(f, tone);
			Filters::Extra(f, extra);
			Filters::ToRGBA8(f, im->pixels, static_cast<int>(im->rowPitch));
		}

		// The last pictures read, decoded and cropped: changing a filter, tone or tint again doesn't read the file again
		bool LoadCroppedCached(const Picture& p, int maxSide, DirectX::ScratchImage& out)
		{
			struct Kept
			{
				std::wstring          path;
				int                   size{ 0 };
				DirectX::ScratchImage img;
			};
			static std::mutex      keptLock;
			static std::deque<Kept> kept;  // the last few (one picture is edited at a time)
			{
				std::lock_guard l(keptLock);
				for (auto& k : kept) {
					if (k.path == p.path.wstring() && k.size == maxSide && k.img.GetImageCount()) {
						return SUCCEEDED(out.InitializeFromImage(*k.img.GetImage(0, 0, 0)));
					}
				}
			}
			if (!LoadCropped(p, maxSide, out)) {
				return false;
			}
			std::lock_guard l(keptLock);
			Kept k{ p.path.wstring(), maxSide, {} };
			if (SUCCEEDED(k.img.InitializeFromImage(*out.GetImage(0, 0, 0)))) {
				kept.push_front(std::move(k));
				while (kept.size() > 3) {
					kept.pop_back();
				}
			}
			return true;
		}

		// a_maxSide: 0 for the look's full size, or a smaller one (the draft)
		bool MakeTexture(const Picture& p, int look, const fs::path& file, int adjust = 0, int extra = 0, int a_maxSide = 0)
		{
			DirectX::ScratchImage img;
			if (!LoadCroppedCached(p, a_maxSide > 0 ? a_maxSide : look / kTones == 0 ? (highDetail ? kClearMaxHigh : kClearMax) : kFilterMax, img)) {
				return false;
			}
			FilterInPlace(img, look, adjust, extra);
			DirectX::ScratchImage mips, bc;
			if (FAILED(DirectX::GenerateMipMaps(*img.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, mips)) ||
				FAILED(DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_BC1_UNORM,
					DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, bc))) {  // one core: never all of them at once
				return false;
			}
			const fs::path tmp = file.wstring() + L".tmp";
			if (FAILED(DirectX::SaveToDDSFile(bc.GetImages(), bc.GetImageCount(), bc.GetMetadata(), DirectX::DDS_FLAGS_NONE, tmp.c_str()))) {
				return false;
			}
			std::error_code ec;
			fs::rename(tmp, file, ec);
			return !ec;
		}

		std::shared_ptr<const Thumb> MakeThumb(const Picture& p, int look)
		{
			DirectX::ScratchImage img;
			// filters look right at a larger size, then shrink
			if (!LoadCropped(p, look / kTones == 0 ? kThumbMax : kThumbMax * 2, img)) {
				return nullptr;
			}
			FilterInPlace(img, look);
			const DirectX::Image* im = img.GetImage(0, 0, 0);
			DirectX::ScratchImage shrunk;
			if (im->width > kThumbMax || im->height > kThumbMax) {
				const float k = static_cast<float>(kThumbMax) / std::max(im->width, im->height);
				if (FAILED(DirectX::Resize(*im, std::max<size_t>(1, size_t(im->width * k)), std::max<size_t>(1, size_t(im->height * k)), DirectX::TEX_FILTER_FANT, shrunk))) {
					return nullptr;
				}
				im = shrunk.GetImage(0, 0, 0);
			}
			auto t = std::make_shared<Thumb>();
			t->w = static_cast<int>(im->width);
			t->h = static_cast<int>(im->height);
			t->rgba.resize(static_cast<size_t>(t->w) * t->h * 4);
			for (int y = 0; y < t->h; ++y) {
				std::memcpy(&t->rgba[static_cast<size_t>(y) * t->w * 4], im->pixels + y * im->rowPitch, static_cast<size_t>(t->w) * 4);
			}
			return t;
		}

		void Worker()
		{
			Video::Startup();
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);  // never compete with the game
			for (;;) {
				Job      job;
				Picture  snapshot;
				{
					std::unique_lock l(lock);
					wake.wait(l, [] { return !jobs.empty(); });
					// what a painting shows comes before the cards' thumbnails (a filter strength change waited
					// behind dozens of them and looked ignored)
					auto pick = std::find_if(jobs.begin(), jobs.end(), [](const Job& j) { return !j.thumb; });
					if (pick == jobs.end()) {
						pick = jobs.begin();
					}
					job = *pick;
					jobs.erase(pick);
					// what a painting shows is made promptly; card thumbnails wait their turn at the lowest priority
					SetThreadPriority(GetCurrentThread(), job.thumb ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_BELOW_NORMAL);
					snapshot = pictures[job.pic];
				}
				if (job.thumb) {
					auto t = Guarded([&] { return MakeThumb(snapshot, job.filter); }, std::shared_ptr<const Thumb>{});
					std::lock_guard l(lock);
					pictures[job.pic].thumbs[job.filter] = t ? t : std::make_shared<Thumb>();
					++jobsDone;
					continue;
				}
				if (job.adjust || job.extra) {  // a look with the player's colour adjustments, tint, vignette, sharpness
					std::string name = CacheName(snapshot, job.filter);
					name.insert(name.size() - 4, "_b" + std::to_string(job.adjust) + "_x" + std::to_string(job.extra));
					const fs::path file = cacheDir / name;
					std::error_code touchEc;
					const bool     have = fs::exists(file, touchEc);  // (the kind that reports trouble instead of throwing)
					if (have) {
						fs::last_write_time(file, fs::file_time_type::clock::now(), touchEc);  // in use: kept by the cache sweep
					}
					if (!have && job.filter / kTones != 0) {  // a filter takes seconds to paint: a small draft goes up first
						std::string draft = name;
						draft.insert(draft.size() - 4, "_d");
						std::error_code draftEc;
						if (fs::exists(cacheDir / draft, draftEc) ||
							Guarded([&] { return MakeTexture(snapshot, job.filter, cacheDir / draft, job.adjust, job.extra, kDraftMax); }, false)) {
							std::function<void()> draftCb;
							{
								std::lock_guard l(lock);
								pictures[job.pic].tuned[TunedKey(job.filter, job.adjust, job.extra)] = GamePath(draft);
								draftCb = onReady;
							}
							if (draftCb) {
								draftCb();
							}
						}
					}
					const bool ok = have || Guarded([&] { return MakeTexture(snapshot, job.filter, file, job.adjust, job.extra); }, false);
				if (!ok) {
					Dev::Log("fail", "tuned texture for '{}' (look {}, adjust {}, extra {}) failed - using the plain look", snapshot.name, job.filter, job.adjust, job.extra);
				}
					std::function<void()> cb;
					{
						std::lock_guard l(lock);
						// failed (unreadable, disk full): fall back to the plain look rather than retrying forever
						pictures[job.pic].tuned[TunedKey(job.filter, job.adjust, job.extra)] =
							ok ? GamePath(name) : pictures[job.pic].texture[job.filter];
						pictures[job.pic].queuedTuned.erase(TunedKey(job.filter, job.adjust, job.extra));
						++jobsDone;
						cb = onReady;
					}
					if (ok && cb) {
						cb();
					}
					continue;
				}
				const fs::path file = cacheDir / CacheName(snapshot, job.filter);
				bool           ok = false;
				{
					Dev::Timer t("texture " + snapshot.name + " look " + std::to_string(job.filter), 3000.0);
					std::error_code existsEc;
					const bool      have = fs::exists(file, existsEc);
					if (!have && job.filter / kTones != 0) {  // a filter takes seconds to paint: a small draft goes up first
						std::string draft = CacheName(snapshot, job.filter);
						draft.insert(draft.size() - 4, "_d");
						if (fs::exists(cacheDir / draft, existsEc) ||
							Guarded([&] { return MakeTexture(snapshot, job.filter, cacheDir / draft, 0, 0, kDraftMax); }, false)) {
							std::function<void()> draftCb;
							{
								std::lock_guard l(lock);
								if (pictures[job.pic].texture[job.filter].empty()) {
									pictures[job.pic].texture[job.filter] = GamePath(draft);
								}
								draftCb = onReady;
							}
							if (draftCb) {
								draftCb();
							}
						}
					}
					ok = have || Guarded([&] { return MakeTexture(snapshot, job.filter, file); }, false);
				}
				if (!ok) {
					Dev::Log("fail", "texture for '{}' (look {}) could not be made", snapshot.name, job.filter);
				}
				std::function<void()> cb;
				{
					std::lock_guard l(lock);
					if (ok) {
						pictures[job.pic].texture[job.filter] = GamePath(Utf8(file.filename()));
					}
					++jobsDone;
					cb = onReady;
				}
				if (ok && cb) {
					cb();
				}
			}
		}

		// Remove cached looks and filtered videos of pictures that were deleted or changed since,
		// and half-written files from a game that closed mid-way. Called with `lock` held, before the workers start.
		void CleanCache()
		{
			std::vector<std::string> keep;
			for (const auto& p : pictures) {
				keep.push_back(p.id + "_");
			}
			auto sweep = [&](const fs::path& dir) {
				std::error_code ec;
				// (stepped by hand: a range-for throws - and would end the game - when Windows can't read an entry)
				for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
					const auto& e = *it;
					if (!e.is_regular_file(ec)) {
						continue;
					}
					const std::string name = Utf8(e.path().filename());
					if (name == "index.txt") {
						continue;  // what scans learned (LoadIndex)
					}
					if (name.rfind("plate_", 0) == 0 || name.rfind("frame_", 0) == 0 || name.rfind("mat_", 0) == 0) {
						// engraved nameplates and recoloured frames (Look.cpp), touched whenever a painting uses them:
						// kept while in use (sweeping them made every coloured frame be painted again at each launch)
						const auto age = fs::file_time_type::clock::now() - fs::last_write_time(e.path(), ec);
						if (!ec && age > std::chrono::hours(24 * 60)) {
							fs::remove(e.path(), ec);
						}
						continue;
					}
					if (name.size() > 4 && name.rfind(".mp4") == name.size() - 4) {
						fs::remove(e.path(), ec);  // a filtered copy of a video, from when videos could be filtered
						continue;
					}
					const bool        partial = name.find(".tmp") != std::string::npos;
					const bool        used = std::any_of(keep.begin(), keep.end(), [&](const std::string& k) { return name.rfind(k, 0) == 0; });
					// a tuned look (the player's colour, tint, vignette, sharpness) no painting has used for two weeks:
					// slider positions tried and left behind (one in use is touched each time a painting loads it)
					bool stale = false;
					if (name.find("_b") != std::string::npos && name.find("_x") != std::string::npos) {  // Worker names them _b<adjust>_x<extra>
						const auto age = fs::file_time_type::clock::now() - fs::last_write_time(e.path(), ec);
						stale = !ec && age > std::chrono::hours(24 * 14);
					}
					if (partial || !used || stale) {
						fs::remove(e.path(), ec);
					}
				}
			};
			sweep(cacheDir);
			sweep(cacheDir / "Video");
		}

		void Queue(int pic, int filter, bool thumb, bool urgent)
		{
			auto& p = pictures[pic];
			bool& queued = thumb ? p.queuedThumb[filter] : p.queuedTex[filter];
			if (queued) {
				if (urgent) {  // move it to the front
					auto it = std::find_if(jobs.begin(), jobs.end(), [&](const Job& j) { return j.pic == pic && j.filter == filter && j.thumb == thumb; });
					if (it != jobs.end()) {
						Job j = *it;
						jobs.erase(it);
						jobs.push_front(j);
					}
				}
				return;
			}
			queued = true;
			++jobsTotal;
			if (urgent) {
				jobs.push_front({ pic, filter, thumb });
			} else {
				jobs.push_back({ pic, filter, thumb });
			}
			wake.notify_one();
		}
	}

	namespace
	{
		bool Supported(const fs::path& f)
		{
			const auto ext = LowerExt(f);
			static const std::set<std::wstring> images{ L".png", L".jpg", L".jpeg", L".jpe", L".jfif", L".bmp", L".webp", L".tif", L".tiff",
				L".tga", L".dds", L".heic", L".heif", L".avif", L".gif" };
			return images.count(ext) || Video::IsVideoFile(f.wstring());
		}

		std::string Stamp(const fs::path& f)
		{
			std::error_code ec;
			const auto      when = fs::last_write_time(f, ec).time_since_epoch().count();
			char            buf[32];
			snprintf(buf, sizeof(buf), "%08llx", static_cast<unsigned long long>((when ^ static_cast<long long>(fs::file_size(f, ec))) & 0xffffffffull));
			return buf;
		}

		// FNV-1a over the size and the first and last 64 KB: the same picture under any name or folder.
		std::string Fingerprint(const fs::path& f)
		{
			std::error_code ec;
			const auto      size = fs::file_size(f, ec);
			std::uint64_t   h = 1469598103934665603ull;
			auto            mix = [&](const std::uint8_t* d, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                    h = (h ^ d[i]) * 1099511628211ull;
                }
			};
			mix(reinterpret_cast<const std::uint8_t*>(&size), sizeof(size));
			if (FILE* fh = _wfopen(f.c_str(), L"rb")) {
				std::vector<std::uint8_t> buf(65536);
				size_t                    n = fread(buf.data(), 1, buf.size(), fh);
				mix(buf.data(), n);
				if (size > buf.size() * 2) {
					_fseeki64(fh, -static_cast<long long>(buf.size()), SEEK_END);
					n = fread(buf.data(), 1, buf.size(), fh);
					mix(buf.data(), n);
				}
				fclose(fh);
			}
			char out[24];
			snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
			return out;
		}

		// Downloaded files are often named by a hash ("011e627224009690..."): no use as a label.
		bool CodeName(const std::string& a_stem)
		{
			if (a_stem.size() < 16) {
				return false;
			}
			return std::all_of(a_stem.begin(), a_stem.end(), [](unsigned char c) { return std::isxdigit(c) || c == '-' || c == '_'; });
		}

		// Everything about a file except what the workers make later. False if it can't be read.
		// What a scan learned about each file, kept between launches: an unchanged file (same date and size)
		// is never opened or fingerprinted again - hundreds of pictures and videos start in a moment.
		struct Known
		{
			std::string stamp, id;
			size_t      width{ 0 }, height{ 0 };
		};
		std::map<std::wstring, Known> sizeIndex;
		std::set<std::string>         cacheNames;  // the cache folder's files, listed once per scan

		fs::path IndexFile() { return cacheDir / "index.txt"; }

		void LoadIndex()
		{
			std::ifstream in(IndexFile(), std::ios::binary);
			std::string   line;
			while (std::getline(in, line)) {  // stamp, id, width, height, path (UTF-8), tab-separated
				std::vector<std::string> f;
				for (size_t a = 0, b; a <= line.size(); a = b + 1) {
					b = line.find('\t', a);
					if (b == std::string::npos) {
						b = line.size();
					}
					f.push_back(line.substr(a, b - a));
				}
				if (f.size() != 5 || f[4].empty()) {
					continue;
				}
				const std::u8string u(f[4].begin(), f[4].end());
				sizeIndex[fs::path(u).wstring()] = { f[0], f[1], std::strtoull(f[2].c_str(), nullptr, 10), std::strtoull(f[3].c_str(), nullptr, 10) };
			}
		}

		void SaveIndex()
		{
			const fs::path tmp = IndexFile().wstring() + L".tmp";
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				for (const auto& [path, k] : sizeIndex) {
					std::error_code ec;
					if (!fs::exists(path, ec)) {
						continue;  // gone: forget it
					}
					out << k.stamp << '\t' << k.id << '\t' << k.width << '\t' << k.height << '\t' << Utf8(path) << '\n';
				}
			}
			std::error_code ec;
			fs::rename(tmp, IndexFile(), ec);
		}

		bool Describe(const fs::path& f, Picture& p)
		{
			std::error_code ec;
			p.video = Video::IsVideoFile(f.wstring());
			size_t width = 0, height = 0;
			p.stamp = Stamp(f);
			const auto hit = sizeIndex.find(f.wstring());
			if (hit != sizeIndex.end() && hit->second.stamp == p.stamp && hit->second.width && hit->second.height) {
				width = hit->second.width;  // seen before, unchanged: no need to open it
				height = hit->second.height;
			} else if (p.video && LowerExt(f) == L".gif") {
				DirectX::TexMetadata meta{};
				if (FAILED(ImageSize(f, meta))) {
					return false;
				}
				width = meta.width;
				height = meta.height;
			} else if (p.video) {
				auto dec = Video::Open(f.wstring());
				if (!dec) {
					return false;  // no decoder for this format on this PC
				}
				width = dec->Width();
				height = dec->Height();
			} else {
				DirectX::TexMetadata meta{};
				if (FAILED(ImageSize(f, meta))) {
					return false;
				}
				width = meta.width;
				height = meta.height;
			}
			if (!width || !height) {
				return false;
			}
			p.path = f;
			p.key = Utf8(fs::relative(f, folder, ec));  // just the file name for pictures in the main folder
			if (p.key.empty()) {
				p.key = Utf8(f.filename());
			}
			p.name = Utf8(f.stem());
			p.aspect = Catalog::NearestAspect(static_cast<float>(width) / height);
			const bool same = hit != sizeIndex.end() && hit->second.stamp == p.stamp && !hit->second.id.empty();
			p.id = same ? hit->second.id : Fingerprint(f);
			sizeIndex[f.wstring()] = { p.stamp, p.id, width, height };
			for (int i = 0; i < kLooks; ++i) {  // its looks already made (one listing of the cache, not a disk check each)
				const std::string name = CacheName(p, i);
				if (cacheNames.count(name)) {
					p.texture[i] = GamePath(name);
				}
			}
			return true;
		}

		// Bring the library in line with the folder. Indices never change: a removed picture keeps its
		// slot (marked removed), a renamed or moved one is recognised by its fingerprint and keeps its
		// slot, a new one is added at the end. Safe while the game runs.
		std::array<int, 3> Scan()
		{
			std::error_code       ec;
			std::vector<fs::path> files;
			// (stepped by hand: a range-for throws when Windows can't read a folder or a name, which ended the game)
			for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec) && Supported(it->path())) {
					files.push_back(it->path());
				}
			}
			std::sort(files.begin(), files.end());
			cacheNames.clear();
			for (fs::directory_iterator it(cacheDir, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec)) {
					cacheNames.insert(Utf8(it->path().filename()));
				}
			}

			// what is already known and unchanged needs no work
			std::map<std::wstring, std::string> known;  // path -> stamp
			{
				std::lock_guard l(lock);
				for (const auto& p : pictures) {
					if (!p.removed) {
						known[p.path.wstring()] = p.stamp;
					}
				}
			}
			std::vector<Picture>     fresh;
			std::set<std::wstring>   present;
			for (const auto& f : files) {
				present.insert(f.wstring());
				auto it = known.find(f.wstring());
				if (it != known.end() && it->second == Stamp(f)) {
					continue;
				}
				Picture p;
				if (Guarded([&] { return Describe(f, p); }, false)) {
					fresh.push_back(std::move(p));
				}
			}

			std::array<int, 3> stats{};  // added, removed, renamed or moved
			std::lock_guard    l(lock);
			for (auto& p : pictures) {  // gone, or changed (the changed file comes back as fresh)
				if (!p.removed && (!present.count(p.path.wstring()) ||
									  std::any_of(fresh.begin(), fresh.end(), [&](const Picture& n) { return n.path == p.path; }))) {
					p.removed = true;
					++stats[1];
				}
			}
			for (auto& n : fresh) {
				int slot = -1;
				for (int i = 0; i < static_cast<int>(pictures.size()); ++i) {
					if (pictures[i].removed && pictures[i].id == n.id) {  // renamed or moved: same picture, same slot
						slot = i;
						break;
					}
				}
				if (slot >= 0) {
					auto& old = pictures[slot];
					old.path = n.path;
					old.key = n.key;
					old.name = n.name;
					old.stamp = n.stamp;
					old.removed = false;
					--stats[1];
					++stats[2];
				} else {
					pictures.push_back(std::move(n));
					slot = static_cast<int>(pictures.size()) - 1;
					++stats[0];
				}
				if (CodeName(pictures[slot].name)) {  // a friendly label instead; rename the file to choose your own
					pictures[slot].name = (pictures[slot].video ? "Video " : "Picture ") + std::to_string(slot + 1);
				}
				Queue(slot, 0, true, false);  // thumbnail
				if (pictures[slot].texture[1].empty()) {  // clear + Skyrim tone, the default look
					Queue(slot, 1, false, false);
				}
			}
			if (stats[0] || stats[1] || stats[2]) {
				++generation;
			}
			Guarded([] { SaveIndex(); return true; }, false);
			return stats;
		}
	}

	void Init()
	{
		const fs::path root = fs::current_path() / "Data";
		folder = root / "PersonalGallery" / "Pictures";
		cacheDir = root / "textures" / "PersonalGallery" / "Cache";
		std::error_code ec;
		fs::create_directories(folder, ec);
		fs::create_directories(cacheDir, ec);
		Video::Startup();
		LoadFavourites();
		Guarded([] { LoadIndex(); return true; }, false);
		Scan();
		{
			std::lock_guard l(lock);
			CleanCache();
		}
		std::thread(Worker).detach();
		std::thread(Worker).detach();  // two: a look being painted never waits behind the cards' thumbnails
	}

	std::array<int, 3> Rescan()
	{
		std::lock_guard    l(scanLock);
		std::array<int, 3> stats{};
		std::thread([&] {  // on its own thread: Media Foundation and COM stay off the caller's
			Video::Startup();
			stats = Scan();
		}).join();
		return stats;
	}

	int CacheMB()
	{
		std::uintmax_t  total = 0;
		std::error_code ec;
		for (const auto& dir : { cacheDir, cacheDir / "Video" }) {
			for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec)) {
					total += it->file_size(ec);
				}
			}
			ec.clear();
		}
		return static_cast<int>(total >> 20);
	}

	void ClearCache()
	{
		std::lock_guard l(lock);
		std::error_code ec;
		for (const auto& dir : { cacheDir, cacheDir / "Video" }) {
			for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec) && Utf8(it->path().filename()) != "index.txt") {
					std::error_code gone;
					fs::remove(it->path(), gone);
				}
			}
			ec.clear();
		}
		for (auto& p : pictures) {  // nothing is made any more: each look is painted again when next asked for
			for (int i = 0; i < kLooks; ++i) {
				p.texture[i].clear();
				p.queuedTex[i] = false;
			}
			p.tuned.clear();
			p.queuedTuned.clear();
		}
		++generation;
	}

	bool Available(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) && !pictures[a_pic].removed;
	}

	int AvailableCount()
	{
		std::lock_guard l(lock);
		return static_cast<int>(std::count_if(pictures.begin(), pictures.end(), [](const Picture& p) { return !p.removed; }));
	}

	int Next(int a_pic, int a_step)
	{
		std::lock_guard l(lock);
		const int n = static_cast<int>(pictures.size());
		if (n == 0) {
			return -1;
		}
		int i = std::clamp(a_pic, -1, n);
		for (int tries = 0; tries <= n; ++tries) {
			i = ((i + a_step) % n + n) % n;
			if (!pictures[i].removed) {
				return i;
			}
		}
		return -1;
	}

	int Valid(int a_pic)
	{
		if (Available(a_pic)) {
			return a_pic;
		}
		return Next(a_pic, 1);
	}

	std::vector<Info> Snapshot(std::uint64_t& a_generation)
	{
		std::lock_guard   l(lock);
		std::vector<Info> out;
		out.reserve(pictures.size());
		for (const auto& p : pictures) {
			out.push_back({ p.name, ParentOf(p.key), p.video, !p.removed, favourites.count(p.id) > 0, p.aspect });
		}
		a_generation = generation;
		return out;
	}

	std::uint64_t Generation()
	{
		std::lock_guard l(lock);
		return generation;
	}

	void SetDetail(bool a_high)
	{
		std::lock_guard l(lock);
		if (highDetail == a_high) {
			return;
		}
		highDetail = a_high;
		std::error_code ec;
		for (auto& p : pictures) {  // clear looks now come from the other size (made when next asked for)
			for (int t = 0; t < kTones; ++t) {
				p.texture[t] = fs::exists(cacheDir / CacheName(p, t), ec) ? GamePath(CacheName(p, t)) : std::string{};
				p.queuedTex[t] = false;
			}
		}
	}

	std::string Album(int a_pic)
	{
		std::lock_guard l(lock);
		if (a_pic < 0 || a_pic >= static_cast<int>(pictures.size())) {
			return {};
		}
		return ParentOf(pictures[a_pic].key);
	}

	std::vector<std::string> Albums()
	{
		std::set<std::string> found;
		{
			std::lock_guard l(lock);
			for (const auto& p : pictures) {
				if (!p.removed) {
					found.insert(ParentOf(p.key));
				}
			}
		}
		found.erase("");
		return { found.begin(), found.end() };
	}

	bool Favourite(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) && favourites.count(pictures[a_pic].id);
	}

	void SetFavourite(int a_pic, bool a_on)
	{
		std::lock_guard l(lock);
		if (a_pic < 0 || a_pic >= static_cast<int>(pictures.size())) {
			return;
		}
		if (a_on) {
			favourites.insert(pictures[a_pic].id);
		} else {
			favourites.erase(pictures[a_pic].id);
		}
		SaveFavourites();
		++generation;
	}

	int NextSlide(int a_pic, int a_mode)
	{
		std::vector<int> pool;
		std::string      album;
		{
			std::lock_guard l(lock);
			const int n = static_cast<int>(pictures.size());
			if (a_pic >= 0 && a_pic < n) {
				album = ParentOf(pictures[a_pic].key);
			}
			for (int i = 0; i < n; ++i) {
				const auto& p = pictures[i];
				if (p.removed || (a_mode == 3 && !favourites.count(p.id)) ||
					(a_mode == 4 && ParentOf(p.key) != album)) {
					continue;
				}
				pool.push_back(i);
			}
		}
		if (pool.empty()) {
			return a_pic;  // nothing to change to (no favourites yet, say)
		}
		if (a_mode == 2) {  // shuffle: anything but the current one
			static std::mt19937 rng{ std::random_device{}() };
			if (pool.size() > 1) {
				std::erase(pool, a_pic);
			}
			return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
		}
		auto it = std::upper_bound(pool.begin(), pool.end(), a_pic);
		return it == pool.end() ? pool.front() : *it;
	}

	std::string Id(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) ? pictures[a_pic].id : std::string{};
	}

	int Count()
	{
		std::lock_guard l(lock);
		return static_cast<int>(pictures.size());
	}

	std::string Key(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) ? pictures[a_pic].key : std::string{};
	}

	std::string Name(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) ? pictures[a_pic].name : std::string{};
	}

	int Aspect(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) ? pictures[a_pic].aspect : 5;
	}

	bool IsVideo(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) && pictures[a_pic].video;
	}

	std::wstring Path(int a_pic)
	{
		std::lock_guard l(lock);
		return a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) ? pictures[a_pic].path.wstring() : std::wstring{};
	}

	int Find(const std::string& a_key, const std::string& a_id)
	{
		std::lock_guard l(lock);
		const int n = static_cast<int>(pictures.size());
		for (int i = 0; i < n; ++i) {
			if (!pictures[i].removed && _stricmp(pictures[i].key.c_str(), a_key.c_str()) == 0) {
				return i;
			}
		}
		if (!a_id.empty()) {  // renamed or moved
			for (int i = 0; i < n; ++i) {
				if (!pictures[i].removed && pictures[i].id == a_id) {
					return i;
				}
			}
		}
		// moved into (or out of) a subfolder, from before fingerprints were saved
		const std::string file = FileOf(a_key);
		for (int i = 0; i < n; ++i) {
			if (!pictures[i].removed && !file.empty() && _stricmp(Utf8(pictures[i].path.filename()).c_str(), file.c_str()) == 0) {
				return i;
			}
		}
		return -1;
	}

	std::string Texture(int a_pic, int a_filter, int a_code, int a_extra)
	{
		if (a_filter == 0) {  // no filter: its strength means nothing - and with nothing else, the plain picture
			a_extra &= ~(31 << 22);
			if ((a_extra & 127) == 0 && ((a_extra >> 12) & 31) == 0 && ((a_extra >> 17) & 31) == 10) {
				a_extra = 0;
			}
		}
		const int a_tone = Filters::ToneIndex(a_code), adjust = Filters::ToneAdjust(a_code);
		std::lock_guard l(lock);
		if ((adjust || a_extra) && a_pic >= 0 && a_pic < static_cast<int>(pictures.size()) && a_filter >= 0 && a_filter < kFilters && a_tone < kTones) {
			auto&      p = pictures[a_pic];
			const auto key = TunedKey(a_filter * kTones + a_tone, adjust, a_extra);
			if (const auto it = p.tuned.find(key); it != p.tuned.end()) {
				if (!it->second.empty()) {
					return it->second;
				}
				return p.texture[a_filter * kTones + a_tone];  // failed and no plain look yet: wait for that one
			}
			if (p.queuedTuned.insert(key).second) {
				++jobsTotal;
				jobs.push_front({ a_pic, a_filter * kTones + a_tone, false, adjust, a_extra });
				wake.notify_one();
			}
			return {};
		}
		if (a_pic < 0 || a_pic >= static_cast<int>(pictures.size()) || a_filter < 0 || a_filter >= kFilters || a_tone < 0 || a_tone >= kTones) {
			return {};
		}
		auto&     p = pictures[a_pic];
		const int look = a_filter * kTones + a_tone;
		if (p.texture[look].empty()) {
			Queue(a_pic, look, false, true);
		}
		return p.texture[look];
	}

	std::shared_ptr<const Thumb> Thumbnail(int a_pic, int a_filter, int a_code)
	{
		const int a_tone = Filters::ToneIndex(a_code);
		std::lock_guard l(lock);
		if (a_pic < 0 || a_pic >= static_cast<int>(pictures.size()) || a_filter < 0 || a_filter >= kFilters || a_tone < 0 || a_tone >= kTones) {
			return nullptr;
		}
		auto&     p = pictures[a_pic];
		const int look = a_filter * kTones + a_tone;
		if (!p.thumbs[look]) {
			Queue(a_pic, look, true, true);
		}
		return p.thumbs[look];
	}

	std::string Status()
	{
		std::lock_guard l(lock);
		if (jobs.empty()) {
			return {};
		}
		return "Preparing pictures " + std::to_string(jobsDone) + " / " + std::to_string(jobsTotal);
	}

	void SetReadyCallback(std::function<void()> a_callback)
	{
		std::lock_guard l(lock);
		onReady = std::move(a_callback);
	}
}

