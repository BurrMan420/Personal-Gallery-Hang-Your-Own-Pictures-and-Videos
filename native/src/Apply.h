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

#include <vector>

// Puts a picture (with a filter) on a painting's "Canvas" shape at runtime.
namespace Apply
{
	void Init();
	void Frame();
	void KeepOpaque();
	bool IsPending(RE::TESObjectREFR* a_ref);  // its new look is still being made  // every frame: paintings never fade in or out  // every frame: puts pictures back on paintings whose 3D reloaded

	// Applies now if the texture is ready and the painting is loaded, otherwise as soon as it is.
	void Set(RE::TESObjectREFR* a_ref, int a_pic, int a_filter, int a_tone);

	// Main thread only: put this texture on the canvas right now (clear-filter surface).
	void ShowMissing(RE::TESObjectREFR* a_ref);  // parchment saying the picture is missing
	void SetNow(RE::TESObjectREFR* a_ref, const std::string& a_diffuse);

	// Crop: zoom (1 = whole picture), pan -1..1 (left/top .. right/bottom), mirror.
	void SetView(RE::TESObjectREFR* a_ref, float a_zoom, float a_panX, float a_panY, bool a_flip);
	// the canvas surface: gloss 0..1 (matte .. varnished), opacity 0..1
	// a_glow: how strongly a projected picture (a projector's, the basin's) lights itself, 0..1
	void SetSurface(RE::TESObjectREFR* a_ref, float a_gloss, float a_opacity, float a_glow = 0.5f);
	// tint, vignette, sharpness (Catalog::PictureExtra): a changed one makes the picture again
	void SetExtra(RE::TESObjectREFR* a_ref, int a_extra);
	// every painting with a picture on it (for picking the one in view)
	std::vector<RE::ObjectRefHandle> Paintings();

	// Main thread only: textures of the first shape whose name starts with a_shape (lit shapes only).
	bool SetShapeTextures(RE::TESObjectREFR* a_ref, std::string_view a_shape, const std::string& a_diffuse, const std::string& a_normal);
}
