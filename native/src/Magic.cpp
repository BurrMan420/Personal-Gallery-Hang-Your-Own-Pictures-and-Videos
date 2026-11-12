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

#include "Magic.h"

#include <chrono>
#include <cmath>
#include <array>
#include <map>
#include <mutex>
#include <vector>

#include "Catalog.h"

namespace Magic
{
	namespace
	{
		constexpr float kRange = 12000.0f;  // animate only paintings this close

		// glow pieces, in Catalog::kGlowMix column order
		constexpr std::array<std::string_view, 5> kPieces{ "magic_edge", "magic_aura", "magic_corners", "magic_sheen", "magic_veil" };

		struct Ring
		{
			RE::NiPointer<RE::BSGeometry> geo;
			RE::BSEffectShaderMaterial*   material{ nullptr };
			int                           piece{ 0 };
		};

		struct Entry
		{
			RE::ObjectRefHandle ref;
			int                 magic{ 0 };
			int                 style{ 0 };
			float               intensity{ 1.0f };
			Custom              custom;
			RE::NiAVObject*     root{ nullptr };  // the 3D the rings below belong to
			int                 shown{ -1 };      // Signature() the rings were set up for
			std::vector<Ring>   rings;
			float               seed{ 0 };
			float               on{ -1 };  // eases to 0 outside its hours (Catalog::kWhen); -1 until first set
		};

		std::mutex                  lock;
		std::map<RE::FormID, Entry> entries;
		const auto                  start = std::chrono::steady_clock::now();

		// Give this ring its own material, so tinting it leaves other paintings of the same model alone.
		RE::BSEffectShaderMaterial* OwnMaterial(RE::BSGeometry* geo)
		{
			auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
			auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->material) : nullptr;
			if (!mat) {
				return nullptr;
			}
			if (auto* fresh = static_cast<RE::BSEffectShaderMaterial*>(mat->Create())) {
				fresh->CopyMembers(mat);
				prop->SetMaterial(fresh, true);
				fresh->~BSEffectShaderMaterial();
				RE::free(fresh);
			}
			return static_cast<RE::BSEffectShaderMaterial*>(prop->material);
		}

		// how much of a piece shows: the glow style's own amount x the player's slider; a piece the style
		// leaves out comes in when its slider goes past 100 %
		float Mix(const Entry& e, int p)
		{
			const float own = Catalog::kGlowMix[e.style][p];
			if (p == 2 && e.custom.mix[p] >= 50.0f) {
				return std::max(own, 1.0f);  // corner gems asked for on any glow style
			}
			return own > 0 ? own * e.custom.mix[p] : std::max(0.0f, e.custom.mix[p] - 1.0f) * 0.8f;
		}

		int Signature(const Entry& e)
		{
			int on = 0;
			for (int p = 0; p < 5; ++p) {
				on |= (e.magic > 0 && Mix(e, p) > 0 ? 1 : 0) << p;
			}
			return e.magic * 100 + e.style + (on << 12) + (e.custom.sheenAngle << 18);
		}

		void Setup(Entry& e, RE::NiAVObject* root)
		{
			e.root = root;
			e.shown = Signature(e);
			e.rings.clear();
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
				const std::string      lowered = Catalog::Lower(g->name.c_str());
				const std::string_view name = lowered;
				for (int p = 0; p < static_cast<int>(kPieces.size()); ++p) {
					int angle = -1;  // the sheen comes at four angles: only the chosen one shows
					if (p == 3) {
						// "magic_sheen" sweeps left to right; "magic_sheen_<degrees>" the same band turned (every 15 degrees)
						if (name == "magic_sheen") {
							angle = 0;
						} else if (name.starts_with("magic_sheen_")) {
							angle = std::atoi(lowered.c_str() + 12) / 15;
						}
						if (angle < 0) {
							continue;
						}
					} else if (name != kPieces[p]) {
						continue;
					}
					const bool on = e.magic > 0 && Mix(e, p) > 0 && (p != 3 || angle == e.custom.sheenAngle);
					g->SetAppCulled(!on);
					if (on) {
						auto* m = OwnMaterial(g);
						if (m && (p == 3 || p == 4)) {  // the sheen and the veil sweep one way or the other, at the player's speed
							m->texCoordScale[0].x = std::abs(m->texCoordScale[0].x) * (e.custom.reverse ? -1.0f : 1.0f);
							if (auto* prop = g->GetGeometryRuntimeData().shaderProperty.get()) {
								for (auto* c = prop->GetControllers(); c; c = c->next.get()) {
									c->frequency = e.custom.sweepSpeed;  // the model's scroll controllers run at 1.0
								}
							}
						}
						e.rings.push_back({ RE::NiPointer<RE::BSGeometry>(g), m, p });
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// 0..1 over time for the player's chosen motion
		float Wave(int motion, float x)
		{
			switch (motion) {
			case 1:  // steady
				return 1.0f;
			case 3:  // pulse: a smooth swell, held briefly
				return std::pow(0.5f + 0.5f * std::sin(x * 2.6f), 3.0f);
			case 4:
				{  // heartbeat: two beats then rest
					const float ph = std::fmod(x * 0.9f, 1.0f);
					return std::min(1.0f, std::exp(-std::pow((ph - 0.1f) / 0.05f, 2.0f)) + 0.6f * std::exp(-std::pow((ph - 0.28f) / 0.05f, 2.0f)));
				}
			case 5:  // flicker
				return std::clamp(0.55f + 0.25f * std::sin(x * 7.1f) + 0.12f * std::sin(x * 13.7f + 1.3f) + 0.08f * std::sin(x * 23.3f + 2.1f), 0.0f, 1.0f);
			case 6:  // crackle
				return std::min(1.0f, 0.25f + std::pow(std::max(0.0f, std::sin(x * 9.7f) * std::sin(x * 3.1f + 0.7f)), 14.0f) * 2.0f);
			default:  // breathe
				return 0.5f + 0.5f * std::sin(x * 1.7f);
			}
		}

		int MotionOf(Catalog::MagicStyle s)
		{
			using S = Catalog::MagicStyle;
			return s == S::kFlicker ? 5 : s == S::kCrackle ? 6 : s == S::kPulse ? 4 : 2;
		}

		// 0..1-ish brightness over time for each style
		float Strength(Catalog::MagicStyle style, float speed, float t, float seed)
		{
			using S = Catalog::MagicStyle;
			const float x = t * speed + seed;
			switch (style) {
			case S::kFlicker:  // layered fast wobble, like a hearth
				return 0.78f + 0.12f * std::sin(x * 7.1f) + 0.07f * std::sin(x * 13.7f + 1.3f) + 0.05f * std::sin(x * 23.3f + 2.1f);
			case S::kCrackle:
				{
					const float base = 0.7f + 0.1f * std::sin(x * 2.3f);
					const float arc = std::pow(std::max(0.0f, std::sin(x * 9.7f) * std::sin(x * 3.1f + 0.7f)), 14.0f);
					return base + 1.4f * arc;
				}
			case S::kPulse:  // two beats then rest
				{
					const float ph = std::fmod(x * 0.9f, 1.0f);
					const float beat = std::exp(-std::pow((ph - 0.1f) / 0.05f, 2.0f)) + 0.6f * std::exp(-std::pow((ph - 0.28f) / 0.05f, 2.0f));
					return 0.72f + 0.45f * beat;
				}
			default:
				return 0.82f + 0.18f * std::sin(x * 1.7f);
			}
		}
	}

	void Set(RE::TESObjectREFR* a_ref, int a_magic, int a_style)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		auto& e = entries[a_ref->GetFormID()];
		e.ref = a_ref->CreateRefHandle();
		e.magic = std::clamp(a_magic, 0, static_cast<int>(Catalog::kMagic.size()) - 1);
		e.style = std::clamp(a_style, 0, static_cast<int>(Catalog::kGlowStyles.size()) - 1);
		e.seed = static_cast<float>(a_ref->GetFormID() % 997) * 0.37f;  // paintings don't breathe in step
		e.shown = -1;  // set up again on the next frame
	}

	void SetIntensity(RE::TESObjectREFR* a_ref, float a_scale)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		auto& e = entries[a_ref->GetFormID()];
		e.ref = a_ref->CreateRefHandle();
		e.intensity = a_scale;
	}

	void SetCustom(RE::TESObjectREFR* a_ref, const Custom& a_custom)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		auto& e = entries[a_ref->GetFormID()];
		e.ref = a_ref->CreateRefHandle();
		e.custom = a_custom;  // the next frame sets the pieces up again only if what shows has changed (Signature)
	}

	void Forget(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return;
		}
		std::lock_guard l(lock);
		entries.erase(a_ref->GetFormID());
	}

	void Frame()
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !camera->cameraRoot) {
			return;
		}
		const RE::NiPoint3 eye = camera->cameraRoot->world.translate;
		const float        t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

		std::lock_guard l(lock);
		for (auto it = entries.begin(); it != entries.end();) {
			auto& e = it->second;
			if (e.magic == 0 && e.shown == Signature(e)) {  // no magic and already hidden: nothing to do (the pieces load hidden)
				++it;
				continue;
			}
			auto  ref = e.ref.get();
			if (!ref) {
				it = entries.erase(it);
				continue;
			}
			auto* root = ref->Get3D();
			if (!root) {
				e.root = nullptr;
				e.rings.clear();
				++it;
				continue;
			}
			if (root != e.root || e.shown != Signature(e)) {
				if (root != e.root) {
					e.on = -1;  // just loaded: start at the right brightness for the hour
				}
				Setup(e, root);  // new 3D (cell reload, model swap) or a new choice
			}
			if (e.magic > 0 && (ref->GetPosition() - eye).Length() < kRange) {
				const auto& look = Catalog::kMagicLooks[e.magic];
				const auto& c = e.custom;
				float       k;
				if (c.motion == 0 && c.depth <= 0) {  // the magic's own motion, at the player's speed
					k = Strength(look.style, look.speed * c.speed, t, e.seed);
				} else {
					const float depth = c.depth > 0 ? c.depth : 0.35f;
					const float w = Wave(c.motion ? c.motion : MotionOf(look.style), (t * c.speed + e.seed) * std::max(0.3f, look.speed));
					k = (1.0f - depth) + depth * w * 1.25f;
				}
				// always, only at night or only by day (the painting's own night hours)
				{
					auto*       cal = RE::Calendar::GetSingleton();
					const bool  night = Catalog::InHours(cal ? cal->GetHour() : 12.0f, c.nightStart, c.nightEnd);
					const float want = c.when == 1 ? (night ? 1.0f : 0.0f) : c.when == 2 ? (night ? 0.0f : 1.0f) : 1.0f;
					e.on = e.on < 0 ? want : e.on + (want - e.on) * 0.04f;  // fades in and out; no flash when it loads
				}
				float cr = look.r, cg = look.g, cb = look.b;
				if (c.hue > 0) {
					Catalog::HueRGB(c.hue - 1.0f, c.sat, cr, cg, cb);
				}
				const float twinkle = 0.75f + 0.25f * std::sin(t * 2.3f + e.seed) * std::sin(t * 0.9f + e.seed * 2.0f);
				for (auto& r : e.rings) {
					if (!r.material) {
						continue;
					}
					float strength;
					switch (r.piece) {
					case 1:  // aura: steadier than the edge
						strength = look.aura * (0.6f + 0.4f * k);
						break;
					case 2:  // corner gems twinkle
						strength = look.edge * k * twinkle;
						break;
					case 4:  // veil: faint
						strength = look.edge * 0.55f * (0.7f + 0.3f * k);
						break;
					default:
						strength = look.edge * k;
						break;
					}
					r.material->baseColor = { cr, cg, cb, c.alpha };
					// (0.6: the glow was drawn too hot, and bloomed to white under an ENB)
					// the sheen past 100 % grows only a little: it lies over the picture, and at full strength it was a
					// solid bar that hid it
					const float own = r.piece == 3 && c.strength > 1.0f ? 1.0f + (c.strength - 1.0f) * 0.3f : c.strength;
					r.material->baseColorScale = 0.6f * strength * Mix(e, r.piece) * e.intensity * own * c.alpha * e.on;
				}
			}
			++it;
		}
	}
}
