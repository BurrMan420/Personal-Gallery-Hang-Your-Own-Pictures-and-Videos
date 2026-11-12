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

// Magic on a painting: every gallery model carries hidden glow pieces - a band along the picture's
// edge (tucked under the frame), an aura around the whole piece, corner gems, a sheen that sweeps
// across the picture and a veil drifting over it. They scroll by themselves (NIF controllers); this
// shows the ones the glow style uses, tints them and makes them breathe, flicker, crackle or pulse.
namespace Magic
{
	// a_magic: Catalog::kMagic (0 = none); a_style: Catalog::kGlowStyles (where it sits)
	void Set(RE::TESObjectREFR* a_ref, int a_magic, int a_style);
	void Forget(RE::TESObjectREFR* a_ref);
	void SetIntensity(RE::TESObjectREFR* a_ref, float a_scale);  // Glow tab: Subtle .. Radiant

	// The player's own magic: everything open-ended. Zero hue / motion / depth keep the magic's own.
	struct Custom
	{
		float hue{ 0 };       // degrees + 1 (0: the magic's colour)
		float sat{ 0.8f };
		float strength{ 1 };
		float alpha{ 1 };
		int   motion{ 0 };    // Catalog::kMotions
		float speed{ 1 };
		float depth{ 0 };     // 0: the magic's own; else how far it dims between beats (0..1)
		float mix[5]{ 1, 1, 1, 1, 1 };  // edge, aura, corners, sheen, veil: x the glow style's own
		bool  reverse{ false };         // sheen and veil sweep the other way
		float sweepSpeed{ 1 };          // how fast the sheen and veil travel
		int   sheenAngle{ 0 };          // 0, 1, 2, 3: 0, 45, 90, 135 degrees (the model's magic_sheen_45 ...)
		int   when{ 0 };                // Catalog::kWhen
		int   nightStart{ 20 }, nightEnd{ 6 };
	};
	void SetCustom(RE::TESObjectREFR* a_ref, const Custom& a_custom);

	// Every frame, render thread (with the other per-frame work).
	void Frame();
}
