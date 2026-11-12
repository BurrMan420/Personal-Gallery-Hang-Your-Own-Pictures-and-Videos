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

#pragma once

#include <cstdint>
#include <vector>

// Artistic picture filters (ports of tools/textures.py). Images are linear RGB floats 0..1.
namespace Filters
{
	struct Image
	{
		int                w{ 0 };
		int                h{ 0 };
		std::vector<float> px;  // w * h * 3

		Image() = default;
		Image(int a_w, int a_h) : w(a_w), h(a_h), px(static_cast<size_t>(a_w) * a_h * 3, 0.0f) {}
		float*       at(int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 3]; }
		const float* at(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 3]; }
	};

	// filter index as in Catalog::kFilters; 0 (clear) returns the image unchanged
	Image Apply(const Image& a_src, int a_filter, std::uint32_t a_seed = 5);

	// colour grade as in Catalog::kTones; 0 (true colours) does nothing. The number may also carry the
	// player's own adjustments (ToneCode): brightness, contrast, gamma, saturation, warmth.
	void Tone(Image& a_img, int a_tone);
	inline int ToneIndex(int a_code) { return a_code & 15; }
	inline int ToneAdjust(int a_code) { return a_code >> 4; }

	// Tint and vignette (Catalog::PictureExtra).
	void Extra(Image& a_img, int a_extra);

	Image FromRGBA8(const std::uint8_t* a_rgba, int a_w, int a_h, int a_rowPitch);
	void  ToRGBA8(const Image& a_img, std::uint8_t* a_out, int a_rowPitch);
}
