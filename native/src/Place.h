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
#include <cstdint>
#include <vector>

// Smooth placement: while a painting is being placed, its 3D node is moved directly every frame
// (no Papyrus SetPosition, so no reload, fade-in or flicker). The real position is written once,
// when the player places it.
namespace Place
{
	// what the mouse wheel changes
	enum Wheel
	{
		kSize,
		kRotate,  // spin: like a clock hand
		kTurn,    // turn left / right (on a wall: swung off its facing - a column, a corner)
		kTilt,
		kDepth,
		kWheelCount
	};

	// for the HUD
	struct Info
	{
		bool  active{ false };
		bool  snap{ false };
		bool  locked{ false };
		int   surface{ 0 };  // 0 nothing in reach, 1 wall, 2 floor, 3 ceiling
		int   wheel{ kSize };
		float scale{ 1 };
		float rotate{ 0 };  // degrees: spin
		float turn{ 0 };    // degrees: turned left / right
		float tilt{ 0 };
		float depth{ 0 };   // snapped: gap from the surface; free: distance
		bool  grid{ false };
		bool  align{ true };
		bool  aligned{ false };  // snapped level with / in line with another painting this frame
		bool  standing{ false };  // a projector, basin, orb or easel: on the floor, only turned, lifted and sized
		RE::NiPoint3 guideA, guideB;
		float width{ 0 }, height{ 0 };  // game units, as shown (scaled)
		float floorGap{ -1 };           // on a wall: its centre's height above the floor; -1 unknown
	};

	void Begin(RE::TESObjectREFR* a_ref, bool a_snap, float a_scale);
	void SetRef(RE::TESObjectREFR* a_ref);  // the painting was swapped for another record
	void SetScale(float a_scale);
	bool ToggleSnap();
	void LockHere();    // an existing painting: start locked where it hangs (F frees it)
	RE::TESObjectREFR* Current();  // the painting being placed, if any
	bool ToggleLock();  // snapped: hold the spot still (arrows nudge it); false if not snapped
	void ResetAll();    // hold the reset key: size, spin, turn, tilt, depth and height all back to how it came
	void Straighten();  // clears rotation, tilt, wall gap and height
	int  ResetWheel();  // Z: only the setting the wheel changes now goes back to its default; returns which
	int  NextWheel();   // R: what the wheel changes next
	void Scroll(float a_notches);  // mouse wheel (Ctrl held: fine steps)
	// gallery panel open: show the painting as a showcase in the open space beside it, facing you.
	// a_centreX: that space's centre across the screen (-1 left .. 1 right); a_width: its share of the screen width
	// a_lightPage: the Light page is open - the showcase's own light goes out, so only the painting's light shows
	// (and a painting already hung is shown where it hangs)
	void SetSideView(bool a_on, float a_centreX = 0.36f, float a_width = 0.64f, float a_aspect = 16.0f / 9.0f, bool a_lightPage = false);
	// a light kept just over your shoulder while the gallery panel is open, so the preview shows its true colours
	void SetPreviewLight(RE::TESObjectREFR* a_light);
	void SetStashFor(RE::TESObjectREFR* a_ref, bool a_on);  // the painting being placed hides a wall safe
	RE::TESObjectREFR* Pick(float a_maxDist);  // what the crosshair rests on (not the player or actors)
	// the gallery painting the crosshair rests on: by collision, or else by its box (never through a wall)
	RE::TESObjectREFR* PickPainting(float a_maxDist);
	bool ToggleGrid();            // G
	bool ToggleAlign();           // T
	void SetOthers(const std::vector<RE::TESObjectREFR*>& a_refs);  // the other paintings, for alignment
	bool Active();
	Info GetInfo();

	// Ends placement; returns [x, y, z, angleX, angleY, angleZ (degrees), scale].
	std::array<float, 7> End();

	// Held-key state from the input sink (DirectInput scan codes).
	void KeyState(std::uint32_t a_key, bool a_down);

	// Every frame, main thread.
	void Frame();

	// Snap ray used by placement (also exposed to Papyrus): [found, x, y, z, angleX, angleZ, surface].
	std::array<float, 7> SnapTarget(RE::TESObjectREFR* a_painting, float a_maxDist, float a_scale);
}
