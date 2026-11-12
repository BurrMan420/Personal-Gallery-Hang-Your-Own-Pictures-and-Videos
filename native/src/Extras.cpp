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

#include "Extras.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>

#include "Apply.h"
#include "Catalog.h"
#include "Place.h"
#include "Player.h"
#include "UI.h"

namespace Extras
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr float kSwingDegrees = 75.0f;
		constexpr float kSwingSeconds = 0.45f;
		constexpr float kDoorDegrees = 8.0f;    // the slightest crack: enough to see it give, never enough to look inside
		constexpr float kDoorSeconds = 0.25f;
		constexpr float kDoorHingeX = -14.0f, kDoorHingeY = -1.2f;  // tools/meshes.py SAFE_HINGE
		constexpr float kSafeW = 44.0f, kSafeH = 36.0f;              // tools/meshes.py wall_safe, with a margin

		struct Swinging
		{
			RE::ObjectRefHandle ref;
			bool                open{ false };
			int                 style{ 0 };
			Clock::time_point   start;
			RE::NiTransform     rest;     // where the painting hangs
			bool                haveRest{ false };
			RE::NiAVObject*     root{ nullptr };
			bool                preview{ false };  // swing back on its own after a moment
			float               amount{ 1 };       // x the usual swing / slide
			float               seconds{ 0.8f };
			// what is fixed to the wall beside the painting (sconces and their flames, the picture light, the nameplate):
			// it stays where it is while the painting moves aside
			struct Kept
			{
				RE::NiPointer<RE::NiAVObject> part;
				RE::NiTransform               rest;
			};
			std::vector<Kept> kept;
		};

		// a part of a painting that is fixed to the wall, not to the painting
		bool OnTheWall(const char* a_name)
		{
			const std::string n = Catalog::Lower(a_name);
			return n.starts_with("lamp_") || n.starts_with("plate_") || n.starts_with("pgprop|l|") || n.starts_with("pgprop|r|");
		}

		void PutBack(Swinging& s)
		{
			for (auto& k : s.kept) {
				if (k.part) {
					k.part->local = k.rest;
				}
			}
			s.kept.clear();
		}

		struct DayNight
		{
			RE::ObjectRefHandle ref;
			int                 day{ -1 }, night{ -1 }, filter{ 0 }, tone{ 0 }, life{ 1 };
			int                 showing{ -1 };  // 0 day, 1 night
		};

		std::mutex                     lock;
		std::map<RE::FormID, Swinging> swings;
		std::map<RE::FormID, int>      stashes;  // painting -> how its stash opens (1..)
		struct StashMotion
		{
			float amount{ 1 }, seconds{ 0.8f };
		};
		std::map<RE::FormID, StashMotion> motions;  // painting -> how far and how fast
		std::atomic<bool>                  previewing{ false };
		std::atomic<int>                   previewStyle{ -1 };
		std::map<RE::FormID, std::string> names; // painting -> the safe's name

		struct Door
		{
			RE::ObjectRefHandle ref;
			bool                open{ false };
			Clock::time_point   start;
		};
		std::map<RE::FormID, Door> doors;

		struct LightLook
		{
			RE::ObjectRefHandle light;
			float               hue{ 0 }, strength{ 1 }, reach{ 1 };
			int                 flicker{ 0 }, when{ 0 };
			float               flickerSpeed{ 1 }, on{ 1 };
			RE::NiLight*        seen{ nullptr };  // the light the base values below were read from
			bool                dirty{ false };   // a choice changed: show it on the next frame, not the next round
			RE::NiColor         diffuse;
			float               fade{ 1 }, radius{ 0 };
		};
		std::map<RE::FormID, LightLook> lights;

		struct Hours
		{
			int start{ 20 }, end{ 6 };
		};
		std::map<RE::FormID, Hours> hours;  // each painting's night

		bool NightFor(RE::FormID a_id)
		{
			auto*       cal = RE::Calendar::GetSingleton();
			const float h = cal ? cal->GetHour() : 12.0f;
			const auto  it = hours.find(a_id);
			return it != hours.end() ? Catalog::InHours(h, it->second.start, it->second.end) : (h < 6.0f || h >= 20.0f);
		}

		float FlickerWave(int a_mode, float a_t)
		{
			switch (a_mode) {
			case 1:  // candle
				return 0.82f + 0.1f * std::sin(a_t * 7.3f) + 0.06f * std::sin(a_t * 13.1f + 1.0f) + 0.04f * std::sin(a_t * 23.0f + 2.0f);
			case 2:  // pulse
				return 0.75f + 0.25f * std::sin(a_t * 2.4f);
			case 3:  // breathe
				return 0.85f + 0.15f * std::sin(a_t * 1.2f);
			default:
				return 1.0f;
			}
		}
		RE::ObjectRefHandle            inView;   // stash painting under the crosshair, within reach
		std::map<RE::FormID, DayNight> nights;
		int                            frameCount{ 0 };

		void Show(DayNight& d, bool night)
		{
			d.showing = night ? 1 : 0;
			const int pic = night ? d.night : d.day;
			auto      ref = d.ref.get();
			if (!ref || pic < 0) {
				return;
			}
			Apply::Set(ref.get(), pic, d.filter, d.tone);
			Player::SetLive(ref.get(), pic, d.filter, d.tone, d.life);
		}

		float Ease(float t) { return t * t * (3 - 2 * t); }  // smoothstep
	}

	void Swing(RE::TESObjectREFR* a_ref, bool a_open, int a_style)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		auto& s = swings[a_ref->GetFormID()];
		s.ref = a_ref->CreateRefHandle();
		s.open = a_open;
		s.style = a_style;
		s.start = Clock::now();
		const auto m = motions.find(a_ref->GetFormID());
		s.amount = m != motions.end() ? m->second.amount : 1.0f;
		s.seconds = m != motions.end() ? m->second.seconds : 0.8f;
	}

	bool Previewing() { return previewing.load(); }
	int  PreviewStyle() { return previewStyle.load(); }

	void PreviewSwing(RE::TESObjectREFR* a_ref, int a_style)
	{
		if (!a_ref || a_style < 0) {
			return;
		}
		std::lock_guard l(lock);
		auto& s = swings[a_ref->GetFormID()];
		s.ref = a_ref->CreateRefHandle();
		s.open = true;
		s.style = a_style;
		// (a moment first: the preview leaves the showcase and goes to the wall, where the opening is shown)
		s.start = Clock::now() + std::chrono::milliseconds(450);
		s.preview = true;
		const auto m = motions.find(a_ref->GetFormID());
		s.amount = m != motions.end() ? m->second.amount : 1.0f;
		s.seconds = m != motions.end() ? m->second.seconds : 0.8f;
	}

	float SafeScale(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi, float a_scale)
	{
		return std::clamp(std::min((a_hi.x - a_lo.x) / kSafeW, (a_hi.z - a_lo.z) / kSafeH) * a_scale * 0.9f, kSafeMinScale, kSafeMaxScale);
	}

	float StashMinScale(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi)
	{
		const float fit = std::min((a_hi.x - a_lo.x) / kSafeW, (a_hi.z - a_lo.z) / kSafeH) * 0.9f;
		return fit > 0 ? kSafeMinScale / fit : 0.1f;
	}

	void LinkLight(RE::TESObjectREFR* a_painting, RE::TESObjectREFR* a_light)
	{
		if (!a_painting) {
			return;
		}
		std::lock_guard l(lock);
		auto&      L = lights[a_painting->GetFormID()];
		const auto handle = a_light ? a_light->CreateRefHandle() : RE::ObjectRefHandle{};
		if (handle != L.light) {  // a different light: read its own values afresh
			L.light = handle;
			L.seen = nullptr;
			L.dirty = true;  // (stays set until the new light's 3D is there)
		}
	}

	void FollowLight(RE::TESObjectREFR* a_painting, const RE::NiTransform& a_shown, int a_side, int a_height, int a_out)
	{
		if (!a_painting) {
			return;
		}
		RE::ObjectRefHandle handle;
		{
			std::lock_guard l(lock);
			const auto it = lights.find(a_painting->GetFormID());
			if (it == lights.end()) {
				return;
			}
			handle = it->second.light;
		}
		auto  light = handle.get();
		auto* l3 = light && !light->IsDisabled() ? light->Get3D() : nullptr;
		if (!l3) {
			return;
		}
		const float        s = a_shown.scale;
		const float        out = std::max(20.0f, 45.0f * s * std::max(2, a_out) / 10.0f);
		const RE::NiPoint3 local{ (a_side - 10) * 6.0f * s, -out, 30.0f * s + (a_height - 10) * 6.0f * s };
		l3->local.translate = a_shown.translate + a_shown.rotate * local;
		RE::NiUpdateData u{};
		l3->Update(u);
	}

	void SetLightLook(RE::TESObjectREFR* a_painting, float a_hue, float a_strength, float a_reach)
	{
		if (!a_painting) {
			return;
		}
		std::lock_guard l(lock);
		auto& L = lights[a_painting->GetFormID()];
		L.dirty |= L.hue != a_hue || L.strength != a_strength || L.reach != a_reach;
		L.hue = a_hue;
		L.strength = a_strength;
		L.reach = a_reach;
	}

	void SetSchedule(RE::TESObjectREFR* a_painting, int a_flicker, float a_flickerSpeed, int a_lightWhen, int a_nightStart, int a_nightEnd)
	{
		if (!a_painting) {
			return;
		}
		std::lock_guard l(lock);
		auto& L = lights[a_painting->GetFormID()];
		L.flicker = a_flicker;
		L.flickerSpeed = a_flickerSpeed;
		L.when = a_lightWhen;
		auto& h = hours[a_painting->GetFormID()];
		const bool changed = h.start != a_nightStart || h.end != a_nightEnd;
		h = { a_nightStart, a_nightEnd };
		if (changed) {
			if (auto it = nights.find(a_painting->GetFormID()); it != nights.end()) {
				it->second.showing = -1;  // look at the clock again with the new hours
			}
		}
	}

	void SafeDoor(RE::TESObjectREFR* a_safe, bool a_open)
	{
		if (!a_safe) {
			return;
		}
		std::lock_guard l(lock);
		auto& d = doors[a_safe->GetFormID()];
		d.ref = a_safe->CreateRefHandle();
		d.open = a_open;
		d.start = Clock::now();
	}

	void SetStashName(RE::TESObjectREFR* a_painting, const std::string& a_name)
	{
		if (!a_painting) {
			return;
		}
		std::lock_guard l(lock);
		if (a_name.empty()) {
			names.erase(a_painting->GetFormID());
		} else {
			names[a_painting->GetFormID()] = a_name;
		}
	}

	std::string StashName(RE::TESObjectREFR* a_painting)
	{
		std::lock_guard l(lock);
		const auto it = a_painting ? names.find(a_painting->GetFormID()) : names.end();
		return it != names.end() ? it->second : std::string{};
	}

	float SafePush(const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi, float a_scale)
	{
		return kSafeDepth * SafeScale(a_lo, a_hi, a_scale) + 0.6f;
	}

	void SetStash(RE::TESObjectREFR* a_ref, int a_opening, float a_amount, float a_seconds)
	{
		if (!a_ref) {
			return;
		}
		if (!HangsOnWall(a_ref) || !HidesStash(a_ref)) {
			a_opening = 0;  // an easel, orb or basin stands in the room, a projection or floating page is thin air: no safe
		}
		Place::SetStashFor(a_ref, a_opening > 0);
		std::lock_guard l(lock);
		if (a_opening > 0) {
			stashes[a_ref->GetFormID()] = a_opening;
		} else {
			stashes.erase(a_ref->GetFormID());
		}
		motions[a_ref->GetFormID()] = { std::clamp(a_amount, 0.1f, 2.0f), std::clamp(a_seconds, 0.2f, 3.0f) };
	}

	int StashOpening(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		const auto it = a_ref ? stashes.find(a_ref->GetFormID()) : stashes.end();
		return it != stashes.end() ? it->second : 0;
	}

	RE::ObjectRefHandle StashInView()
	{
		std::lock_guard l(lock);
		return inView;
	}

	bool HangsOnWall(RE::TESObjectREFR* a_ref)
	{
		// the record's model folder is its display type: PersonalGallery\<display>[_<frame>]\shapeNN.nif
		auto* model = a_ref && a_ref->GetBaseObject() ? a_ref->GetBaseObject()->As<RE::TESModel>() : nullptr;
		if (!model || !model->GetModel()) {
			return false;
		}
		std::string path = model->GetModel();
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const auto a = path.find('\\');
		const auto b = a == std::string::npos ? a : path.find('\\', a + 1);
		if (b == std::string::npos) {
			return false;
		}
		const std::string folder = path.substr(a + 1, b - a - 1);
		if (folder == "projector_wall" || folder == "projector_low" || folder == "projector_side") {
			return true;
		}
		const std::string display = folder.substr(0, folder.find('_'));
		for (const char* wall : { "framed", "canvas", "poster", "banner", "tapestry", "mural", "mirror", "page" }) {
			if (display == wall) {
				return true;
			}
		}
		return false;
	}

	// a solid display flat on the wall (Catalog::HangsOnWall): not a projected picture or a floating page
	bool HidesStash(RE::TESObjectREFR* a_ref)
	{
		auto* model = a_ref && a_ref->GetBaseObject() ? a_ref->GetBaseObject()->As<RE::TESModel>() : nullptr;
		const std::string path = Catalog::Lower(model ? model->GetModel() : "");
		return path.find("projector") == std::string::npos && path.find("page") == std::string::npos;
	}

	bool IsPainting(RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		auto* file = base ? base->GetFile(0) : nullptr;
		return file && _stricmp(file->fileName, "PersonalGallery.esp") == 0 && base->Is(RE::FormType::Activator);
	}

	std::array<float, 7> StashSpot(RE::TESObjectREFR* a_painting)
	{
		if (!a_painting) {
			return {};
		}
		const RE::NiPoint3 lo = a_painting->GetBoundMin(), hi = a_painting->GetBoundMax();
		const float        sc = a_painting->GetScale();
		const float        safe = SafeScale(lo, hi, sc);
		const auto         ang = a_painting->GetAngle();
		RE::NiMatrix3      r;
		r.EulerAnglesToAxesZXY(ang.x, ang.y, ang.z);
		const RE::NiPoint3 into{ r.entry[0][1], r.entry[1][1], r.entry[2][1] };  // the painting's +Y: into the wall
		const RE::NiPoint3 back = a_painting->GetPosition() + r * (RE::NiPoint3{ (lo.x + hi.x) / 2, hi.y, (lo.z + hi.z) / 2 } * sc);
		const RE::NiPoint3 wall = back + into * SafePush(lo, hi, sc);  // the painting stands off the wall by this much
		const RE::NiPoint3 at = wall;  // the safe's model has its origin on the wall plane (tools/meshes.py wall_safe)
		constexpr float    deg = 57.29578f;
		return { at.x, at.y, at.z, ang.x * deg, ang.y * deg, ang.z * deg, safe };
	}

	void SetDayNight(RE::TESObjectREFR* a_ref, int a_day, int a_night, int a_filter, int a_tone, int a_life)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		if (a_night < 0 || a_night == a_day) {
			nights.erase(a_ref->GetFormID());
			return;
		}
		auto& d = nights[a_ref->GetFormID()];
		d = { a_ref->CreateRefHandle(), a_day, a_night, a_filter, a_tone, a_life, -1 };
		if (NightFor(a_ref->GetFormID())) {
			Show(d, true);  // the caller has just put the day picture on
		} else {
			d.showing = 0;
		}
	}

	void Frame()
	{
		// what Place knows, asked before taking our own lock (Place asks us things while holding its lock:
		// asking it while holding ours could deadlock against a script thread)
		RE::TESObjectREFR* const placingRef = Place::Current();
		RE::TESObjectREFR*       aimed = nullptr;
		if (frameCount % 6 == 5) {  // (the stash prompt below looks every 6th frame)
			auto* ui = RE::UI::GetSingleton();
			if (!UI::IsOpen() && ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				aimed = Place::PickPainting(230.0f);
			}
		}
		std::lock_guard l(lock);

		// swinging paintings: turn about the left edge, easing in and out
		bool anyPreview = false;
		int  styleNow = -1;
		for (auto it = swings.begin(); it != swings.end();) {
			auto& s = it->second;
			auto  ref = s.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root) {
				it = swings.erase(it);
				continue;
			}
			// the painting being placed: building mode sets where it hangs every frame - swing from there
			const bool placing = placingRef == ref.get();
			anyPreview |= placing && s.preview;
			if (placing && s.preview) {
				styleNow = s.style;
			}
			if (!s.haveRest || root != s.root || placing) {
				if (!s.haveRest || root != s.root) {  // a new swing (or new 3D): note what stays on the wall, as it hangs now
					s.kept.clear();
					if (auto* node = root->AsNode()) {
						for (auto& child : node->GetChildren()) {
							if (child && OnTheWall(child->name.c_str())) {
								s.kept.push_back({ child, child->local });
							}
						}
					}
				}
				s.rest = root->local;
				s.root = root;
				s.haveRest = true;
			}
			if (s.preview && s.open && std::chrono::duration<float>(Clock::now() - s.start).count() > s.seconds + 0.9f) {
				s.open = false;  // a preview: hold open a moment, then back
				s.start = Clock::now();
			}
			const float t = std::clamp(std::chrono::duration<float>(Clock::now() - s.start).count() / s.seconds, 0.0f, 1.0f);
			// How far open, 0..1, in two overlapping moves so it reads as a made thing and not a flap: first it comes a
			// little off the wall (unlatches), then it swings or slides, easing gently in and out. Closing is the same
			// run backwards.
			const float along = s.open ? t : 1.0f - t;
			auto        smooth = [](float x) {
				x = std::clamp(x, 0.0f, 1.0f);
				return x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
			};
			const float unlatch = smooth(along / 0.22f);
			const float amount = smooth((along - 0.12f) / 0.88f);
			// in the painting's own space: x right, y into the wall, z up
			const RE::NiPoint3 lo = ref->GetBoundMin(), hi = ref->GetBoundMax();
			const float        sc = s.rest.scale;
			RE::NiMatrix3      turn;
			turn.entry[0][0] = turn.entry[1][1] = turn.entry[2][2] = 1.0f;
			RE::NiPoint3 hinge{}, slide{};
			// a door-like swing on a side edge opens wide enough to reach in; lifting or dropping on the top or bottom
			// edge opens less (a painting held out flat over your head looked silly)
			const float  deg = std::min(160.0f, (s.style == 2 ? 48.0f : s.style == 3 ? 56.0f : 68.0f) * s.amount) * amount * 0.01745329f;
			auto aboutUp = [&](float a) {  // hinge on a side edge
				turn.entry[0][0] = std::cos(a);
				turn.entry[0][1] = -std::sin(a);
				turn.entry[1][0] = std::sin(a);
				turn.entry[1][1] = std::cos(a);
			};
			auto aboutSide = [&](float a) {  // hinge on the top or bottom edge
				turn.entry[1][1] = std::cos(a);
				turn.entry[1][2] = -std::sin(a);
				turn.entry[2][1] = std::sin(a);
				turn.entry[2][2] = std::cos(a);
			};
			// off the wall before it moves: a little for a swing, more for a slide - and enough to clear the candle
			// sconces when it slides sideways past them
			bool sconces = false;
			for (const auto& kpt : s.kept) {
				sconces |= kpt.part && Catalog::Lower(kpt.part->name.c_str()).starts_with("pgprop|");
			}
			const float pull = -((s.style == 4 || s.style == 5) ? (sconces ? 16.0f : 3.5f) : s.style == 6 ? 3.5f : 1.2f) * unlatch;
			switch (s.style) {
			case 1:  // swing right
				hinge = { hi.x, 0, 0 };
				aboutUp(deg);
				slide = { 0, pull, 0 };
				break;
			case 2:  // lift up on the top edge
				hinge = { 0, 0, hi.z };
				aboutSide(-deg);
				slide = { 0, pull, 0 };
				break;
			case 3:  // drop down on the bottom edge
				hinge = { 0, 0, lo.z };
				aboutSide(deg);
				slide = { 0, pull, 0 };
				break;
			case 4:  // slide left
				slide = { -(hi.x - lo.x) * 1.02f * s.amount * amount, pull, 0 };
				break;
			case 5:  // slide right
				slide = { (hi.x - lo.x) * 1.02f * s.amount * amount, pull, 0 };
				break;
			case 6:  // slide up
				slide = { 0, pull, (hi.z - lo.z) * 1.02f * s.amount * amount };
				break;
			default:  // swing left
				hinge = { lo.x, 0, 0 };
				aboutUp(-deg);
				slide = { 0, pull, 0 };
				break;
			}
			const RE::NiMatrix3 r = s.rest.rotate * turn;
			root->local.rotate = r;
			root->local.translate = s.rest.translate + s.rest.rotate * (hinge * sc) - r * (hinge * sc) + s.rest.rotate * (slide * sc);
			// the painting moved by "turn about the hinge, then slide": what is on the wall is moved back by the same
			{
				const RE::NiMatrix3 back = turn.Transpose();
				for (auto& k : s.kept) {
					k.part->local.rotate = back * k.rest.rotate;
					k.part->local.translate = back * (k.rest.translate - hinge - slide) + hinge;
				}
			}
			RE::NiUpdateData update{};
			root->Update(update);
			if (t >= 1.0f && !s.open) {  // closed again: exactly back where it hangs
				root->local = s.rest;
				PutBack(s);
				root->Update(update);
				it = swings.erase(it);
				continue;
			}
			++it;
		}

		previewing = anyPreview;
		previewStyle = styleNow;

		// safe doors: open a crack about the hinge on the door's left edge, out towards the player
		for (auto it = doors.begin(); it != doors.end();) {
			auto& d = it->second;
			auto  ref = d.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			auto* door = root ? root->GetObjectByName("safe_door") : nullptr;
			if (!door) {
				it = doors.erase(it);
				continue;
			}
			const float t = std::clamp(std::chrono::duration<float>(Clock::now() - d.start).count() / kDoorSeconds, 0.0f, 1.0f);
			const float a = -kDoorDegrees * (d.open ? Ease(t) : 1.0f - Ease(t)) * 0.01745329f;
			RE::NiMatrix3 turn;
			turn.entry[0][0] = std::cos(a);
			turn.entry[0][1] = -std::sin(a);
			turn.entry[1][0] = std::sin(a);
			turn.entry[1][1] = std::cos(a);
			turn.entry[2][2] = 1.0f;
			const RE::NiPoint3 hinge{ kDoorHingeX, kDoorHingeY, 0 };
			door->local.rotate = turn;
			door->local.translate = hinge - turn * hinge;
			RE::NiUpdateData update{};
			door->Update(update);
			if (t >= 1.0f && !d.open) {  // shut: exactly as modelled
				door->local = RE::NiTransform{};
				door->Update(update);
				it = doors.erase(it);
				continue;
			}
			++it;
		}

		++frameCount;

		// paintings' lights: the player's colour, strength, reach, flicker and hours (a reload resets them)
		{
			// (kept in double until wrapped: as a float, a PC that has been on for weeks would flicker in steps)
			const float t = static_cast<float>(std::fmod(std::chrono::duration<double>(Clock::now().time_since_epoch()).count(), 10000.0));
			for (auto& [id, L] : lights) {
				if (!L.dirty && frameCount % (L.flicker ? 2 : 15) != 0) {
					continue;
				}
				auto ref = L.light.get();
				if (!ref || ref->IsDisabled() || !ref->Get3D()) {
					L.seen = nullptr;  // unloaded: read its base values afresh when it is back
					continue;
				}
				auto* x = ref->extraList.GetByType<RE::ExtraLight>();
				RE::NiLight* nl = x && x->lightData ? x->lightData->light.get() : nullptr;
				if (!nl) {
					continue;
				}
				L.dirty = false;
				auto&      rd = nl->GetLightRuntimeData();
				const bool fresh = L.seen != nl;
				if (fresh) {
					L.seen = nl;
					L.diffuse = rd.diffuse;
					L.fade = rd.fade;
					L.radius = rd.radius.x;
				}
				if (L.hue > 0) {
					float r, g, b;
					Catalog::HueRGB(L.hue - 1.0f, 0.75f, r, g, b);
					rd.diffuse = { r, g, b };
				} else {
					rd.diffuse = L.diffuse;
				}
				const bool  night = NightFor(id);
				const float want = L.when == 1 ? (night ? 1.0f : 0.0f) : L.when == 2 ? (night ? 0.0f : 1.0f) : 1.0f;
				L.on = fresh ? want : L.on + (want - L.on) * (L.flicker ? 0.03f : 0.2f);  // just loaded: no flash
				rd.fade = L.fade * L.strength * L.on * FlickerWave(L.flicker, t * L.flickerSpeed);
				rd.radius = { L.radius * L.reach, L.radius * L.reach, L.radius * L.reach };
			}
		}

		// a stash painting under the crosshair, within reach, while just playing (not building, no menus)
		if (frameCount % 6 == 0) {
			inView = {};
			if (auto* hit = aimed; hit && !stashes.empty() && stashes.count(hit->GetFormID()) && !swings.count(hit->GetFormID())) {
				inView = hit->CreateRefHandle();
			}
		}

		// day and night pictures: look at the clock a couple of times a second
		if (frameCount % 30 || nights.empty()) {
			return;
		}
		for (auto it = nights.begin(); it != nights.end();) {
			auto& d = it->second;
			if (!d.ref.get()) {
				it = nights.erase(it);
				continue;
			}
			const bool night = NightFor(it->first);  // this painting's own hours
			if (d.showing != (night ? 1 : 0)) {
				Show(d, night);
			}
			++it;
		}
	}
}
