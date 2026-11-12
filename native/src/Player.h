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

#include <d3d11.h>

// Living portraits: video playback on paintings with positional sound.
// Each playing painting borrows a placeholder "slot" texture; the slot's GPU texture is pointed at
// our own dynamic texture, which is refreshed with decoded frames every game frame.
namespace Player
{
	enum Mode
	{
		kStill = 0,   // shows the still frame only (until woken by the spell)
		kWakes = 1,   // comes alive when you look at it or walk near
		kAlways = 2,  // always moving while you are in range
	};

	void Init();

	void SetLive(RE::TESObjectREFR* a_ref, int a_pic, int a_filter, int a_tone, int a_mode);
	void Stop(RE::TESObjectREFR* a_ref);
	void StopAll();  // a loading screen: every video lets go of its sound and slot (they start again after)
	int  ToggleAwake(RE::TESObjectREFR* a_ref);  // Animate Portrait spell; returns 1 if now awake
	void SetSettings(float a_volume, int a_maxPlaying, int a_quality = 3, int a_fps = 2);  // quality: Catalog::kQualities; fps: 15 / 24 / 30
	// Video tab: volume (1 = normal, above 1 louder), play once instead of looping, live frame width.
	// a_fps: this painting's own frame rate (0 as the mod menu says, 1..4 = 15, 24, 30, 60 a second)
	void SetOptions(RE::TESObjectREFR* a_ref, float a_gain, bool a_once, int a_maxWidth, int a_extra = 0, int a_fps = 0);
	bool IsPlaying(RE::FormID a_ref);

	// while placing: silence or hold a video painting (-1: not a video). Cleared when building ends.
	int  ToggleMute(RE::TESObjectREFR* a_ref);
	int  TogglePause(RE::TESObjectREFR* a_ref);
	bool IsMuted(RE::TESObjectREFR* a_ref);
	bool IsHeld(RE::TESObjectREFR* a_ref);
	void ClearHolds();

	// Called once per rendered frame (from the Present hook).
	void Frame(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
}
