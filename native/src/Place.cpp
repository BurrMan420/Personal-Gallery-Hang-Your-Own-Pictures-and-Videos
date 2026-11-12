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

#include "Dev.h"
#include "Place.h"

#include "Apply.h"
#include "Extras.h"
#include "Look.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <set>


namespace Place
{
	namespace
	{
		constexpr float kRadToDeg = 57.29578f;
		constexpr float kDegToRad = 0.01745329f;
		constexpr float kSkip = 2.0f;       // step past things the snap ray should ignore
		constexpr float kMaxPush = 30.0f;   // a bump deeper than this is furniture in front, not the wall
		constexpr float kProbe = 40.0f;     // depth probes start this far in front of the painting's back
		constexpr float kMaxScale = 100.0f;  // the wheel's largest size: as big as a house front, if that is wanted

		// keys (DirectInput scan codes)
		constexpr std::uint32_t kInsert = 210, kDelete = 211, kPgUp = 201, kPgDn = 209, kComma = 51, kPeriod = 52,
								kLBracket = 26, kRBracket = 27, kMinus = 12, kEquals = 13, kLShift = 42, kRShift = 54,
								kLCtrl = 29, kRCtrl = 157, kLeft = 203, kRight = 205, kUp = 200, kDown = 208;

		std::mutex              lock;
		RE::ObjectRefHandle     ref;
		bool                    active{ false };
		bool                    snap{ true };
		bool                    locked{ false };  // snapped spot held still; arrows nudge it
		float                   scale{ 1.0f };
		float                   distance{ 150.0f };  // free: from the camera
		float                   height{ 0.0f };      // free: raised; wall: slid up
		float                   gap{ 0.0f };         // snapped: pulled out from the surface
		bool                    stash{ false };      // a hidden stash: stand off the wall so the safe fits behind
		float                   minScale{ 0.1f };    // a hidden stash: no smaller than what hides the safe
		float                   floorGap{ -1.0f };   // on a wall: centre above the floor (for the HUD)
		RE::NiPoint3            boundsLo, boundsHi;
		float                   turn{ 0.0f };        // degrees added to the facing (free, floor, ceiling)
		float                   tilt{ 0.0f };        // degrees, angle X: leans the top out or in
		float                   spin{ 0.0f };        // degrees, angle Y: turns it on the wall like a clock hand
		float                   wallTurn{ 0.0f };    // degrees: on a wall, swung left / right off its facing
		RE::NiPoint3            pos;
		RE::NiPoint3            angles;  // degrees, as the game stores them (X, Y, Z)
		RE::NiPoint3            smoothedNormal;
		int                     wheel{ kSize };
		bool                    grid{ false };   // G: steps of 15 degrees, sizes in quarters, positions on a 5-unit grid
		bool                    align{ true };   // T: snap level with / in line with nearby paintings
		bool                    aligned{ false };
		RE::NiPoint3            guideA, guideB;  // the line drawn while aligned
		std::vector<RE::ObjectRefHandle> others;  // every other painting (for alignment)
		int                     lastSurface{ 0 };
		RE::NiPoint3            shownPos;  // eased towards pos / angles / scale, so every move glides
		RE::NiPoint3            shownAngles;
		float                   shownScale{ 1.0f };
		bool                    shownValid{ false };
		std::set<std::uint32_t> held;
		std::chrono::steady_clock::time_point last;

		// snapped spot (kept while locked)
		struct Anchor
		{
			bool         ok{ false };
			RE::NiPoint3 pos;
			RE::NiPoint3 normal;
			int          surface{ 0 };  // 1 wall, 2 floor, 3 ceiling
		} anchor;

		struct Hit
		{
			bool         ok{ false };
			RE::NiPoint3 pos;
			RE::NiPoint3 normal;
			float        dist{ 0 };
			RE::TESObjectREFR* ref{ nullptr };
		};

		// Havok ray from `from` along `dir`; ignores the player, other actors and `ignore`.
		Hit Cast(RE::NiPoint3 from, const RE::NiPoint3& dir, float maxDist, RE::TESObjectREFR* ignore)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return {};
			}
			RE::CFilter filter{};
			player->GetCollisionFilterInfo(filter);  // keeps the player's own system group out of the hits
			filter.SetCollisionLayer(RE::COL_LAYER::kLOS);
			const float havok = RE::bhkWorld::GetWorldScale();
			float       travelled = 0.0f;
			for (int attempt = 0; attempt < 8 && travelled < maxDist; ++attempt) {
				const RE::NiPoint3 end = from + dir * (maxDist - travelled);
				RE::bhkPickData    pick{};
				pick.rayInput.from = RE::hkVector4(from * havok);
				pick.rayInput.to = RE::hkVector4(end * havok);
				pick.rayInput.filterInfo = filter;
				{
					RE::BSReadLockGuard guard(world->worldLock);
					world->PickObject(pick);
				}
				if (!pick.rayOutput.HasHit()) {
					return {};
				}
				const RE::NiPoint3 hitPos = from + (end - from) * pick.rayOutput.hitFraction;
				const float        step = (hitPos - from).Length();
				auto* hitRef = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
				if (hitRef && (hitRef == ignore || hitRef == player || hitRef->Is(RE::FormType::ActorCharacter))) {
					travelled += step + kSkip;
					from = hitPos + dir * kSkip;
					continue;
				}
				float n[4];
				_mm_storeu_ps(n, pick.rayOutput.normal.quad);
				RE::NiPoint3 normal{ n[0], n[1], n[2] };
				normal.Unitize();
				return { true, hitPos, normal, travelled + step, hitRef };
			}
			return {};
		}

		// While the gallery panel covers the left of the screen, aim a little to the right so the
		// painting being chosen for sits in the open space beside it. Eased in and out.
		float aimShift{ 0.0f };
		float aimTarget{ 0.0f };

		// the showcase while the gallery panel is open
		bool  showcase{ false };
		bool  lightPage{ false };  // the Light page is open: no showcase light, a hung painting stays on its wall
		float showX{ 0.36f }, showW{ 0.64f }, showAspect{ 16.0f / 9.0f };
		float showBlend{ 0.0f };  // 0 placing .. 1 showcase, eased
		RE::ObjectRefHandle previewLight;  // lights the showcase
		float turnShow{ 0.0f };   // degrees the showcase turns while showing how the stash opens
		float slideShow{ 0.0f };  // 0..1 while it shows a stash that slides: drawn smaller, starting off-centre
		RE::NiPoint3 slideFrom;   // where that slide starts, in widths / heights of the painting

		bool CameraRay(RE::NiPoint3& a_from, RE::NiPoint3& a_dir)
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->cameraRoot) {
				return false;
			}
			const auto& xf = camera->cameraRoot->world;
			a_from = xf.translate;
			const RE::NiPoint3 right{ xf.rotate.entry[0][0], xf.rotate.entry[1][0], xf.rotate.entry[2][0] };
			a_dir = RE::NiPoint3{ xf.rotate.entry[0][1], xf.rotate.entry[1][1], xf.rotate.entry[2][1] } + right * aimShift;
			a_dir.Unitize();
			return true;
		}

		RE::NiPoint3 Horizontal(RE::NiPoint3 v)
		{
			v.z = 0;
			const float l = v.Length();
			return l > 1e-4f ? v / l : RE::NiPoint3{ 0, -1, 0 };
		}

		// angle Z (degrees) that makes the picture's front (local -Y) face `f`
		float Facing(const RE::NiPoint3& f) { return std::atan2(-f.x, -f.y) * kRadToDeg; }

		// The rotation the game itself builds from a reference's angles, so what you see while placing is
		// exactly what you get once it is placed.
		RE::NiMatrix3 Rotation(const RE::NiPoint3& deg)
		{
			RE::NiMatrix3 m;
			m.EulerAnglesToAxesZXY(deg.x * kDegToRad, deg.y * kDegToRad, deg.z * kDegToRad);
			return m;
		}

		bool Fine() { return held.count(kLCtrl) || held.count(kRCtrl); }

		float Wrap(float deg)
		{
			deg = std::fmod(deg + 180.0f, 360.0f);
			return (deg < 0 ? deg + 360.0f : deg) - 180.0f;
		}

		// On a wall: the grid, then alignment with nearby paintings on the same wall (level with one,
		// or straight above / below one). Sets `aligned` and the guide line.
		void SnapOnWall(RE::NiPoint3& c, const RE::NiPoint3& n, RE::TESObjectREFR* self)
		{
			aligned = false;
			const RE::NiPoint3 along{ n.y, -n.x, 0 };  // horizontal, along the wall
			if (grid) {
				const float a = c.Dot(along);
				c += along * (std::round(a / 5.0f) * 5.0f - a);
				c.z = std::round(c.z / 5.0f) * 5.0f;
			}
			if (!align) {
				return;
			}
			constexpr float kMagnet = 8.0f;
			float           best = kMagnet;
			const float     facing = Facing(n);
			for (const auto& h : others) {
				auto o = h.get();
				if (!o || o.get() == self || !o->Get3D() || o->IsDisabled()) {
					continue;
				}
				const RE::NiPoint3 p = o->GetPosition();
				const RE::NiPoint3 d = p - c;
				if (std::abs(Wrap(o->GetAngleZ() * kRadToDeg - facing)) > 10.0f || std::abs(d.Dot(n)) > 25.0f || std::abs(d.Dot(along)) > 900.0f ||
					std::abs(d.z) > 600.0f) {
					continue;  // not on this wall, or far away
				}
				if (std::abs(d.z) < best) {  // level with it
					best = std::abs(d.z);
					c.z = p.z;
					aligned = true;
					guideA = c;
					guideB = { p.x, p.y, c.z };
				}
				if (const float side = d.Dot(along); std::abs(side) < best) {  // straight above or below it
					best = std::abs(side);
					c += along * side;
					aligned = true;
					guideA = c;
					guideB = p;
				}
			}
		}

		float Rate(std::uint32_t up, std::uint32_t down)
		{
			return (held.count(up) ? 1.0f : 0.0f) - (held.count(down) ? 1.0f : 0.0f);
		}

		// Classify the surface under the crosshair. On walls the facing comes from probes left and right of
		// the hit, so rough stone or log walls give the plane of the whole painting, not of one stone.
		Anchor FindAnchor(RE::TESObjectREFR* a_painting, float a_halfWidth)
		{
			RE::NiPoint3 from, dir;
			if (!CameraRay(from, dir)) {
				return {};
			}
			const Hit hit = Cast(from, dir, 3000.0f, a_painting);
			if (a_painting && !Extras::HangsOnWall(a_painting) && (!hit.ok || hit.normal.z <= 0.7f || hit.dist > 450.0f)) {
				// a display that stands (projector, basin, orb, easel): always on the floor in front of you - looking
				// across a room the aim finds a far wall or nothing, and it floated
				RE::NiPoint3 over = hit.ok && hit.dist <= 450.0f ? hit.pos + Horizontal(hit.normal) * (a_halfWidth + 12.0f)
				                                                 : from + dir * std::min(hit.ok ? hit.dist : 250.0f, 250.0f);
				const Hit floor = Cast(over + RE::NiPoint3{ 0, 0, 40.0f }, { 0, 0, -1 }, 2500.0f, a_painting);
				if (floor.ok && floor.normal.z > 0.6f) {
					// its picture stands behind it: keep that clear of a wall or pillar behind (it showed through)
					RE::NiPoint3 at = floor.pos;
					const RE::NiPoint3 away = Horizontal(dir);
					const float  need = a_halfWidth * 0.5f + 25.0f;
					const Hit    back = Cast(at + RE::NiPoint3{ 0, 0, 50.0f }, away, need, a_painting);
					if (back.ok) {
						at -= away * (need - back.dist);
					}
					return { true, at, floor.normal, 2 };
				}
			}
			if (!hit.ok) {
				return {};
			}
			Anchor a{ true, hit.pos, hit.normal, 1 };
			if (hit.normal.z > 0.7f) {
				a.surface = 2;
				return a;
			}
			if (hit.normal.z < -0.7f) {
				a.surface = 3;
				return a;
			}
			RE::NiPoint3       n = Horizontal(hit.normal);
			const RE::NiPoint3 side{ -n.y, n.x, 0 };
			const float        reach = std::clamp(a_halfWidth, 8.0f, 200.0f);
			const Hit          l = Cast(hit.pos + n * kProbe + side * reach, -n, kProbe * 3, a_painting);
			const Hit          r = Cast(hit.pos + n * kProbe - side * reach, -n, kProbe * 3, a_painting);
			if (l.ok && r.ok) {
				const RE::NiPoint3 along = Horizontal(l.pos - r.pos);
				RE::NiPoint3       fitted{ along.y, -along.x, 0 };
				if (fitted.Dot(n) < 0) {
					fitted = -fitted;
				}
				if (fitted.Dot(n) > 0.85f) {  // within ~30 degrees: the same wall
					n = fitted;
				}
			}
			a.normal = n;
			return a;
		}

		// How far the surface pokes out in front of the painting's back, sampled over its back face (3 x 3).
		float Bulge(RE::TESObjectREFR* a_painting, const RE::NiPoint3& a_center, const RE::NiMatrix3& a_rot,
			const RE::NiPoint3& a_normal, const RE::NiPoint3& a_lo, const RE::NiPoint3& a_hi)
		{
			float push = 0.0f;
			for (const float fx : { a_lo.x * 0.92f, 0.0f, a_hi.x * 0.92f }) {
				for (const float fz : { a_lo.z * 0.92f, 0.0f, a_hi.z * 0.92f }) {
					const RE::NiPoint3 corner = a_center + a_rot * (RE::NiPoint3{ fx, a_hi.y, fz } * scale);
					const Hit          h = Cast(corner + a_normal * kProbe, -a_normal, kProbe + kMaxPush, a_painting);
					if (h.ok) {
						const float in = kProbe - h.dist;  // > 0: the wall is in front of this corner
						if (in > push && in < kMaxPush) {
							push = in;
						}
					}
				}
			}
			return push;
		}
	}

	std::array<float, 7> SnapTarget(RE::TESObjectREFR* a_painting, float a_maxDist, float a_scale)
	{
		std::array<float, 7> result{};
		RE::NiPoint3         from, dir;
		if (!a_painting || !CameraRay(from, dir)) {
			return result;
		}
		const Hit hit = Cast(from, dir, a_maxDist > 0 ? a_maxDist : 3000.0f, a_painting);
		if (!hit.ok) {
			return result;
		}
		const RE::NiPoint3 lo = a_painting->GetBoundMin();
		const RE::NiPoint3 hi = a_painting->GetBoundMax();
		RE::NiPoint3       p = hit.pos;
		float              angleZ = Facing(-Horizontal(dir)), surface = 1.0f;
		if (hit.normal.z > 0.7f) {
			surface = 2.0f;
			p.z += -lo.z * a_scale + 0.2f;
		} else if (hit.normal.z < -0.7f) {
			surface = 3.0f;
			p.z -= hi.z * a_scale + 0.2f;
		} else {
			const RE::NiPoint3 n = Horizontal(hit.normal);
			angleZ = Facing(n);
			p += n * (hi.y * a_scale + 0.3f);
		}
		result = { 1.0f, p.x, p.y, p.z, 0.0f, angleZ, surface };
		return result;
	}

	void Begin(RE::TESObjectREFR* a_ref, bool a_snap, float a_scale)
	{
		// this painting's own stash (asked before taking our lock: Extras asks us things under its lock)
		const bool  hasStash = a_ref && Extras::StashOpening(a_ref) > 0;
		const float stashMin = hasStash ? Extras::StashMinScale(a_ref->GetBoundMin(), a_ref->GetBoundMax()) : 0.1f;
		std::lock_guard l(lock);
		stash = hasStash;
		minScale = stashMin;
		ref = a_ref ? a_ref->CreateRefHandle() : RE::ObjectRefHandle{};
		active = a_ref != nullptr;
		snap = a_snap;
		locked = false;
		anchor = {};
		smoothedNormal = {};
		scale = a_scale;
		height = 0;
		gap = 0;
		turn = 0;
		wallTurn = 0;
		tilt = a_ref ? a_ref->GetAngleX() * kRadToDeg : 0.0f;
		spin = a_ref ? a_ref->GetAngleY() * kRadToDeg : 0.0f;
		distance = 150.0f;
		if (auto* camera = RE::PlayerCamera::GetSingleton(); a_ref && camera && camera->cameraRoot) {
			distance = std::clamp((a_ref->GetPosition() - camera->cameraRoot->world.translate).Length(), 60.0f, 1500.0f);
		}
		held.clear();
		shownValid = false;
		lastSurface = 0;
		last = std::chrono::steady_clock::now();
	}

	void SetRef(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		ref = a_ref ? a_ref->CreateRefHandle() : RE::ObjectRefHandle{};
		if (a_ref && shownValid) {
			// load its 3D where the old one is shown, not where it was spawned (no flash at your feet)
			a_ref->data.location = shownPos;
			a_ref->data.angle = shownAngles * kDegToRad;
		}
	}

	void SetScale(float a_scale)
	{
		std::lock_guard l(lock);
		scale = a_scale;
		if (!snap) {
			distance = std::max(distance, 60.0f * a_scale);  // keep big paintings out of your face
		}
	}

	bool ToggleSnap()
	{
		std::lock_guard l(lock);
		snap = !snap;
		locked = false;
		height = 0;
		turn = 0;
		return snap;  // the caller updates the UI (lock order: UI never waits on Place while holding its own)
	}

	void LockHere()
	{
		std::lock_guard l(lock);
		auto r = ref.get();
		if (!r) {
			return;
		}
		// the wall behind it: straight back from its centre, along the way it faces
		const auto    ang = r->GetAngle();
		RE::NiMatrix3 rot;
		rot.EulerAnglesToAxesZXY(ang.x, ang.y, ang.z);
		const RE::NiPoint3 into{ rot.entry[0][1], rot.entry[1][1], rot.entry[2][1] };
		const RE::NiPoint3 lo = r->GetBoundMin(), hi = r->GetBoundMax();
		const RE::NiPoint3 centre = r->GetPosition() + rot * (RE::NiPoint3{ (lo.x + hi.x) / 2, 0, (lo.z + hi.z) / 2 } * r->GetScale());
		const Hit          h = Cast(centre - into * 10.0f, into, 60.0f + hi.y * r->GetScale(), r.get());
		if (!h.ok || std::abs(h.normal.z) > 0.5f) {
			return;  // not on a wall: start free, as before
		}
		anchor = { true, h.pos + RE::NiPoint3{ 0, 0, r->GetPosition().z - centre.z }, Horizontal(h.normal), 1 };
		anchor.pos.z = h.pos.z - (centre.z - r->GetPosition().z);
		smoothedNormal = anchor.normal;
		wallTurn = std::clamp(Wrap(ang.z * kRadToDeg - Facing(anchor.normal)), -80.0f, 80.0f);  // keep how it was turned
		// how far it stood off the wall (the wheel's "Away from wall"): keep it, so it doesn't jump back
		const float off = (r->GetPosition() - h.pos).Dot(anchor.normal) - hi.y * scale - 0.3f - (stash ? Extras::SafePush(lo, hi, scale) : 0.0f);
		gap = std::clamp(off, 0.0f, 150.0f);
		snap = true;
		locked = true;
		lastSurface = 1;
	}

	RE::TESObjectREFR* Current()
	{
		std::lock_guard l(lock);
		return active ? ref.get().get() : nullptr;
	}

	bool ToggleLock()
	{
		std::lock_guard l(lock);
		if (!snap || !anchor.ok) {
			locked = false;
			return false;
		}
		locked = !locked;
		return locked;
	}

	void ResetAll()
	{
		std::lock_guard l(lock);
		tilt = 0;
		spin = 0;
		turn = 0;
		wallTurn = 0;
		gap = 0;
		height = 0;
		scale = std::max(1.0f, minScale);
		distance = 150.0f;
	}

	void Straighten()
	{
		std::lock_guard l(lock);
		tilt = 0;
		spin = 0;
		turn = 0;
		wallTurn = 0;
		gap = 0;
		height = 0;
	}

	int ResetWheel()
	{
		std::lock_guard l(lock);
		switch (wheel) {
		case kSize:
			scale = std::max(1.0f, minScale);
			break;
		case kRotate:
			spin = 0;
			break;
		case kTurn:
			(snap && lastSurface == 1 ? wallTurn : turn) = 0;
			break;
		case kTilt:
			tilt = 0;
			break;
		default:
			gap = 0;
			if (!(snap && lastSurface != 0)) {
				distance = 150.0f;
			}
			break;
		}
		return wheel;
	}

	int NextWheel()
	{
		std::lock_guard l(lock);
		auto       r = ref.get();
		const bool standing = r && !Extras::HangsOnWall(r.get());
		do {  // a standing display: only size, turn and lift mean anything
			wheel = (wheel + 1) % kWheelCount;
		} while (standing && (wheel == kRotate || wheel == kTilt));
		return wheel;
	}

	void Scroll(float a_notches)
	{
		std::lock_guard l(lock);
		if (!active || a_notches == 0) {
			return;
		}
		const bool fine = Fine();
		switch (wheel) {
		case kSize:
			if (grid) {  // quarter steps: 0.25, 0.5, 0.75 ...
				scale = std::clamp(std::round(scale / 0.25f + a_notches) * 0.25f, 0.25f, kMaxScale);
			} else {
				scale = std::clamp(scale * std::pow(fine ? 1.015f : 1.07f, a_notches), 0.1f, kMaxScale);
			}
			scale = std::max(scale, minScale);
			break;
		case kRotate:
			spin = grid ? Wrap(std::round(spin / 15.0f + a_notches) * 15.0f) : Wrap(spin + a_notches * (fine ? 1.0f : 5.0f));
			break;
		case kTurn:
			if (snap && lastSurface == 1) {
				wallTurn = std::clamp(grid ? std::round(wallTurn / 15.0f + a_notches) * 15.0f : wallTurn + a_notches * (fine ? 1.0f : 5.0f),
					-80.0f, 80.0f);
			} else {
				turn = grid ? Wrap(std::round(turn / 15.0f + a_notches) * 15.0f) : Wrap(turn + a_notches * (fine ? 1.0f : 5.0f));
			}
			break;
		case kTilt:
			tilt = grid ? std::clamp(std::round(tilt / 15.0f + a_notches) * 15.0f, -90.0f, 90.0f)
			            : std::clamp(tilt + a_notches * (fine ? 1.0f : 5.0f), -90.0f, 90.0f);
			break;
		default:
			if (snap && lastSurface != 0) {
				gap = std::clamp(gap + a_notches * (fine ? 0.25f : 1.5f), -50.0f, 1000.0f);
			} else {
				distance = std::clamp(distance + a_notches * (fine ? 3.0f : 20.0f) * std::max(1.0f, scale), 30.0f, 10000.0f);
			}
			break;
		}
	}

	bool ToggleGrid()
	{
		std::lock_guard l(lock);
		grid = !grid;
		return grid;
	}

	bool ToggleAlign()
	{
		std::lock_guard l(lock);
		align = !align;
		aligned = false;
		return align;
	}

	void SetOthers(const std::vector<RE::TESObjectREFR*>& a_refs)
	{
		std::lock_guard l(lock);
		others.clear();
		for (auto* r : a_refs) {
			if (r) {
				others.push_back(r->CreateRefHandle());
			}
		}
	}

	Info GetInfo()
	{
		std::lock_guard l(lock);
		Info i;
		i.active = active;
		i.snap = snap;
		i.locked = locked;
		i.surface = snap ? lastSurface : 0;
		i.wheel = wheel;
		i.scale = scale;
		i.rotate = spin;
		i.turn = snap && lastSurface == 1 ? wallTurn : turn;
		i.tilt = tilt;
		i.depth = snap && lastSurface != 0 ? gap : distance;
		i.grid = grid;
		i.align = align;
		i.aligned = aligned && snap && lastSurface == 1;
		i.guideA = guideA;
		i.guideB = guideB;
		i.width = (boundsHi.x - boundsLo.x) * scale;
		i.height = (boundsHi.z - boundsLo.z) * scale;
		i.floorGap = snap && lastSurface == 1 ? floorGap : -1.0f;
		if (auto r = ref.get()) {
			i.standing = !Extras::HangsOnWall(r.get());
		}
		return i;
	}

	void SetPreviewLight(RE::TESObjectREFR* a_light)
	{
		std::lock_guard l(lock);
		previewLight = a_light ? a_light->CreateRefHandle() : RE::ObjectRefHandle{};
	}

	void SetSideView(bool a_on, float a_centreX, float a_width, float a_aspect, bool a_lightPage)
	{
		std::lock_guard l(lock);
		showcase = a_on;
		lightPage = a_lightPage;
		showX = a_centreX;
		showW = std::clamp(a_width, 0.2f, 1.0f);
		showAspect = std::max(0.5f, a_aspect);
	}

	void SetStashFor(RE::TESObjectREFR* a_ref, bool a_on)
	{
		std::lock_guard l(lock);
		if (a_ref && ref.get().get() == a_ref) {
			stash = a_on;
			// a painting too small to hide even the smallest safe grows just enough to
			minScale = a_on ? Extras::StashMinScale(a_ref->GetBoundMin(), a_ref->GetBoundMax()) : 0.1f;
			scale = std::max(scale, minScale);
		}
	}

	RE::TESObjectREFR* Pick(float a_maxDist)
	{
		RE::NiPoint3 from, dir;
		if (!CameraRay(from, dir)) {
			return nullptr;
		}
		const Hit h = Cast(from, dir, a_maxDist, nullptr);
		return h.ok ? h.ref : nullptr;
	}

	RE::TESObjectREFR* PickPainting(float a_maxDist)
	{
		RE::NiPoint3 from, dir;
		if (!CameraRay(from, dir)) {
			return nullptr;
		}
		const Hit h = Cast(from, dir, a_maxDist, nullptr);
		if (h.ok && Extras::IsPainting(h.ref)) {
			return h.ref;
		}
		// its box, in its own space: the nearest painting the ray passes through, closer than any wall
		const float        wall = h.ok ? h.dist : a_maxDist;
		RE::TESObjectREFR* best = nullptr;
		float              bestT = wall + 1.0f;
		for (const auto& handle : Apply::Paintings()) {
			auto r = handle.get();
			if (!r || r->IsDisabled() || !r->Get3D()) {
				continue;
			}
			const auto    ang = r->GetAngle();
			RE::NiMatrix3 rot;
			rot.EulerAnglesToAxesZXY(ang.x, ang.y, ang.z);
			const float        sc = std::max(0.01f, r->GetScale());
			const RE::NiMatrix3 inv = rot.Transpose();
			const RE::NiPoint3 o = inv * (from - r->GetPosition()) / sc;
			const RE::NiPoint3 d = inv * dir;
			const RE::NiPoint3 lo = r->GetBoundMin(), hi = r->GetBoundMax();
			float              t0 = 0.0f, t1 = a_maxDist / sc;
			bool               hit = true;
			for (int k = 0; k < 3 && hit; ++k) {
				const float ok = k == 0 ? o.x : k == 1 ? o.y : o.z, dk = k == 0 ? d.x : k == 1 ? d.y : d.z;
				const float lk = k == 0 ? lo.x : k == 1 ? lo.y : lo.z, hk = k == 0 ? hi.x : k == 1 ? hi.y : hi.z;
				if (std::abs(dk) < 1e-6f) {
					hit = ok >= lk - 2.0f && ok <= hk + 2.0f;
					continue;
				}
				float a = (lk - 2.0f - ok) / dk, b = (hk + 2.0f - ok) / dk;
				if (a > b) {
					std::swap(a, b);
				}
				t0 = std::max(t0, a);
				t1 = std::min(t1, b);
				hit = t0 <= t1;
			}
			const float t = t0 * sc;
			if (hit && t < bestT && t <= wall + 15.0f) {
				bestT = t;
				best = r.get();
			}
		}
		return best;
	}

	bool Active()
	{
		std::lock_guard l(lock);
		return active;
	}

	std::array<float, 7> End()
	{
		std::lock_guard l(lock);
		active = false;
		locked = false;
		held.clear();
		if (!shownValid) {
			Dev::Log("odd", "placing ended before the painting's 3D appeared - left where it was");
		}
		if (!shownValid) {  // its 3D never came (no frame placed it): scale 0, so the scripts leave it where it is
			return {};
		}
		return { pos.x, pos.y, pos.z, angles.x, angles.y, angles.z, scale };
	}

	void KeyState(std::uint32_t a_key, bool a_down)
	{
		std::lock_guard l(lock);
		if (a_down) {
			held.insert(a_key);
		} else {
			held.erase(a_key);
		}
	}

	void Frame()
	{
		std::lock_guard l(lock);
		if (!active) {
			return;
		}
		auto  r = ref.get();
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* root = r ? r->Get3D() : nullptr;
		if (!root || !camera || !camera->cameraRoot) {
			return;
		}
		const auto  now = std::chrono::steady_clock::now();
		const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
		last = now;
		const RE::NiPoint3 lo = r->GetBoundMin();
		const RE::NiPoint3 hi = r->GetBoundMax();

		// held keys, per second; Shift is faster, Ctrl is fine control, big paintings move faster
		const float speed = (held.count(kLShift) || held.count(kRShift) ? 3.0f : 1.0f) * (Fine() ? 0.2f : 1.0f);
		const float big = std::max(1.0f, scale);
		const float push = Rate(kInsert, kDelete);  // Insert farther / pull out, Delete nearer / push in
		const float spinKeys = Rate(kPeriod, kComma);
		height += Rate(kPgUp, kPgDn) * 100.0f * speed * big * dt;
		tilt = std::clamp(tilt + Rate(kRBracket, kLBracket) * 45.0f * speed * dt, -90.0f, 90.0f);
		if (const float grow = Rate(kEquals, kMinus); grow != 0) {
			scale = std::clamp(scale * std::pow(1.8f, grow * speed * dt), minScale, kMaxScale);
		}

		const auto&        cam = camera->cameraRoot->world;
		aimShift += (aimTarget - aimShift) * std::min(1.0f, dt * 8.0f);
		RE::NiPoint3 eye, fwd;
		CameraRay(eye, fwd);
		const RE::NiPoint3 toPlayer = -Horizontal(fwd);

		bool placed = false;
		int  surface = 0;
		const bool standing = !Extras::HangsOnWall(r.get());
		if (standing) {  // a projector, basin, orb or easel: always on the floor, never too small or huge
			scale = std::clamp(scale, 0.1f, kMaxScale);
		}
		if (snap || standing) {
			if (!locked) {
				anchor = FindAnchor(r.get(), std::max(-lo.x, hi.x) * scale);
			} else if (const float sx = Rate(kRight, kLeft), sy = Rate(kUp, kDown); sx != 0 || sy != 0) {
				// locked: arrows slide it along the surface
				const RE::NiPoint3 n = anchor.surface == 1 ? anchor.normal : RE::NiPoint3{ 0, 0, 1 };
				RE::NiPoint3       right = anchor.surface == 1 ? RE::NiPoint3{ n.y, -n.x, 0 } : RE::NiPoint3{ -toPlayer.y, toPlayer.x, 0 };
				RE::NiPoint3       up = anchor.surface == 1 ? RE::NiPoint3{ 0, 0, 1 } : -toPlayer;
				anchor.pos += (right * sx + up * sy) * 40.0f * speed * big * dt;
			}
			if (anchor.ok) {
				surface = anchor.surface;
				gap = std::clamp(gap + push * 20.0f * speed * dt, -50.0f, 1000.0f);
				if (surface == 1) {
					// wall: faces out of it; , and . spin it on the wall, Page Up/Down slide it up and down.
					// The facing eases between nearby angles so a rough wall doesn't make it shiver.
					if (smoothedNormal.SqrLength() < 0.5f || smoothedNormal.Dot(anchor.normal) < 0.9f) {
						smoothedNormal = anchor.normal;
					} else {
						smoothedNormal = Horizontal(smoothedNormal + (anchor.normal - smoothedNormal) * std::min(1.0f, dt * 15.0f));
					}
					const RE::NiPoint3 n = smoothedNormal;
					spin += spinKeys * 60.0f * speed * dt;
					angles = { tilt, spin, Facing(n) + wallTurn };
					const RE::NiMatrix3 rot = Rotation(angles);
					RE::NiPoint3        center = anchor.pos + n * (hi.y * scale) + RE::NiPoint3{ 0, 0, height };
					SnapOnWall(center, n, r.get());
					// turned: its far edge swings towards the wall - stand off by as much
					center += n * (std::max(-lo.x, hi.x) * scale * std::abs(std::sin(wallTurn * kDegToRad)));
					if (stash) {  // room for the wall safe behind the painting
						center += n * Extras::SafePush(lo, hi, scale);
					}
					center += n * (Bulge(r.get(), center, rot, n, lo, hi) + gap + 0.3f);
					pos = center;
					// how high it hangs: straight down from its centre to the floor (shown on the HUD)
					const Hit down = Cast(center, { 0, 0, -1 }, 2000.0f, r.get());
					floorGap = down.ok ? down.dist : -1.0f;
				} else {
					// floor or ceiling: stands up (or hangs) facing you; , and . turn it
					turn += spinKeys * 90.0f * speed * dt;
					// a display that stands (projector, basin, orb, easel) stays upright: only turned, never leaned over
					const bool upright = !Extras::HangsOnWall(r.get());
					angles = upright ? RE::NiPoint3{ 0, 0, Facing(toPlayer) + turn } : RE::NiPoint3{ tilt, spin, Facing(toPlayer) + turn };
					pos = anchor.pos;
					if (grid) {
						pos.x = std::round(pos.x / 5.0f) * 5.0f;
						pos.y = std::round(pos.y / 5.0f) * 5.0f;
					}
					pos.z += surface == 2 ? -lo.z * scale + 0.2f + gap : -(hi.z * scale + 0.2f + gap);
					smoothedNormal = {};
				}
				placed = true;
			}
		}
		if (!placed) {
			locked = false;
			smoothedNormal = {};
			distance = std::clamp(distance + push * 220.0f * speed * big * dt, 30.0f, 10000.0f);
			turn += spinKeys * 90.0f * speed * dt;
			// never so near for its size that it fills the view or swallows you (a few wheel turns used to do that)
			distance = std::max(distance, std::max(hi.x - lo.x, hi.z - lo.z) * scale * 0.9f + 40.0f);
			pos = cam.translate + fwd * distance;
			pos.z += height;
			angles = { tilt, spin, Facing(toPlayer) + turn };
		}

		lastSurface = surface;
		boundsLo = lo;
		boundsHi = hi;

		// ease what is shown towards the target (about 40 ms), jump on big moves
		const float ease = 1.0f - std::exp(-dt * 26.0f);
		if (!shownValid || (pos - shownPos).Length() > 400.0f) {
			shownPos = pos;
			shownAngles = angles;
			shownScale = scale;
			shownValid = true;
		} else {
			shownPos += (pos - shownPos) * ease;
			shownAngles.x += Wrap(angles.x - shownAngles.x) * ease;
			shownAngles.y += Wrap(angles.y - shownAngles.y) * ease;
			shownAngles.z += Wrap(angles.z - shownAngles.z) * ease;
			shownScale += (scale - shownScale) * ease;
		}

		RE::NiTransform shown;
		shown.translate = shownPos;
		shown.rotate = Rotation(shownAngles);
		shown.scale = shownScale;

		// the gallery panel is open: a showcase in the open space beside it, facing you, sized to fill it -
		// and never behind a wall (if one is nearer, it comes closer and smaller, looking just the same)
		// (a painting already hung, on the Light page: it stays on its wall, so its light is judged where it will be)
		// (and while a stash opening is shown: on the wall where it hangs or will hang, not held up in front of you)
		const bool showNow = showcase && !(lightPage && locked) && !Extras::Previewing();
		showBlend += ((showNow ? 1.0f : 0.0f) - showBlend) * std::min(1.0f, dt * 10.0f);
		if (showBlend > 0.01f) {
			auto*       pc = RE::PlayerCamera::GetSingleton();
			const float fov = std::clamp(pc ? pc->GetRuntimeData2().worldFOV : 75.0f, 40.0f, 130.0f) * kDegToRad;
			const float th = std::tan(fov * 0.5f), tv = th / showAspect;
			const RE::NiPoint3 right{ cam.rotate.entry[0][0], cam.rotate.entry[1][0], cam.rotate.entry[2][0] };
			const RE::NiPoint3 ahead{ cam.rotate.entry[0][1], cam.rotate.entry[1][1], cam.rotate.entry[2][1] };
			const bool         standing = !Extras::HangsOnWall(r.get());
			const float        w = std::max({ 1.0f, (hi.x - lo.x) * scale, standing ? (hi.y - lo.y) * scale : 0.0f }),
			                   h = std::max(1.0f, (hi.z - lo.z) * scale) * (standing ? 1.25f : 1.0f);
			const float        want = std::max({ w / (2.0f * th * showW * 0.8f), h / (2.0f * tv * 0.72f), 40.0f });
			RE::NiPoint3       dir = ahead + right * (showX * th);
			const float        len = dir.Length();
			dir.Unitize();
			float      dist = want * len;
			// look along the centre and toward each corner: a post or wall at the side must not cut through it
			Hit wall = Cast(cam.translate, dir, dist + (hi.y - lo.y) * scale + 10.0f, r.get());
			{
				const RE::NiPoint3 upv{ cam.rotate.entry[0][2], cam.rotate.entry[1][2], cam.rotate.entry[2][2] };
				const float        hw = w * 0.55f, hh = h * 0.55f;
				for (const float sx : { -1.0f, 1.0f }) {
					for (const float sz : { -1.0f, 1.0f }) {
						RE::NiPoint3 to = dir * dist + right * (sx * hw) + upv * (sz * hh);
						const float  len2 = to.Length();
						to.Unitize();
						const Hit c = Cast(cam.translate, to, len2 + 10.0f, r.get());
						if (c.ok && (!wall.ok || c.dist * (dist / std::max(1.0f, len2)) < wall.dist)) {
							wall = c;
							wall.dist = c.dist * (dist / std::max(1.0f, len2));  // as a distance along the centre
						}
					}
				}
			}
			float      shrink = 1.0f;
			if (wall.ok && wall.dist < dist + (hi.y - lo.y) * scale + 10.0f) {
				// nearer and smaller - but never nearer than 60: closer than ~15 the game doesn't draw at all, and a
				// deep frame that close vanished (in a tight spot it may touch the wall instead, but it shows)
				const float fit = std::max(60.0f, wall.dist - (hi.y - lo.y) * scale - 10.0f);
				shrink = fit / dist;  // nearer and smaller: the same size on screen
				dist = fit;
			}
			RE::NiTransform show;
			show.translate = cam.translate + dir * dist;
			show.scale = scale * shrink;
			// a little turned while it shows how its stash opens, so the swing can be seen. A stash that slides is
			// not turned: it is drawn at half size and starts to one side, so the whole slide stays in the open
			// space (at full size it slid behind the panel or off the screen and could not be judged)
			const int  opening = Extras::PreviewStyle();
			const bool slides = opening >= 4;
			if (slides) {
				slideFrom = opening == 4 ? RE::NiPoint3{ 0.5f, 0, 0 } : opening == 5 ? RE::NiPoint3{ -0.5f, 0, 0 } : RE::NiPoint3{ 0, 0, -0.5f };
			}
			slideShow += ((slides ? 1.0f : 0.0f) - slideShow) * std::min(1.0f, dt * 6.0f);
			show.scale *= 1.0f - 0.5f * slideShow;
			turnShow += ((Extras::Previewing() && !slides ? 28.0f : 0.0f) - turnShow) * std::min(1.0f, dt * 6.0f);
			const RE::NiMatrix3 turnM = Rotation({ 0, 0, turnShow });
			if (Extras::HangsOnWall(r.get())) {
				show.rotate = cam.rotate * turnM;  // its face towards you (the model's front is -Y, the camera looks along +Y)
			} else {
				// stands on a table or the floor (easel, orb, basin, projector): upright, turned to you, a little
				// below your eyes so you look down into it - as you would at one standing in the room
				show.rotate = Rotation({ 0, 0, Facing(-Horizontal(ahead)) + turnShow });
				// on the ground in front of you wherever there is ground (it used to hang in the air at chest height,
				// which looked wrong for a basin or a projector); a little further off if it would otherwise sit
				// below the bottom of the view
				const RE::NiPoint3 flat = Horizontal(dir);
				bool               grounded = false;
				for (int pass = 0; pass < 2 && !grounded; ++pass) {
					const RE::NiPoint3 over = cam.translate + flat * dist;
					const Hit          floor = Cast(over + RE::NiPoint3{ 0, 0, 30.0f }, { 0, 0, -1 }, 450.0f, r.get());
					if (!floor.ok || floor.normal.z < 0.5f) {
						break;
					}
					const float z = floor.pos.z - lo.z * show.scale + 0.2f;
					const float need = (cam.translate.z - z) / (tv * 0.62f);  // far enough that its middle is in view
					// Only when the ground there is in view as it is. (It used to be pushed up to three times further off
					// to reach ground that was: a small projector then stood far across the room, a speck behind
					// whatever else was in the way, and could not be judged at all.)
					if (need > dist * 1.15f) {
						break;
					}
					show.translate = { over.x, over.y, z };
					grounded = true;
				}
				if (!grounded) {  // no ground under it (a bridge, a ledge): held in front of you, as before
					show.translate.z -= dist * 0.32f;
				}
			}
			if (slideShow > 0.001f) {
				show.translate += show.rotate * (RE::NiPoint3{ slideFrom.x * (hi.x - lo.x), 0, slideFrom.z * (hi.z - lo.z) } * (show.scale * slideShow));
			}
			const float t = showBlend * showBlend * (3 - 2 * showBlend);
			shown.translate = shown.translate + (show.translate - shown.translate) * t;
			shown.scale = shown.scale + (show.scale - shown.scale) * t;
			if (t > 0.5f) {
				shown.rotate = show.rotate;
			}
		}
		root->local = shown;
		RE::NiUpdateData update{};
		root->Update(update);
		if (int side, height, out; Look::LightSpot(r.get(), side, height, out)) {  // its light comes along
			Extras::FollowLight(r.get(), root->world, side, height, out);
		}

		// the preview light: just above and behind your eyes while the panel is open, far below the world otherwise
		if (auto lightRef = previewLight.get(); lightRef && !lightRef->IsDisabled()) {
			if (auto* l3 = lightRef->Get3D()) {
				// in front of the showcase, between you and the painting, and reaching all of it: at night a big
				// painting stood beyond a small light at your shoulder and showed almost black
				const float        size = std::max((hi.x - lo.x), (hi.z - lo.z)) * shown.scale;
				RE::NiPoint3       toEye = cam.translate - shown.translate;
				const float        dist = toEye.Length();
				toEye.Unitize();
				const float        out = std::clamp(size * 0.8f, 50.0f, std::max(50.0f, dist * 0.8f));
				const bool         lit = showBlend > 0.5f && !lightPage;
				l3->local.translate = lit ? shown.translate + toEye * out + RE::NiPoint3{ 0, 0, size * 0.15f }
				                          : cam.translate - RE::NiPoint3{ 0, 0, 20000.0f };
				if (auto* x = lightRef->extraList.GetByType<RE::ExtraLight>(); x && x->lightData && x->lightData->light) {
					auto&       rd = x->lightData->light->GetLightRuntimeData();
					const float reach = std::max(200.0f, out + size * 1.2f);
					rd.radius = { reach, reach, reach };
					rd.fade = 0.8f;
				}
				RE::NiUpdateData lu{};
				l3->Update(lu);
			}
		}
	}
}
