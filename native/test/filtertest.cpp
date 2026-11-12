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

// Runs every filter from the plugin on one picture and saves PNGs, to check the C++ port
// outside the game:  FilterTest.exe <picture> <out-dir>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include <DirectXTex.h>
#include <objbase.h>

#include "../src/Filters.h"

int wmain(int argc, wchar_t** argv)
{
	if (argc < 3) {
		std::fwprintf(stderr, L"usage: FilterTest <picture> <out-dir>\n");
		return 1;
	}
	(void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	DirectX::ScratchImage raw, rgba, sized;
	if (FAILED(DirectX::LoadFromWICFile(argv[1], DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, raw))) {
		return 2;
	}
	const auto* im = raw.GetImage(0, 0, 0);
	if (im->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
		DirectX::Convert(*im, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba);
		im = rgba.GetImage(0, 0, 0);
	}
	const float k = 2048.0f / std::max(im->width, im->height);
	DirectX::Resize(*im, size_t(im->width * k) & ~size_t(3), size_t(im->height * k) & ~size_t(3), DirectX::TEX_FILTER_FANT, sized);
	im = sized.GetImage(0, 0, 0);
	const auto src = Filters::FromRGBA8(im->pixels, int(im->width), int(im->height), int(im->rowPitch));

	const wchar_t* names[]{ L"clear", L"oil", L"watercolor", L"sketch", L"aged", L"woven", L"skyrim", L"fresco", L"woodcut", L"mosaic",
		L"illuminated", L"stainedglass", L"inkwash", L"stone", L"scorched", L"icon", L"apocrypha", L"elderscroll",
		L"tone_skyrim", L"tone_candle", L"tone_winter", L"tone_faded", L"tone_dusk", L"tone_moonlit", L"tone_sovngarde", L"tone_blackreach" };
	constexpr int kFilterCount = 18;
	for (int f = 0; f < kFilterCount + 8; ++f) {
		const auto t0 = std::chrono::steady_clock::now();
		auto       out = f < kFilterCount ? Filters::Apply(src, f) : src;
		if (f >= kFilterCount) {
			Filters::Tone(out, f - kFilterCount + 1);
		}
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		DirectX::ScratchImage o;
		o.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, out.w, out.h, 1, 1);
		Filters::ToRGBA8(out, o.GetImage(0, 0, 0)->pixels, int(o.GetImage(0, 0, 0)->rowPitch));
		const auto path = std::filesystem::path(argv[2]) / (std::wstring(names[f]) + L".png");
		DirectX::SaveToWICFile(*o.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str());
		std::wprintf(L"%ls: %lld ms\n", names[f], static_cast<long long>(ms));
	}
	return 0;
}
