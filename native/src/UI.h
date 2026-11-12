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
#include <vector>

// Build mode UI, drawn with ImGui on the game's swap chain:
//  - the gallery panel (mouse cursor, categories, cards; game controls blocked while it is open)
//  - while placing, a HUD with the wheel setting and key hints (fighting, activating, sneaking and
//    menus are blocked so the mouse buttons, wheel and E / R / F / Q / Z / X / Tab belong to build mode)
// Choices go to Papyrus as a "PG_Select" mod event: strArg = tab key, or one of the actions
// "place", "cancel", "takedown", "nearest", "hide", "show"; numArg = index.
namespace UI
{
	enum Tab
	{
		kPicture,
		kFilter,
		kTone,
		kLife,
		kMagic,
		kDisplay,
		kFrame,
		kSize,
		kLight,
		kSlideshow,
		kGlow,  // where the magic glow sits
		// options shown as rows (same order as Look::Option)
		kIntensity,
		kFrameWidth,
		kFrameDepth,
		kMat,
		kGlass,
		kAge,
		kLamp,
		kPlate,
		kZoom,
		kPanX,
		kPanY,
		kFlip,
		kVolume,
		kPlayMode,
		kQuality,
		kSlideTime,
		kLightLevel,
		kStashOpen,  // how the hidden stash opens
		// magic, light and the picture's colour, all open-ended
		kMagicHue,
		kMagicSat,
		kMagicStrength,
		kMagicAlpha,
		kMagicMotion,
		kMagicSpeed,
		kMagicDepth,
		kMixEdge,
		kMixAura,
		kMixCorners,
		kMixSheen,
		kMixVeil,
		kSweep,
		kLightHue,
		kLightBright,
		kLightReach,
		kPicBright,
		kPicContrast,
		kPicGamma,
		kPicSat,
		kPicWarm,
		// the picture's surface and edges, the sweep's speed, the light's place and flicker, the hours
		kPicGloss,
		kPicOpacity,
		kTintHue,
		kTintStrength,
		kVignette,
		kSharpness,
		kSheenSpeed,
		kLightSide,
		kLightHeight,
		kLightOut,
		kLightFlicker,
		kLightFlickerSpeed,
		kLightWhen,
		kMagicWhen,
		kNightStart,
		kNightEnd,
		kSheenAngle,  // 0, 45, 90, 135 degrees
		kFilterStrength,
		kStashAmount,
		kStashSpeed,
		kGlassShine,
		kMatWidth,
		kPlateFont,
		kBeam,
		kVision,
		kGlassThick,
		kFrameHue,
		kFrameTint,
		kPlateHue,
		kPlateSize,
		kProjTilt,
		kProjSize,
		kMatHue,
		kMatTint,
		kVideoFps,
		kProjTurn,
		kDayNight,  // 0 the same picture all day, 1 another picture at night
		kNightPic,  // that night picture
		kTabCount  // every choice a painting has (Papyrus passes them in this order)
	};

	void Install();  // after the renderer exists (kDataLoaded)

	// a_isNew: a new painting (otherwise an existing one is being adjusted)
	void Open(const std::array<int, kTabCount>& a_selection, bool a_menu, int a_menuKey, bool a_isNew, const std::string& a_title,
		const std::string& a_stashName);
	void Set(int a_tab, int a_index);
	void SetSlidePicks(const std::vector<std::string>& a_ids);  // the slideshow's ticked pictures (before Open)
	// a build-mode key the player chose (mod menu): a_action as PGControlScript's hotkey list, a_key a scan code
	void SetHotkey(int a_action, int a_key);
	void SetMenuKey(int a_key);  // the gallery key (MCM), so its press is answered at once
	void CancelOpening();        // the key was pressed but the gallery won't open (no pictures, a menu...)
	void Browse(bool a_on);      // gallery mode (Home): look at a painting to change it, N for a new one
	bool IsBrowsing();
	void SetUndoList(const std::vector<std::string>& a_items);  // what Undo can reach, newest first (before Open)
	void Close();
	bool MenuOpen();
	bool IsOpen();  // build mode
	std::vector<int> GetDefaultStyle();  // the look new paintings start with, if the player saved one (else empty)
	std::vector<int> GetStyle(int a_index);  // a saved style's choices (UI.h Tab order), empty if none
}
