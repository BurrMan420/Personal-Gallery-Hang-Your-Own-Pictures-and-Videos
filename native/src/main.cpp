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

// Personal Gallery native helper:
//  - picture library (drop images and videos into Data\PersonalGallery\Pictures, converted at runtime)
//  - runtime picture/filter swap on hung paintings, living portraits (video + sound)
//  - smooth placement (snap to surfaces) and the build-mode menu (ImGui overlay)
// Exposed to Papyrus as the global script PGNative.

#include <vector>

#include "Apply.h"
#include "Catalog.h"
#include "Extras.h"
#include "Look.h"
#include "Magic.h"
#include "Pictures.h"
#include "Place.h"
#include "Player.h"
#include "UI.h"

namespace
{
	// --- Papyrus: placement ----------------------------------------------------------------

	void PlaceBegin(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, bool snap, float scale) { Place::Begin(ref, snap, scale); }
	void PlaceSetRef(RE::StaticFunctionTag*, RE::TESObjectREFR* ref) { Place::SetRef(ref); }
	void PlaceSetScale(RE::StaticFunctionTag*, float scale) { Place::SetScale(scale); }

	// [x, y, z, angleX, angleY, angleZ (degrees), scale]
	std::vector<float> PlaceEnd(RE::StaticFunctionTag*)
	{
		const auto t = Place::End();
		return { t.begin(), t.end() };
	}

	std::vector<float> SnapTarget(RE::StaticFunctionTag*, RE::TESObjectREFR* painting, float maxDist)
	{
		const auto t = Place::SnapTarget(painting, maxDist, painting ? painting->GetScale() : 1.0f);
		return { t.begin(), t.end() };
	}

	// --- Papyrus: picture library ------------------------------------------------------------

	std::int32_t GetPictureCount(RE::StaticFunctionTag*) { return Pictures::AvailableCount(); }
	std::int32_t ValidPicture(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Valid(i); }
	std::int32_t NextPicture(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Next(i, 1); }
	RE::BSFixedString GetPictureId(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Id(i); }
	std::vector<std::int32_t> ScanPictures(RE::StaticFunctionTag*)
	{
		const auto r = Pictures::Rescan();
		return { r.begin(), r.end() };
	}
	RE::BSFixedString GetPictureKey(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Key(i); }
	RE::BSFixedString GetPictureName(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Name(i); }
	std::int32_t GetPictureAspect(RE::StaticFunctionTag*, std::int32_t i) { return Pictures::Aspect(i); }
	std::int32_t FindPicture(RE::StaticFunctionTag*, RE::BSFixedString key, RE::BSFixedString id) { return Pictures::Find(key.c_str(), id.c_str()); }
	void ShowMissing(RE::StaticFunctionTag*, RE::TESObjectREFR* ref) { Apply::ShowMissing(ref); }

	// --- Papyrus: magic ------------------------------------------------------------------------

	void SetMagic(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, std::int32_t magic, std::int32_t style) { Magic::Set(ref, magic, style); }

	// --- Papyrus: the rest of a painting's look (Look::Option order) --------------------------

	void SetLook(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, std::vector<std::int32_t> options, RE::BSFixedString title)
	{
		Look::Set(ref, { options.begin(), options.end() }, title.c_str());
	}

	// slideshow: the next picture (mode: 1 in order, 2 shuffle, 3 favourites, 4 same album)
	std::int32_t NextSlidePicture(RE::StaticFunctionTag*, std::int32_t pic, std::int32_t mode) { return Pictures::NextSlide(pic, mode); }

	// every hung painting, so a painting being placed can line up with them
	void SetOtherPaintings(RE::StaticFunctionTag*, std::vector<RE::TESForm*> refs)
	{
		std::vector<RE::TESObjectREFR*> list;
		for (auto* f : refs) {
			list.push_back(f ? f->As<RE::TESObjectREFR>() : nullptr);
		}
		Place::SetOthers(list);
	}

	void ApplyPicture(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, std::int32_t pic, std::int32_t filter, std::int32_t tone)
	{
		// tone: the grade in the low 4 bits, the player's colour adjustments above (PGControlScript ToneCode)
		const std::int32_t grade = std::clamp<std::int32_t>(tone & 15, 0, static_cast<std::int32_t>(Catalog::kTones.size()) - 1);
		Apply::Set(ref, pic, std::clamp<std::int32_t>(filter, 0, static_cast<std::int32_t>(Catalog::kFilters.size()) - 1),
			(std::max<std::int32_t>(tone, 0) & ~15) | grade);
	}

	// --- Papyrus: living portraits ------------------------------------------------------------

	bool IsVideo(RE::StaticFunctionTag*, std::int32_t pic) { return Pictures::IsVideo(pic); }

	void SetLive(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, std::int32_t pic, std::int32_t filter, std::int32_t tone, std::int32_t mode)
	{
		Player::SetLive(ref, pic, filter, tone, mode);
	}

	void StopLive(RE::StaticFunctionTag*, RE::TESObjectREFR* ref) { Player::Stop(ref); }
	std::int32_t ToggleAwake(RE::StaticFunctionTag*, RE::TESObjectREFR* ref) { return Player::ToggleAwake(ref); }
	void SetVideoSettings(RE::StaticFunctionTag*, float volume, std::int32_t maxPlaying, std::int32_t quality, std::int32_t fps)
	{
		Player::SetSettings(volume, maxPlaying, quality, fps);
	}
	void SetTextureDetail(RE::StaticFunctionTag*, bool high) { Pictures::SetDetail(high); }

	// --- Papyrus: build-mode menu --------------------------------------------------------------

	void BuildModeOpen(RE::StaticFunctionTag*, std::vector<std::int32_t> sel, bool menu, std::int32_t menuKey, bool isNew, RE::BSFixedString title,
		RE::BSFixedString stashName)
	{
		std::array<int, UI::kTabCount> s{};
		for (size_t i = 0; i < s.size() && i < sel.size(); ++i) {
			s[i] = sel[i];
		}
		UI::Open(s, menu, menuKey, isNew, title.c_str(), stashName.c_str());
	}

	void BuildModeSet(RE::StaticFunctionTag*, std::int32_t tab, std::int32_t index) { UI::Set(tab, index); }
	void BuildModeClose(RE::StaticFunctionTag*) { UI::Close(); }
	bool IsMenuOpen(RE::StaticFunctionTag*) { return UI::MenuOpen(); }
	bool IsBuilding(RE::StaticFunctionTag*) { return UI::IsOpen(); }

	// --- Papyrus: hidden stash and day / night ------------------------------------------------
	void SwingPainting(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, bool open, std::int32_t style) { Extras::Swing(ref, open, style); }
	void SafeDoor(RE::StaticFunctionTag*, RE::TESObjectREFR* safe, bool open) { Extras::SafeDoor(safe, open); }
	void PlaceLockHere(RE::StaticFunctionTag*) { Place::LockHere(); }
	void SetMenuKey(RE::StaticFunctionTag*, std::int32_t key) { UI::SetMenuKey(key); }
	void CancelOpening(RE::StaticFunctionTag*) { UI::CancelOpening(); }
	void SetPreviewLight(RE::StaticFunctionTag*, RE::TESObjectREFR* light) { Place::SetPreviewLight(light); }
	void Browse(RE::StaticFunctionTag*, bool on) { UI::Browse(on); }
	bool IsBrowsing(RE::StaticFunctionTag*) { return UI::IsBrowsing(); }
	void SetUndoList(RE::StaticFunctionTag*, std::vector<RE::BSFixedString> items)
	{
		std::vector<std::string> out;
		for (const auto& i : items) {
			out.emplace_back(i.c_str());
		}
		UI::SetUndoList(out);
	}
	void SetSlidePicks(RE::StaticFunctionTag*, std::vector<RE::BSFixedString> ids)
	{
		std::vector<std::string> out;
		for (const auto& id : ids) {
			out.emplace_back(id.c_str());
		}
		UI::SetSlidePicks(out);
	}
	void LinkLight(RE::StaticFunctionTag*, RE::TESObjectREFR* painting, RE::TESObjectREFR* light) { Extras::LinkLight(painting, light); }
	void SetStashName(RE::StaticFunctionTag*, RE::TESObjectREFR* painting, RE::BSFixedString name) { Extras::SetStashName(painting, name.c_str()); }
	// the painting the crosshair rests on, within reach (paintings carry no activation prompt)
	RE::TESObjectREFR* PaintingInView(RE::StaticFunctionTag*, float maxDist)
	{
		return Place::PickPainting(maxDist > 0 ? maxDist : 600.0f);
	}

	std::vector<float> StashSpot(RE::StaticFunctionTag*, RE::TESObjectREFR* painting)
	{
		const auto t = Extras::StashSpot(painting);
		return { t.begin(), t.end() };
	}
	void SetDayNight(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, std::int32_t day, std::int32_t night, std::int32_t filter,
		std::int32_t tone, std::int32_t life)
	{
		Extras::SetDayNight(ref, day, night, filter, tone, life);
	}
	std::vector<std::int32_t> GetStyle(RE::StaticFunctionTag*, std::int32_t index)
	{
		const auto v = UI::GetStyle(index);
		return { v.begin(), v.end() };
	}

	std::vector<std::int32_t> GetDefaultStyle(RE::StaticFunctionTag*)
	{
		const auto v = UI::GetDefaultStyle();
		return { v.begin(), v.end() };
	}

	void SetHotkey(RE::StaticFunctionTag*, std::int32_t action, std::int32_t key) { UI::SetHotkey(action, key); }

	// A painting's size, without the scripts' own limit of 10: kept on the reference (so it is saved and comes back
	// when its 3D reloads) and put on what is drawn now.
	void SetRefScale(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, float scale)
	{
		if (!ref) {
			return;
		}
		scale = std::clamp(scale, 0.01f, 600.0f);
		ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(scale * 100.0f);
		ref->AddChange(1u << 4);  // "scale changed": without it the game does not write the size into the save
		SKSE::GetTaskInterface()->AddTask([handle = ref->CreateRefHandle(), scale]() {
			auto  r = handle.get();
			auto* root = r ? r->Get3D() : nullptr;
			if (root) {
				root->local.scale = scale;
				RE::NiUpdateData update{};
				root->Update(update);
			}
		});
	}
	void SetShadows(RE::StaticFunctionTag*, bool on) { Look::SetShadows(on); }
	std::int32_t GetCacheMB(RE::StaticFunctionTag*) { return Pictures::CacheMB(); }
	void ClearCache(RE::StaticFunctionTag*) { Pictures::ClearCache(); }

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
	{
		constexpr auto cls = "PGNative";
		vm->RegisterFunction("PlaceBegin", cls, PlaceBegin);
		vm->RegisterFunction("PlaceSetRef", cls, PlaceSetRef);
		vm->RegisterFunction("PlaceSetScale", cls, PlaceSetScale);
		vm->RegisterFunction("PlaceEnd", cls, PlaceEnd);
		vm->RegisterFunction("SnapTarget", cls, SnapTarget);
		vm->RegisterFunction("GetPictureCount", cls, GetPictureCount);
		vm->RegisterFunction("GetPictureKey", cls, GetPictureKey);
		vm->RegisterFunction("GetPictureName", cls, GetPictureName);
		vm->RegisterFunction("GetPictureAspect", cls, GetPictureAspect);
		vm->RegisterFunction("FindPicture", cls, FindPicture);
		vm->RegisterFunction("ValidPicture", cls, ValidPicture);
		vm->RegisterFunction("NextPicture", cls, NextPicture);
		vm->RegisterFunction("GetPictureId", cls, GetPictureId);
		vm->RegisterFunction("ScanPictures", cls, ScanPictures);
		vm->RegisterFunction("ShowMissing", cls, ShowMissing);
		vm->RegisterFunction("SetMagic", cls, SetMagic);
		vm->RegisterFunction("SetLook", cls, SetLook);
		vm->RegisterFunction("NextSlidePicture", cls, NextSlidePicture);
		vm->RegisterFunction("SetOtherPaintings", cls, SetOtherPaintings);
		vm->RegisterFunction("ApplyPicture", cls, ApplyPicture);
		vm->RegisterFunction("IsVideo", cls, IsVideo);
		vm->RegisterFunction("SetLive", cls, SetLive);
		vm->RegisterFunction("StopLive", cls, StopLive);
		vm->RegisterFunction("ToggleAwake", cls, ToggleAwake);
		vm->RegisterFunction("SetVideoSettings", cls, SetVideoSettings);
		vm->RegisterFunction("SetTextureDetail", cls, SetTextureDetail);
		vm->RegisterFunction("BuildModeOpen", cls, BuildModeOpen);
		vm->RegisterFunction("BuildModeSet", cls, BuildModeSet);
		vm->RegisterFunction("BuildModeClose", cls, BuildModeClose);
		vm->RegisterFunction("IsMenuOpen", cls, IsMenuOpen);
		vm->RegisterFunction("IsBuilding", cls, IsBuilding);
		vm->RegisterFunction("SwingPainting", cls, SwingPainting);
		vm->RegisterFunction("StashSpot", cls, StashSpot);
		vm->RegisterFunction("PlaceLockHere", cls, PlaceLockHere);
		vm->RegisterFunction("SetMenuKey", cls, SetMenuKey);
		vm->RegisterFunction("CancelOpening", cls, CancelOpening);
		vm->RegisterFunction("SetPreviewLight", cls, SetPreviewLight);
		vm->RegisterFunction("Browse", cls, Browse);
		vm->RegisterFunction("IsBrowsing", cls, IsBrowsing);
		vm->RegisterFunction("SetUndoList", cls, SetUndoList);
		vm->RegisterFunction("SetSlidePicks", cls, SetSlidePicks);
		vm->RegisterFunction("LinkLight", cls, LinkLight);
		vm->RegisterFunction("SafeDoor", cls, SafeDoor);
		vm->RegisterFunction("SetStashName", cls, SetStashName);
		vm->RegisterFunction("PaintingInView", cls, PaintingInView);
		vm->RegisterFunction("SetDayNight", cls, SetDayNight);
		vm->RegisterFunction("GetStyle", cls, GetStyle);
		vm->RegisterFunction("GetDefaultStyle", cls, GetDefaultStyle);
		vm->RegisterFunction("SetHotkey", cls, SetHotkey);
		vm->RegisterFunction("SetRefScale", cls, SetRefScale);
		vm->RegisterFunction("SetShadows", cls, SetShadows);
		vm->RegisterFunction("GetCacheMB", cls, GetCacheMB);
		vm->RegisterFunction("ClearCache", cls, ClearCache);
		return true;
	}

	// What the gallery needs and can't find, said once in plain words when a game is loaded (instead of failing quietly).
	std::string missingParts;

	void CheckRequirements()
	{
		if (!GetModuleHandleW(L"PapyrusUtil.dll")) {
			missingParts += "\n- PapyrusUtil SE (the gallery's records)";
		}
		if (auto* data = RE::TESDataHandler::GetSingleton()) {
			if (!data->LookupModByName("PersonalGallery.esp")) {
				missingParts += "\n- PersonalGallery.esp is not enabled";
			}
			if (!data->LookupModByName("SkyUI_SE.esp")) {
				missingParts += "\n- SkyUI (the mod settings menu)";
			}
		}
	}

	void TellMissing()
	{
		if (!missingParts.empty()) {
			RE::DebugMessageBox(("Personal Gallery is missing:\n" + missingParts + "\n\nInstall what is listed and start the game again.").c_str());
			missingParts.clear();  // once
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* msg)
	{
		if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
			TellMissing();
		}
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			CheckRequirements();
			Pictures::Init();
			Apply::Init();
			Player::Init();
			UI::Install();
		} else if (msg->type == SKSE::MessagingInterface::kPreLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
			UI::Close();  // a load mid-placement: let go of the painting and give the controls back
			Place::End();
			Player::StopAll();  // and no video keeps sounding into the loading screen
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SKSE::GetPapyrusInterface()->Register(RegisterFunctions);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
