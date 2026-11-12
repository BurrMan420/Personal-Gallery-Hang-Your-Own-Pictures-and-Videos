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

#include <array>
#include <string>

// Small living touches for hung paintings:
//  - Swing: a painting swings or slides aside to show the hidden stash behind it, and back.
//  - Day and night: a painting can show another picture between dusk and dawn; switches on its own.
namespace Extras
{
	// a_style: Catalog::kStashOpenings (swing left / right, lift up, drop down, slide left / right / up)
	void Swing(RE::TESObjectREFR* a_ref, bool a_open, int a_style);
	// Show how it will open: swing aside, pause, swing back (works on the painting being placed).
	void PreviewSwing(RE::TESObjectREFR* a_ref, int a_style);

	// how far the wall safe stands out from the wall, fittings and all (tools/meshes.py SAFE_REACH, and a little):
	// the painting stands off the wall by this, so nothing of the safe comes through it
	inline constexpr float kSafeDepth = 3.0f;

	// The safe keeps to a believable size: never a toy, never a vault door (x the 40 x 32 model).
	inline constexpr float kSafeMinScale = 0.6f;
	inline constexpr float kSafeMaxScale = 1.4f;

	// The safe's scale behind a painting of these bounds, and how far the painting stands off the wall for it.
	float SafeScale(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi, float a_scale);
	// The smallest painting scale that still hides the smallest safe.
	float StashMinScale(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi);

	// The safe's door swings open on its hinge (and shut again).
	void SafeDoor(RE::TESObjectREFR* a_safe, bool a_open);

	// What the player called the safe ("" = Hidden stash), for the prompt.
	void SetStashName(RE::TESObjectREFR* a_painting, const std::string& a_name);
	std::string StashName(RE::TESObjectREFR* a_painting);
	float SafePush(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi, float a_scale);

	// Look option: 0 no stash, else how it opens (Catalog::kStashOpenings); how far (x the usual) and in how many seconds.
	void SetStash(RE::TESObjectREFR* a_ref, int a_opening, float a_amount = 1.0f, float a_seconds = 0.8f);

	// The painting being placed is showing how it opens (Place turns its showcase a little to show it).
	bool HidesStash(RE::TESObjectREFR* a_ref);  // a display a wall safe can sit behind
	bool Previewing();
	int  PreviewStyle();  // how the previewed stash opens (0..6, as Swing's style), -1 when none is playing

	// How this painting's stash opens (0: none).
	int StashOpening(RE::TESObjectREFR* a_ref);

	// The hidden stash the player is looking at, within reach (for the prompt); empty handle if none.
	RE::ObjectRefHandle StashInView();

	// A painting whose display hangs on a wall (framed, canvas, banner, ...): only these hide a safe.
	bool HangsOnWall(RE::TESObjectREFR* a_ref);

	// A painting of this mod (by its record's plugin).
	bool IsPainting(RE::TESObjectREFR* a_ref);

	// Where the wall safe goes behind a painting, sized to stay hidden behind it:
	// [x, y, z, angleX, angleY, angleZ (degrees), scale].
	std::array<float, 7> StashSpot(RE::TESObjectREFR* a_painting);

	// a_night < 0: the same picture all day
	void SetDayNight(RE::TESObjectREFR* a_ref, int a_day, int a_night, int a_filter, int a_tone, int a_life);

	// A painting's light (its own light reference) and the player's colour, strength and reach for it.
	void LinkLight(RE::TESObjectREFR* a_painting, RE::TESObjectREFR* a_light);
	// while placing: put the painting's light where it will be (as PGControlScript.PositionLight does), by its shown spot
	void FollowLight(RE::TESObjectREFR* a_painting, const RE::NiTransform& a_shown, int a_side, int a_height, int a_out);
	void SetLightLook(RE::TESObjectREFR* a_painting, float a_hue, float a_strength, float a_reach);  // hue: degrees + 1, 0 its own
	// the light's flicker (Catalog::kFlickers) and speed, when it shines (Catalog::kWhen), and the painting's night hours
	void SetSchedule(RE::TESObjectREFR* a_painting, int a_flicker, float a_flickerSpeed, int a_lightWhen, int a_nightStart, int a_nightEnd);

	// Every frame, render thread.
	void Frame();
}
