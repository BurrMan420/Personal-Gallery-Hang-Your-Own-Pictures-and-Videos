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

#include "Apply.h"

#include <map>
#include <mutex>

#include "Catalog.h"
#include "Pictures.h"
#include "Player.h"

namespace Apply
{
	namespace
	{
		struct Wanted
		{
			RE::ObjectRefHandle ref;
			int                 pic;
			int                 filter;
			int                 tone;
			bool                fallbackShown{ false };
			std::string         shown;  // the texture put on last (a draft is not put on again each time something is ready)
		};

		// crop / zoom / flip per painting, as a texture-coordinate scale and offset on the canvas
		struct View
		{
			float su{ 1 }, sv{ 1 }, ou{ 0 }, ov{ 0 };
			float gloss{ -1 }, opacity{ 1 };  // gloss < 0: as modelled
			float glow{ 0.5f };               // a projected picture's own light (its glow map's strength)
			int   extra{ 0 };
			RE::NiAVObject* ownOn{ nullptr };  // the 3D whose canvas has its own material (set with the picture)
		};

		std::mutex                       lock;
		std::map<RE::FormID, Wanted>     pending;
		// what each painting should show, so it can be put back by itself when its 3D reloads
		struct Known
		{
			Wanted          w;
			RE::NiAVObject* root{ nullptr };
			bool            missing{ false };
		};
		std::map<RE::FormID, Known>      known;
		int                              frameCount{ 0 };

		std::mutex                       viewLock;
		std::map<RE::FormID, View>       views;

		void ApplyView(RE::TESObjectREFR* ref, RE::BSShaderMaterial* material, RE::BSGeometry* geo = nullptr)
		{
			View v;
			{
				std::lock_guard l(viewLock);
				if (auto it = views.find(ref->GetFormID()); it != views.end()) {
					v = it->second;
				}
			}
			material->texCoordScale[0] = { v.su, v.sv };
			material->texCoordOffset[0] = { v.ou, v.ov };
			auto* lit = static_cast<RE::BSLightingShaderMaterialBase*>(material);
			if (v.gloss >= 0) {  // matte canvas .. wet varnish
				lit->specularColorScale = v.gloss * 1.2f;
				lit->specularPower = 8.0f + v.gloss * 110.0f;
			}
			// a projected picture glows with its own colours: the glow map is the picture, as strong as chosen
			if (material->GetFeature() == RE::BSShaderMaterial::Feature::kGlowMap) {
				auto* glowing = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material);
				glowing->glowTexture = lit->diffuseTexture;
				if (auto* prop = geo ? netimmerse_cast<RE::BSLightingShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get()) : nullptr) {
					prop->emissiveMult = std::clamp(v.glow, 0.0f, 1.5f);
				}
			}
		}

		// has this painting's canvas (in this 3D) been given its own material yet? Before that it may be shared
		// with every painting of the same model, and changing it would change them all.
		bool OwnsMaterial(RE::TESObjectREFR* ref, RE::NiAVObject* root)
		{
			std::lock_guard l(viewLock);
			const auto it = views.find(ref->GetFormID());
			return it != views.end() && it->second.ownOn == root;
		}

		int ExtraOf(RE::FormID id)
		{
			std::lock_guard l(viewLock);
			const auto it = views.find(id);
			return it != views.end() ? it->second.extra : 0;
		}

		std::string NormalPath(int filter)
		{
			return "textures\\PersonalGallery\\Filters\\" + std::string(Catalog::kFilterKeys[filter]) + "_n.dds";
		}

		// Swap the diffuse/normal textures of the "Canvas" shape (same technique as po3's texture swaps).
		bool SetCanvasTextures(RE::TESObjectREFR* ref, const std::string& diffuse, const std::string& normal)
		{
			return SetShapeTextures(ref, "Canvas", diffuse, normal);
		}
	}

	bool SetShapeTextures(RE::TESObjectREFR* ref, std::string_view a_shape, const std::string& diffuse, const std::string& normal)
	{
		auto* root = ref ? ref->Get3D() : nullptr;
		if (!root) {
			return false;
		}
		const bool canvas = a_shape == "Canvas";
		bool       done = false;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geo) {
			const std::string_view name = geo->name.c_str();
			if (name.size() < a_shape.size() || _strnicmp(name.data(), a_shape.data(), a_shape.size()) != 0) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = lighting ? static_cast<RE::BSLightingShaderMaterialBase*>(lighting->material) : nullptr;
			if (!material) {
				return canvas ? RE::BSVisit::BSVisitControl::kContinue : RE::BSVisit::BSVisitControl::kStop;  // every Canvas shape (the basin has two)
			}
			auto* txst = RE::BSShaderTextureSet::Create();
			if (auto* old = material->textureSet.get()) {  // keep its other maps (the Enchanted frame's glowing runes)
				for (std::uint32_t i = RE::BSTextureSet::Texture::kEnvironmentMask; i < RE::BSTextureSet::Texture::kUsedTotal; ++i) {
					const auto slot = static_cast<RE::BSTextureSet::Texture>(i);
					if (const char* path = old->GetTexturePath(slot); path && *path) {
						txst->SetTexturePath(slot, path);
					}
				}
			}
			txst->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, diffuse.c_str());
			txst->SetTexturePath(RE::BSTextureSet::Texture::kNormal, normal.c_str());
			if (canvas && material->GetFeature() == RE::BSShaderMaterial::Feature::kGlowMap) {
				txst->SetTexturePath(RE::BSTextureSet::Texture::kGlowMap, diffuse.c_str());  // it glows as what it shows
			}
			if (auto* fresh = static_cast<RE::BSLightingShaderMaterialBase*>(material->Create())) {
				fresh->CopyMembers(material);
				fresh->ClearTextures();
				fresh->OnLoadTextureSet(0, txst);
				lighting->SetMaterial(fresh, true);
				lighting->SetupGeometry(geo);
				lighting->FinishSetupGeometry(geo);
				fresh->~BSLightingShaderMaterialBase();
				RE::free(fresh);
				if (canvas && lighting->material) {
					{
						std::lock_guard l(viewLock);
						views[ref->GetFormID()].ownOn = ref->Get3D();
					}
					ApplyView(ref, lighting->material, geo);
				}
				done = true;
			}
			return canvas ? RE::BSVisit::BSVisitControl::kContinue : RE::BSVisit::BSVisitControl::kStop;  // every Canvas shape (the basin has two)
		});
		return done;
	}

	namespace
	{

		// Main thread only. Returns true when nothing more is needed for this painting.
		bool TryApply(Wanted& w)
		{
			auto ref = w.ref.get();
			if (!ref || !ref->Get3D()) {
				return true;  // gone or unloaded - its OnLoad will ask again
			}
			if (Player::IsPlaying(ref->GetFormID())) {
				return true;  // a living portrait owns the canvas while it plays
			}
			std::string tex = Pictures::Texture(w.pic, w.filter, w.tone, ExtraOf(ref->GetFormID()));
			if (tex.empty()) {
				// show the plain picture (or keep the blank canvas) while this look is being made
				if (!w.fallbackShown && (w.filter != 0 || w.tone != 0) && !OwnsMaterial(ref.get(), ref->Get3D())) {  // only a bare canvas
					auto clear = Pictures::Texture(w.pic, 0, w.tone & 15);  // the plain grade while the tuned one is made
					if (clear.empty()) {
						clear = Pictures::Texture(w.pic, 0, 0);
					}
					if (!clear.empty()) {
						w.fallbackShown = SetCanvasTextures(ref.get(), clear, NormalPath(0));
					}
				}
				return false;
			}
			// the filter's brush relief comes with it from half strength up: below that the picture reads as itself
			if (tex != w.shown) {
				SetCanvasTextures(ref.get(), tex, NormalPath(Catalog::FilterStrength(ExtraOf(ref->GetFormID())) > 0.5f ? w.filter : 0));
				w.shown = tex;
			}
			// a quick draft (Pictures.cpp names them ..._d.dds) shows at once; the painting waits on for the full one
			return !tex.ends_with("_d.dds");
		}

		void ProcessPending()
		{
			std::lock_guard l(lock);
			for (auto it = pending.begin(); it != pending.end();) {
				it = TryApply(it->second) ? pending.erase(it) : std::next(it);
			}
		}
	}

	void Init()
	{
		Pictures::SetReadyCallback([] {
			SKSE::GetTaskInterface()->AddTask([] { ProcessPending(); });
		});
	}

	void ShowMissing(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return;
		}
		auto handle = a_ref->CreateRefHandle();
		{
			std::lock_guard l(lock);
			auto& k = known[a_ref->GetFormID()];
			k.w.ref = handle;
			k.root = a_ref->Get3D();
			k.missing = true;
		}
		SKSE::GetTaskInterface()->AddTask([handle]() {
			if (auto ref = handle.get()) {
				SetCanvasTextures(ref.get(), "textures\\PersonalGallery\\missing.dds", NormalPath(0));
			}
		});
	}

	void SetView(RE::TESObjectREFR* a_ref, float a_zoom, float a_panX, float a_panY, bool a_flip)
	{
		if (!a_ref) {
			return;
		}
		const float z = std::max(1.0f, a_zoom);
		const float half = 0.5f / z;
		const float cu = 0.5f + std::clamp(a_panX, -1.0f, 1.0f) * (0.5f - half);
		const float cv = 0.5f + std::clamp(a_panY, -1.0f, 1.0f) * (0.5f - half);
		View        v{ 1 / z, 1 / z, cu - half, cv - half };
		if (a_flip) {  // mirror left to right
			v.su = -v.su;
			v.ou = cu + half;
		}
		{
			std::lock_guard l(viewLock);
			auto& kept = views[a_ref->GetFormID()];
			v.gloss = kept.gloss;
			v.glow = kept.glow;
			v.opacity = kept.opacity;
			v.extra = kept.extra;
			v.ownOn = kept.ownOn;
			kept = v;
		}
		auto handle = a_ref->CreateRefHandle();
		SKSE::GetTaskInterface()->AddTask([handle]() {
			auto ref = handle.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root || !OwnsMaterial(ref.get(), root)) {
				return;  // applied with the picture when it loads (its own material is made then)
			}
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geo) {
				if (_strnicmp(geo->name.c_str(), "canvas", 6) != 0) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
				if (lighting && lighting->material) {
					ApplyView(ref.get(), lighting->material, geo);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
	}

	void SetSurface(RE::TESObjectREFR* a_ref, float a_gloss, float a_opacity, float a_glow)
	{
		if (!a_ref) {
			return;
		}
		{
			std::lock_guard l(viewLock);
			auto& v = views[a_ref->GetFormID()];
			v.gloss = a_gloss;
			v.glow = a_glow;
			v.opacity = std::clamp(a_opacity, 0.05f, 1.0f);
		}
		auto handle = a_ref->CreateRefHandle();
		SKSE::GetTaskInterface()->AddTask([handle]() {
			auto  ref = handle.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root || !OwnsMaterial(ref.get(), root)) {
				return;  // applied with the picture when it loads (its own material is made then)
			}
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geo) {
				if (_strnicmp(geo->name.c_str(), "canvas", 6) == 0) {
					auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(geo->GetGeometryRuntimeData().shaderProperty.get());
					if (lighting && lighting->material) {
						ApplyView(ref.get(), lighting->material, geo);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
	}

	std::vector<RE::ObjectRefHandle> Paintings()
	{
		std::lock_guard                  l(lock);
		std::vector<RE::ObjectRefHandle> out;
		out.reserve(known.size());
		for (const auto& [id, k] : known) {
			out.push_back(k.w.ref);
		}
		return out;
	}

	void SetExtra(RE::TESObjectREFR* a_ref, int a_extra)
	{
		if (!a_ref) {
			return;
		}
		{
			std::lock_guard l(viewLock);
			auto& v = views[a_ref->GetFormID()];
			if (v.extra == a_extra) {
				return;
			}
			v.extra = a_extra;
		}
		Wanted w;
		{
			std::lock_guard l(lock);
			const auto it = known.find(a_ref->GetFormID());
			if (it == known.end() || it->second.missing) {
				return;  // its picture comes with the extra when it is set
			}
			w = it->second.w;
		}
		Set(a_ref, w.pic, w.filter, w.tone);  // make it again with the new extra
	}

	void SetNow(RE::TESObjectREFR* a_ref, const std::string& a_diffuse)
	{
		SetCanvasTextures(a_ref, a_diffuse, NormalPath(0));
	}

	// Every frame: our paintings never fade in or out - a swapped or newly placed painting shows at once
	// (their fade-node root would otherwise ease it in over a second, which looked like a ghost)
	bool IsPending(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard l(lock);
		return a_ref && pending.count(a_ref->GetFormID()) > 0;
	}

	void KeepOpaque()
	{
		std::lock_guard l(lock);
		for (const auto& [id, k] : known) {
			auto ref = k.w.ref.get();
			auto* root = ref && !ref->IsDisabled() ? ref->Get3D() : nullptr;
			if (auto* fade = root ? root->AsFadeNode() : nullptr) {
				fade->GetRuntimeData().currentFade = 1.0f;
			}
		}
	}

	void Frame()
	{
		if (++frameCount % 15) {  // a few times a second is plenty
			return;
		}
		std::vector<std::pair<RE::FormID, Known>> reload;
		{
			std::lock_guard l(lock);
			for (auto it = known.begin(); it != known.end();) {
				auto ref = it->second.w.ref.get();
				if (!ref) {
					it = known.erase(it);
					continue;
				}
				auto* root = ref->Get3D();
				if (root && root != it->second.root) {  // new 3D: the cell was left and entered again
					it->second.root = root;
					reload.emplace_back(*it);
				}
				++it;
			}
		}
		for (auto& [id, k] : reload) {
			if (k.missing) {
				if (auto ref = k.w.ref.get()) {
					ShowMissing(ref.get());
				}
				continue;
			}
			Wanted w = k.w;
			w.fallbackShown = false;
			SKSE::GetTaskInterface()->AddTask([w, id]() mutable {
				std::lock_guard l(lock);
				if (!TryApply(w)) {
					pending[id] = w;
				}
			});
		}
	}

	void Set(RE::TESObjectREFR* a_ref, int a_pic, int a_filter, int a_tone)
	{
		if (!a_ref || a_pic < 0) {
			return;
		}
		if (Pictures::IsVideo(a_pic)) {
			a_filter = 0;  // a video is never painted over: its still shows as the video will play
		}
		Wanted           w{ a_ref->CreateRefHandle(), a_pic, a_filter, a_tone };
		const RE::FormID id = a_ref->GetFormID();
		{
			std::lock_guard l(lock);
			known[id] = { w, a_ref->Get3D(), false };
		}
		SKSE::GetTaskInterface()->AddTask([w, id]() mutable {  // scene graph changes belong on the main thread
			std::lock_guard l(lock);
			if (TryApply(w)) {
				pending.erase(id);
			} else {
				pending[id] = w;
			}
		});
	}
}
