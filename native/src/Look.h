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

#include <string>
#include <vector>

// Everything about a painting's look beyond picture / filter / display / frame: glow intensity,
// frame width and depth, mat, glass, age, lamp, nameplate, crop, and the video options.
// Shows and fits the pieces every gallery model carries hidden (see tools/meshes.py add_finish).
namespace Look
{
	// Order of the options (UI selection after the tabs; PGControlScript keeps the same order).
	enum Option
	{
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
		kStashOpen,  // used by the scripts (Extras::Swing), kept with the look
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
		kSheenAngle,
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
		kProjTilt,  // projectors: the projected picture leans (10 = level, 3 degrees a step)
		kProjSize,  // projectors: how big the projection is (5 = 100 %, 10 % a step from 50 %)
		kMatHue,    // the mat's colour (0 its own, then every 5 degrees) ...
		kMatTint,   // ... and how strongly (0..20)
		kVideoFps,  // a video painting's own frame rate (0 as the mod menu says, then 15, 24, 30, 60)
		kProjTurn,  // projectors: the projected picture turned left or right (10 = straight, 3 degrees a step)
		kOptionCount
	};

	// What a brand-new painting starts with.
	inline constexpr int kDefaults[kOptionCount]{ 1, 1, 1, 0, 0, 0, 0, 0, 0, 4, 4, 0, 20, 0, 2, 1, 1, 0, 0, 16, 10, 20, 0, 10, 0, 10, 10, 0, 10, 10, 0, 0, 10, 10, 15, 15, 15, 15, 15, 3, 20, 0, 8, 0, 10, 10, 10, 10, 10, 0, 10, 0, 0, 20, 6, 0, 20, 10, 6, 10, 1, 0, 6, 5, 1, 0, 10, 0, 1, 10, 5, 0, 10, 0, 10 };

	void Set(RE::TESObjectREFR* a_ref, const std::vector<int>& a_options, const std::string& a_title);
	// whether paintings cast shadows (MCM); every painting is refitted with it
	void SetShadows(bool a_on);
	// where a painting's light sits (Light page: left / right, height, distance), or false if not known
	bool LightSpot(RE::TESObjectREFR* a_ref, int& a_side, int& a_height, int& a_out);

	// Every frame (any thread): schedules the main-thread work for paintings waiting on their 3D.
	void Frame();
}
