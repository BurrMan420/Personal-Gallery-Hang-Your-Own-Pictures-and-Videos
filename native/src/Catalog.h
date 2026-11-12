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

// Shared catalogue. The order here must match tools/catalog.py (records and meshes)
// and PGControlScript (which only ever passes indices).

#include <algorithm>
#include <algorithm>
#include <algorithm>
#include <array>
#include <cmath>
#include <cmath>
#include <cmath>
#include <string_view>

#include <string>

namespace Catalog
{
	// a part's name in lower case: the game keeps one copy of each name whatever its capitals ("glass" may come back
	// as "GLASS"), so every name check compares in lower case
	inline std::string Lower(const char* a_name)
	{
		std::string s = a_name ? a_name : "";
		for (auto& c : s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}
	// width / height of each canvas shape; pictures are centre-cropped to the nearest one
	// (new shapes are only ever added at the end: hung paintings keep their records)
	inline constexpr std::array kAspects{ 0.5f, 0.5625f, 0.6667f, 0.75f, 0.8f, 1.0f, 1.25f, 1.3333f, 1.5f, 1.7778f, 2.0f, 2.3333f,
		0.4286f, 0.3333f, 3.0f, 3.5556f, 4.0f };  // tall phone (9:21), scroll (1:3), panorama (3:1), 32:9, 4:1

	inline constexpr std::array<std::string_view, 18> kDisplays{
		"Framed", "Canvas", "Pinned parchment", "Banner", "Tapestry", "Mural", "On an easel", "Table frame",
		"Mirror of Mara", "Scrying orb", "Apocrypha page", "Dwemer projector", "Scrying basin", "Dwemer wall projector",
		"Scrying basin (vision)", "Dwemer projector (below)", "Dwemer projector (side)", "Dwemer projector (narrow)"
	};
	// displays that hang on a wall: only these can hide a wall safe
	constexpr bool HangsOnWall(int a_display)
	{
		// (a solid thing flat on the wall: framed, canvas, parchment, banner, tapestry, mural, mirror. Not a floating
		// page or a projected picture: there is nothing there to hide a safe behind.)
		return a_display <= 5 || a_display == 8;
	}

	inline constexpr std::array<std::string_view, 18> kDisplayKeys{
		"framed", "canvas", "poster", "banner", "tapestry", "mural", "easel", "table", "mirror", "orb", "page",
		"projector", "basin", "projector_wall", "basin_vision", "projector_low", "projector_side", "projector_narrow"
	};

	inline constexpr std::array<std::string_view, 22> kFrames{
		"Dark wood", "Gilded", "Black lacquer", "Nordic carved", "Dwemer bronze",
		"Imperial", "Elven gold", "Riften driftwood", "Solitude silver", "Daedric iron",
		"Windhelm grey", "Markarth bronze", "Jarl's longhouse", "Enchanted gilt",
		"Falmer chitin", "Orcish iron", "Glass (malachite)", "Ebony", "Stalhrim", "Dragonbone", "Ratway", "Dawnguard castle"
	};
	inline constexpr std::array<std::string_view, 22> kFrameKeys{
		"wood", "gold", "black", "nordic", "dwemer", "imperial", "elven", "driftwood", "silver", "daedric",
		"windhelm", "markarth", "longhouse", "enchanted",
		"falmer", "orcish", "glass", "ebony", "stalhrim", "dragonbone", "ratway", "castle"
	};

	inline constexpr std::array<std::string_view, 18> kFilters{
		"None", "Oil painting", "Watercolor", "Charcoal on parchment", "Old master", "Woven",
		"Skyrim painting", "Fresco", "Woodcut print", "Mosaic",
		"Illuminated manuscript", "Stained glass", "Ink wash", "Carved stone", "Scorched parchment", "Gilded icon",
		"Apocrypha ink", "Elder Scroll"
	};
	inline constexpr std::array<std::string_view, 18> kFilterKeys{
		"clear", "oil", "watercolor", "sketch", "aged", "woven", "skyrim", "fresco", "woodcut", "mosaic",
		"illuminated", "stainedglass", "inkwash", "stone", "scorched", "icon", "apocrypha", "elderscroll"
	};

	// colour grade applied after the filter; "True colours" leaves the picture untouched
	inline constexpr std::array<std::string_view, 9> kTones{
		"None", "Skyrim tone", "Candlelit", "Nordic winter", "Faded", "Moody dusk", "Moonlit", "Sovngarde gold", "Blackreach glow"
	};
	inline constexpr std::array<std::string_view, 9> kToneKeys{ "true", "skyrim", "candle", "winter", "faded", "dusk", "moonlit", "sovngarde", "blackreach" };

	inline constexpr std::array<std::string_view, 5> kSizes{ "Small", "Normal", "Large", "Huge", "Mural" };
	inline constexpr std::array<float, 5>            kSizeScales{ 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

	inline constexpr std::array<std::string_view, 6> kLights{ "None", "Warm lamp", "Candlelight", "Moonlight", "Magelight",
		"Match the magic" };
	inline constexpr int kMatchMagicLight = 5;
	// UI swatch colours for the lights (RGB); "Match the magic" is drawn in the magic's colour
	inline constexpr std::array<std::array<float, 3>, 6> kLightColours{ { { 0.15f, 0.15f, 0.15f },
		{ 1.0f, 0.84f, 0.63f }, { 1.0f, 0.67f, 0.35f }, { 0.59f, 0.71f, 1.0f }, { 0.75f, 0.88f, 1.0f }, { 1.0f, 1.0f, 1.0f } } };
	inline constexpr std::array<std::string_view, 3> kLightLevels{ "Dim", "Normal", "Bright" };

	// slideshow: which picture comes next, and how often
	inline constexpr std::array<std::string_view, 6> kSlideshow{ "Off", "In order", "Shuffle", "Favourites", "This album", "My picks" };
	inline constexpr int kSlideMyPicks = 5;  // the pictures the player ticked (Slideshow page)

	// how fast the painting moves aside for its hidden stash
	inline constexpr std::array<std::string_view, 4> kStashSpeeds{ "Quick", "Normal", "Slow", "Very slow" };
	inline constexpr std::array<float, 4>            kStashSeconds{ 0.45f, 0.8f, 1.3f, 2.0f };
	inline constexpr std::array<std::string_view, 7> kSlideTimes{ "5 s", "10 s", "30 s", "1 min", "5 min", "15 min", "1 hour" };
	inline constexpr std::array<float, 7>            kSlideSeconds{ 5, 10, 30, 60, 300, 900, 3600 };

	// --- Finish (framed paintings) ---------------------------------------------------------------
	inline constexpr std::array<std::string_view, 3> kFrameWidths{ "Slim", "Normal", "Wide" };
	inline constexpr std::array<std::string_view, 4> kFrameDepths{ "Flat", "Normal", "Deep", "Very deep" };
	inline constexpr std::array<float, 4>            kFrameDepthScales{ 0.6f, 1.0f, 1.5f, 2.1f };
	inline constexpr int                             kAutoFrame = 22;  // Frame tab "Auto": index after the frames
	inline constexpr std::array<std::string_view, 13> kMats{ "None", "Parchment", "Linen", "Black velvet", "Burgundy",
		"Forest green", "Nordic weave", "Ivory", "Charcoal", "Midnight blue", "Gold leaf", "Leather", "Snow fur" };
	inline constexpr std::array<std::string_view, 13> kMatKeys{ "", "parchment", "linen", "velvet", "burgundy", "forest", "nordic",
		"ivory", "charcoal", "midnight", "goldleaf", "leather", "snowfur" };
	inline constexpr std::array<std::string_view, 3> kGlass{ "None", "Clear glass", "Old glass" };
	inline constexpr std::array<std::string_view, 3> kAges{ "Clean", "Dusty", "Weathered" };
	inline constexpr std::array<std::string_view, 4> kLamps{ "None", "Brass picture light", "Imperial candle sconces",
		"Nordic candle sconces" };
	inline constexpr std::array<std::string_view, 4> kPlates{ "None", "Brass", "Wood", "Iron" };
	// how the painting moves aside to show the hidden stash behind it
	// 0: no stash; otherwise a wall safe hides behind the painting and the painting moves aside this way
	inline constexpr std::array<std::string_view, 8> kStashOpenings{ "Off", "Swings left", "Swings right", "Lifts up", "Drops down",
		"Slides left", "Slides right", "Slides up" };

	// --- Crop -----------------------------------------------------------------------------------
	inline constexpr std::array<std::string_view, 7> kZooms{ "1x", "1.15x", "1.3x", "1.5x", "1.75x", "2x", "2.5x" };
	inline constexpr std::array<float, 7>            kZoomScales{ 1.0f, 1.15f, 1.3f, 1.5f, 1.75f, 2.0f, 2.5f };
	inline constexpr int                             kPanSteps = 9;  // 0 left / top ... 4 centre ... 8 right / bottom
	inline constexpr std::array<std::string_view, 2> kFlips{ "Normal", "Mirrored" };

	// --- Video ----------------------------------------------------------------------------------
	// video volume: a slider in 5 % steps, 0 (mute) to 200 %; 20 = 100 %
	inline constexpr int kVolumeSteps = 41;
	inline constexpr int kVolumeNormal = 20;
	constexpr float      VolumeScale(int a_step) { return a_step * 0.05f; }
	inline constexpr std::array<std::string_view, 2> kPlayModes{ "Loop", "Play once" };
	inline constexpr std::array<std::string_view, 5> kQualities{ "Low", "Medium", "High", "Ultra", "Full HD" };
	inline constexpr std::array<int, 5>              kQualityWidths{ 480, 720, 960, 1440, 1920 };

	// --- Glow -----------------------------------------------------------------------------------
	inline constexpr std::array<std::string_view, 4> kIntensities{ "Subtle", "Normal", "Bright", "Radiant" };
	inline constexpr std::array<float, 4>            kIntensityScales{ 0.45f, 1.0f, 1.6f, 2.4f };

	// open-ended magic: how it moves (0 keeps the magic's own motion)
	inline constexpr std::array<std::string_view, 7> kMotions{ "Its own", "Steady", "Breathe", "Pulse", "Heartbeat", "Flicker", "Crackle" };

	inline constexpr std::array<std::string_view, 4> kFlickers{ "Steady", "Candle", "Pulse", "Breathe" };
	inline constexpr std::array<std::string_view, 3> kWhen{ "Always", "Night only", "Day only" };

	// a night that runs from a_start to a_end o'clock (it may cross midnight)
	inline bool InHours(float a_hour, int a_start, int a_end)
	{
		if (a_start == a_end) {
			return true;  // the same hour for both: night all day (a "Night only" light simply stays on)
		}
		return a_start < a_end ? (a_hour >= a_start && a_hour < a_end) : (a_hour >= a_start || a_hour < a_end);
	}

	// the extra look a painting's picture is made with: tint, vignette, sharpness (0 = none)
	// and how strongly the filter shows (20 = fully; stored as 20 - strength so the usual case is 0)
	inline int PictureExtra(int a_tintHue, int a_tintStrength, int a_vignette, int a_sharpness, int a_filterStrength = 20)
	{
		const int weaker = 20 - std::clamp(a_filterStrength, 0, 20);
		if (a_tintHue == 0 && a_vignette == 0 && a_sharpness == 10 && weaker == 0) {
			return 0;
		}
		return (a_tintHue & 127) | ((a_tintStrength & 31) << 7) | ((a_vignette & 31) << 12) | ((a_sharpness & 31) << 17) | (weaker << 22);
	}
	inline float FilterStrength(int a_extra) { return 1.0f - ((a_extra >> 22) & 31) / 20.0f; }

	// hue (degrees) to an RGB colour of this saturation, full brightness
	inline void HueRGB(float a_hue, float a_sat, float& r, float& g, float& b)
	{
		const float h = std::fmod(std::max(a_hue, 0.0f), 360.0f) / 60.0f;
		const float x = 1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f);
		float       c[3]{};
		switch (static_cast<int>(h)) {
		case 0: c[0] = 1, c[1] = x; break;
		case 1: c[0] = x, c[1] = 1; break;
		case 2: c[1] = 1, c[2] = x; break;
		case 3: c[1] = x, c[2] = 1; break;
		case 4: c[0] = x, c[2] = 1; break;
		default: c[0] = 1, c[2] = x; break;
		}
		r = 1 - a_sat + a_sat * c[0];
		g = 1 - a_sat + a_sat * c[1];
		b = 1 - a_sat + a_sat * c[2];
	}

	// living portraits (videos): see Player::Mode
	inline constexpr std::array<std::string_view, 3> kLife{ "Still", "Plays when looked at", "Always moving" };

	// magic glow along the picture's edge and around the frame (PersonalGalleryNative, Magic.cpp)
	inline constexpr std::array<std::string_view, 11> kMagic{ "None", "Enchanted", "Arcane gold", "Ethereal",
		"Daedric violet", "Ember", "Soul gem", "Frost", "Storm", "Sovngarde", "Nocturnal" };

	enum class MagicStyle
	{
		kBreathe,  // slow swell
		kFlicker,  // firelight
		kCrackle,  // sudden bright arcs
		kPulse     // heartbeat
	};

	struct MagicLook
	{
		float      r, g, b;
		float      edge;   // strength of the band on the picture's edge
		float      aura;   // strength of the glow around the frame
		float      speed;  // how fast it breathes / flickers
		MagicStyle style;
	};

	// where a magic glow sits (the Glow tab). Strengths of each piece: edge band, aura, corner gems,
	// sweeping sheen, drifting veil. The last style is the classic Skyrim effect shader over the whole
	// painting, played by Papyrus.
	inline constexpr std::array<std::string_view, 7> kGlowStyles{ "Along the edge", "Soft aura", "Corner gems",
		"Sweeping sheen", "Drifting veil", "Grand", "Shimmer" };
	inline constexpr int kClassicGlow = 6;
	inline constexpr std::array<std::array<float, 5>, 7> kGlowMix{ {
		{ 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },   // along the edge
		{ 0.0f, 1.9f, 0.0f, 0.0f, 0.0f },   // soft aura
		{ 0.35f, 0.5f, 1.3f, 0.0f, 0.0f },  // corner gems
		{ 0.3f, 0.6f, 0.0f, 1.0f, 0.0f },   // sweeping sheen
		{ 0.4f, 0.0f, 0.0f, 0.0f, 1.0f },   // drifting veil
		{ 1.0f, 1.1f, 1.0f, 0.7f, 0.5f },   // grand
		{ 0.45f, 0.7f, 0.0f, 0.8f, 1.0f },  // shimmer: veil and sheen over the whole picture, a soft edge and aura
		                                     // (it used to be one of the game's spell shaders, deaf to every slider)
	} };

	// index = kMagic index (0 unused)
	inline constexpr std::array<MagicLook, 11> kMagicLooks{ {
		{ 0, 0, 0, 0, 0, 0, MagicStyle::kBreathe },
		{ 0.35f, 0.62f, 1.00f, 1.6f, 0.9f, 0.6f, MagicStyle::kBreathe },  // Enchanted
		{ 1.00f, 0.82f, 0.50f, 1.5f, 0.8f, 0.4f, MagicStyle::kBreathe },  // Arcane gold
		{ 0.62f, 0.95f, 1.00f, 1.3f, 1.1f, 0.25f, MagicStyle::kBreathe }, // Ethereal
		{ 0.72f, 0.28f, 1.00f, 1.7f, 0.9f, 0.5f, MagicStyle::kPulse },    // Daedric violet
		{ 1.00f, 0.42f, 0.10f, 1.8f, 0.8f, 1.0f, MagicStyle::kFlicker },  // Ember
		{ 0.58f, 0.50f, 1.00f, 1.5f, 1.0f, 0.8f, MagicStyle::kPulse },    // Soul gem
		{ 0.62f, 0.85f, 1.00f, 1.4f, 0.9f, 0.3f, MagicStyle::kBreathe },  // Frost
		{ 0.70f, 0.72f, 1.00f, 1.6f, 0.8f, 1.0f, MagicStyle::kCrackle },  // Storm
		{ 1.00f, 0.76f, 0.34f, 1.4f, 1.2f, 0.2f, MagicStyle::kBreathe },  // Sovngarde
		{ 0.40f, 0.26f, 0.85f, 1.5f, 1.0f, 0.35f, MagicStyle::kBreathe }, // Nocturnal
	} };

	int NearestAspect(float ratio);
}
