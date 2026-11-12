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

#include "UI.h"

#include <random>

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <d3d11.h>
#include <DirectXTex.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

#include "Catalog.h"
#include "Dev.h"
#include "Pictures.h"
#include "Apply.h"
#include "Extras.h"
#include "Look.h"
#include "Magic.h"
#include "Place.h"
#include "Player.h"

namespace UI
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// event key for each choice (PG_Select strArg)
		constexpr std::array<std::string_view, kTabCount> kTabKeys{ "picture", "filter", "tone", "life", "magic", "display", "frame",
			"size", "light", "slideshow", "glow", "intensity", "framewidth", "framedepth", "mat", "glass", "age", "lamp", "plate",
			"zoom", "panx", "pany", "flip", "volume", "play", "quality", "slidetime", "lightlevel", "stashopen",
			"magichue", "magicsat", "magicstrength", "magicalpha", "motion", "speed", "depth", "mixedge", "mixaura", "mixcorners", "mixsheen", "mixveil", "sweep", "lighthue", "lightbright", "lightreach", "picbright", "piccontrast", "picgamma", "picsat", "picwarm",
			"picgloss", "picopacity", "tinthue", "tintstrength", "vignette", "sharpness", "sheenspeed", "lightside", "lightheight", "lightout", "lightflicker", "lightflickerspeed", "lightwhen", "magicwhen", "nightstart", "nightend", "sheenangle", "filterstrength", "stashamount", "stashspeed", "glassshine", "matwidth", "platefont", "beam", "vision", "glassthick", "framehue", "frametint", "platehue", "platesize", "projtilt", "projsize", "mathue", "mattint", "videofps", "projturn", "daynight", "nightpic" };

		// Pages of the gallery, in the order a painting is made: what it shows, how it hangs, how the picture looks,
		// what is around it, and extras. A page is a grid of cards (for one choice) and / or rows of options.
		enum Page
		{
			pPicture,
			pChange,
			pVideo,
			pDisplay,
			pFrame,
			pFinish,
			pStyle,
			pLight,
			pMagic,
			pStash,
			kPageCount
		};

		struct PageInfo
		{
			const char*                              name;
			const char*                              help;
			int                                      cards;  // choice shown as cards, -1 for none
			std::vector<std::pair<int, const char*>> rows;   // choices shown as rows of buttons or sliders
		};
		const std::array<PageInfo, kPageCount> kPages{ {
			{ "Picture", "Your pictures and videos. Right-click a card to make it a favourite.", kPicture, {} },
			{ "Changing picture", "Let the picture change by itself: another one at night, or a slideshow.", -1,
				{ { kSlideshow, "Order" }, { kSlideTime, "Change every" }, { kNightStart, "Night from" }, { kNightEnd, "Night until" } } },
			{ "Video", "How a video painting plays.", -1,
				{ { kLife, "Moves" }, { kVolume, "Volume" }, { kPlayMode, "Plays" }, { kQuality, "Quality" }, { kVideoFps, "Frame rate" } } },
			{ "Display", "How it is shown: framed, canvas, banner, tapestry, projector and more.", kDisplay,
				{ { kBeam, "Projector beam" }, { kVision, "Projected glow" }, { kProjSize, "Projection size" }, { kProjTilt, "Up / down" }, { kProjTurn, "Left / right" } } },
			{ "Frame", "Auto picks a frame to suit the magic. Shape it and colour it your way.", kFrame,
				{ { kFrameWidth, "Width" }, { kFrameDepth, "Depth" }, { kFrameHue, "Colour" }, { kFrameTint, "Colour strength" } } },
			{ "Finish", "The finishing touches: a mat, glass, age and a nameplate.", -1,
				{ { kMat, "Mat" }, { kMatWidth, "Mat width" }, { kMatHue, "Mat colour" }, { kMatTint, "Colour strength" }, { kGlass, "Glass" },
					{ kGlassShine, "Glass shine" }, { kAge, "Age" }, { kPlate, "Nameplate" },
					{ kPlateHue, "Plate colour" }, { kPlateSize, "Plate size" } } },
			{ "Style", "How the picture itself looks: painted in a style (still pictures), set in a colour mood, fine-tuned.", -1,
				{ { kFilterStrength, "Strength" }, { kPicBright, "Brightness" }, { kPicSat, "Saturation" }, { kPicContrast, "Contrast" },
					{ kTintHue, "Tint" }, { kTintStrength, "Tint strength" }, { kVignette, "Vignette" }, { kPicGloss, "Gloss" } } },
			{ "Light", "A light on the painting, so it reads in dark rooms. On this page the preview's own light is off.", -1,
				{ { kLamp, "Lamp on the frame" }, { kLightLevel, "Level" }, { kLightHue, "Colour" }, { kLightBright, "Strength" }, { kLightReach, "Reach" },
					{ kLightFlicker, "Flicker" }, { kLightWhen, "Shines" }, { kNightStart, "Night from" }, { kNightEnd, "Night until" } } },
			{ "Magic", "An enchanted glow - any colour, any motion, wherever you want it to shine.", -1,
				{ { kMagic, "Magic" }, { kGlow, "Where it glows" }, { kMixCorners, "Corner gems" }, { kMagicStrength, "Strength" }, { kMagicHue, "Colour" }, { kMagicSat, "Saturation" },
					{ kMagicMotion, "Motion" }, { kMagicSpeed, "Speed" }, { kSheenAngle, "Sweeps towards" }, { kMagicWhen, "Shines" },
					{ kNightStart, "Night from" }, { kNightEnd, "Night until" } } },
			{ "Hidden stash", "A wall safe behind the painting; the painting moves aside to open it.", -1,
				{ { kStashOpen, "Hidden stash" }, { kStashAmount, "Opens" }, { kStashSpeed, "Takes" } } },
		} };

		// the rail's groups
		struct Group
		{
			const char*      name;
			std::vector<int> pages;
		};
		const std::array<Group, 5> kGroups{ { { "PICTURE", { pPicture, pChange, pVideo } }, { "HANGING", { pDisplay, pFrame, pFinish } },
			{ "LOOK", { pStyle } }, { "ATMOSPHERE", { pLight, pMagic } }, { "EXTRAS", { pStash } } } };

		// Skyrim's own menu look: smoky black panels, off-white condensed type, thin rules with a diamond;
		// a muted gold marks what is chosen
		constexpr ImU32 kPanel = IM_COL32(0, 0, 0, 215);
		constexpr ImU32 kCard = IM_COL32(16, 16, 15, 235);
		constexpr ImU32 kCardHover = IM_COL32(42, 41, 38, 245);
		constexpr ImU32 kCardEdge = IM_COL32(78, 76, 70, 220);
		constexpr ImU32 kAccent = IM_COL32(236, 234, 226, 255);
		constexpr ImU32 kGold = IM_COL32(240, 236, 226, 255);    // the one accent: ivory, as on Skyrim's title screen
		constexpr ImU32 kGoldDim = IM_COL32(240, 236, 226, 70);
		constexpr ImU32 kSel = IM_COL32(236, 232, 222, 235);     // a chosen button: ivory, with dark type
		constexpr ImU32 kSelInk = IM_COL32(14, 14, 14, 255);
		constexpr ImU32 kInk = IM_COL32(232, 230, 222, 255);
		constexpr ImU32 kDim = IM_COL32(140, 140, 134, 255);
		constexpr ImU32 kFaint = IM_COL32(95, 94, 90, 255);

		// game controls blocked while the gallery panel is open (the mouse drives the cursor) ...
		using UEFlag = RE::ControlMap::UEFlag;
		constexpr UEFlag Flags(std::initializer_list<UEFlag> a_flags)
		{
			std::underlying_type_t<UEFlag> v = 0;
			for (auto f : a_flags) {
				v |= std::to_underlying(f);
			}
			return static_cast<UEFlag>(v);
		}
		constexpr UEFlag kMenuBlocked = Flags({ UEFlag::kLooking, UEFlag::kMovement, UEFlag::kFighting, UEFlag::kActivate,
			UEFlag::kPOVSwitch, UEFlag::kWheelZoom, UEFlag::kJumping, UEFlag::kSneaking, UEFlag::kMenu, UEFlag::kMainFour, UEFlag::kVATS });
		// ... and while placing (you can still walk and look around)
		constexpr UEFlag kPlaceBlocked = Flags({ UEFlag::kFighting, UEFlag::kActivate, UEFlag::kPOVSwitch, UEFlag::kWheelZoom,
			UEFlag::kSneaking, UEFlag::kMenu, UEFlag::kVATS, UEFlag::kJumping });  // never kMainFour: it stops walking
		// Placing and gallery mode take only the keys they use, out of the input before the game sees it (InputGate):
		// switching the game's controls off stopped the player walking. kPlaceBlocked is the fallback without it.
		// a game menu is up (paused, or one with a cursor: the map, inventory, journal, a container...): the
		// gallery's keys stand aside (the map doesn't pause the game - it was getting stuck)
		bool GameMenuOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return true;
			}
			if (ui->GameIsPaused()) {
				return true;
			}
			for (auto& m : ui->menuStack) {
				if (m && m->UsesCursor()) {
					return true;
				}
			}
			return false;
		}

		bool gateInstalled = false;
		bool gateDispatching = false;  // the gate is passing on input it has already shown us

		// controller buttons (XInput masks; triggers as Skyrim reports them)
		using Pad = RE::BSWin32GamepadDevice::Key;

		// keys (DirectInput scan codes)
		constexpr std::uint32_t kKeyEnter = 28, kKeyPadEnter = 156, kKeyBack = 14, kKeyTab = 15, kKeyE = 18, kKeyR = 19,
								kKeyF = 33, kKeyQ = 16, kKeyZ = 44, kKeyX = 45, kKeyEnd = 207, kKeyL = 38, kKeyBackslash = 43,
								kKeyUp = 200, kKeyDown = 208, kKeyLeft = 203, kKeyRight = 205, kKeyPgUp = 201, kKeyPgDn = 209,
								kKeyT = 20, kKeyG = 34, kKeyC = 46, kKeyV = 47, kKeyLCtrl = 29, kKeyRCtrl = 157, kKeyLShift = 42,
								kKeyRShift = 54, kKeyDelete = 211, kKeyHome = 199, kKeyEsc = 1, kKeyM = 50, kKeyP = 25, kKeyN = 49;

		// The build-mode keys the player can choose (mod menu). The rest of this file is written for the built-in
		// keys: a chosen key is read as the built-in key of its action, and a built-in key given away does nothing.
		enum Action
		{
			aPlace,
			aCancel,
			aPanel,
			aWheel,
			aLock,
			aSnap,
			aAlign,
			aGrid,
			aReset,
			aTakeDown,
			aMute,
			aPause,
			aNew,
			kActionCount
		};
		constexpr std::array<std::uint32_t, kActionCount> kBuiltIn{ kKeyE, kKeyBack, kKeyTab, kKeyR, kKeyF, kKeyQ, kKeyT, kKeyG, kKeyZ, kKeyX, kKeyM,
			kKeyP, kKeyN };
		std::array<std::atomic<std::uint32_t>, kActionCount> boundKey{ kKeyE, kKeyBack, kKeyTab, kKeyR, kKeyF, kKeyQ, kKeyT, kKeyG, kKeyZ, kKeyX, kKeyM,
			kKeyP, kKeyN };

		std::uint32_t BuiltInKey(std::uint32_t a_pressed)
		{
			for (int a = 0; a < kActionCount; ++a) {
				if (boundKey[a] == a_pressed) {
					return kBuiltIn[a];
				}
			}
			for (int a = 0; a < kActionCount; ++a) {
				if (kBuiltIn[a] == a_pressed) {
					return 0xFFFF;  // this action has another key now: its old one is the game's again
				}
			}
			return a_pressed;
		}

		// a key's name for a key cap (US layout names, as the game's own controls page shows them)
		std::string KeyName(std::uint32_t a_code)
		{
			static const char*         rows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
			static const std::uint32_t starts[] = { 0x10, 0x1E, 0x2C };
			for (int r = 0; r < 3; ++r) {
				if (a_code >= starts[r] && a_code < starts[r] + std::strlen(rows[r])) {
					return std::string(1, rows[r][a_code - starts[r]]);
				}
			}
			if (a_code >= 0x02 && a_code <= 0x0B) {
				return std::string(1, "1234567890"[a_code - 0x02]);
			}
			if (a_code >= 0x3B && a_code <= 0x44) {
				return "F" + std::to_string(a_code - 0x3A);
			}
			static const std::map<std::uint32_t, const char*> named{ { 0x39, "Space" }, { 0x1C, "Enter" }, { 0x0F, "Tab" }, { 0x0E, "Backspace" },
				{ 0x2A, "Shift" }, { 0x36, "R Shift" }, { 0x1D, "Ctrl" }, { 0x38, "Alt" }, { 0xD2, "Ins" }, { 0xD3, "Del" }, { 0xC7, "Home" }, { 0xCF, "End" },
				{ 0xC9, "PgUp" }, { 0xD1, "PgDn" }, { 0x57, "F11" }, { 0x58, "F12" }, { 0x0C, "-" }, { 0x0D, "=" }, { 0x1A, "[" }, { 0x1B, "]" },
				{ 0x27, ";" }, { 0x28, "'" }, { 0x33, "," }, { 0x34, "." }, { 0x35, "/" }, { 0x29, "`" }, { 0xC8, "Up" }, { 0xD0, "Down" },
				{ 0xCB, "Left" }, { 0xCD, "Right" } };
			const auto it = named.find(a_code);
			return it != named.end() ? it->second : "Key " + std::to_string(a_code);
		}

		std::string ActionKey(int a_action) { return KeyName(boundKey[a_action]); }

		struct State
		{
			bool                       open{ false };  // build mode
			bool                       menu{ false };  // gallery panel (mouse) - otherwise placing
			bool                       isNew{ true };
			int                        tab{ pPicture };  // page shown
			std::array<int, kTabCount> sel{};
			int                        picFilter{ 0 };  // picture page: 0 all, 1 pictures, 2 videos, 3 favourites
		int                        shape{ 0 };      // picture page: 0 every shape, 1 portrait, 2 square, 3 landscape, 4 panorama
			std::string                album;           // picture page: "" all albums, else a subfolder
			char                       search[64]{};
			char                       title[64]{};     // nameplate: its words ...
			char                       plateFont[64]{}; // ... the font they are set in (empty: the painting's old lettering style) ...
			int                        plateStyle{ 0 }; // ... and how: 1 bold, 2 italic, 4 underlined, 8 struck through, 16 spaced capitals
			char                       stashName[48]{}; // the hidden stash's safe, as the player calls it
			std::set<std::string>      picks;           // slideshow: the pictures ticked (fingerprints)
			bool                       browse{ false };  // gallery mode: look at a painting to change it, N for a new one
			std::vector<std::string>   undoList;         // what Undo can reach, newest first
			bool                       askTakeDown{ false };
			bool                       typing{ false }; // a text box has the keyboard
			int                        focus{ -1 };     // keyboard / controller: option row in focus, -1 = the cards
			bool                       pad{ false };    // last input came from a controller (hints show its buttons)
			char                       styleName[48]{};
			bool                       pickNight{ false };  // picture page: cards choose the night picture
			int                        cols{ 4 };       // cards per row, from the last drawn frame
			int                        menuKey{ 199 };
			bool                       scrollToSel{ false };
			std::string                toast;
			Clock::time_point          toastUntil{};
			Clock::time_point          opening{};       // Home pressed: the gallery is on its way
		};

		struct MouseInput
		{
			float dx{ 0 }, dy{ 0 }, wheel{ 0 };
			std::vector<std::pair<int, bool>> buttons;
			std::vector<std::pair<int, bool>> keys;   // ImGuiKey, down (typing)
			std::vector<unsigned int>         chars;  // typed characters
		};

		struct Tex
		{
			ID3D11ShaderResourceView* srv{ nullptr };
			int                       w{ 0 };
			int                       h{ 0 };
		};

		std::mutex stateLock;
		State      state;
		MouseInput mouse;
		int        mouseHeld{ 0 };  // buttons down: controls stay blocked until they are let go
		UEFlag     blockedNow{ UEFlag::kNone };
		ImVec2     cursor{ -1, -1 };

		using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
		PresentFn                  originalPresent{ nullptr };
		ID3D11Device*              device{ nullptr };
		ID3D11DeviceContext*       context{ nullptr };
		bool                       imguiReady{ false };
		ImFont*                    fontBody{ nullptr };
		ImFont*                    fontSmall{ nullptr };
		ImFont*                    fontTitle{ nullptr };
		std::map<long long, Tex>   thumbs;  // (pic << 16) | (filter << 8) | tone
		std::map<long long, int>   thumbUsed;  // the frame each thumbnail was last drawn: the oldest go when there are many
		int                        uiFrame{ 0 };
		constexpr size_t           kMaxThumbs = 320;  // a few screens of cards; hundreds of pictures never fill video memory
		std::map<std::string, Tex> assets;  // Data\PersonalGallery\UI\*.png
		int                        texturesThisFrame{ 0 };  // thumbnails uploaded per frame are capped, so the menu never hitches
		Clock::time_point          lastFrame;
		bool                       hudShown{ false };  // build mode was drawn last frame
		bool                       lastMenu{ false };
		Clock::time_point          shownAt;

		void Toast(State& s, std::string text)
		{
			s.toast = std::move(text);
			s.toastUntil = Clock::now() + std::chrono::milliseconds(1400);
		}

		// --- saved styles: whole looks (everything but the picture) under a name ------------------
		struct Style
		{
			std::string                name;
			std::array<int, kTabCount> v{};
		};
		constexpr const char* kDefaultStyle = "(my default)";  // the saved style new paintings start with (not listed)
		std::mutex         styleLock;

		// Ready-made looks to start from (the Saved styles list shows them first). Each sets a few choices on top of
		// the built-in look; the picture, display, size, nameplate words, stash, video and hours are left as they are.
		struct Preset
		{
			const char*                      name;
			std::vector<std::pair<int, int>> set;
		};
		const std::array<Preset, 7> kPresets{ {
			{ "Museum piece", { { kFrame, 1 }, { kMat, 7 }, { kGlass, 1 }, { kLamp, 1 }, { kLight, 1 } } },
			{ "Tavern wall", { { kFrame, 0 }, { kFilter, 1 }, { kTone, 2 }, { kLamp, 2 }, { kAge, 1 } } },
			{ "Old master", { { kFrame, 5 }, { kFilter, 4 }, { kTone, 4 }, { kMat, 2 }, { kAge, 2 } } },
			{ "Dwemer relic", { { kFrame, 4 }, { kTone, 7 }, { kMagic, 5 }, { kGlow, 0 }, { kLight, 2 } } },
			{ "Daedric shrine", { { kFrame, 9 }, { kTone, 5 }, { kMagic, 4 }, { kGlow, 5 }, { kLight, 5 } } },
			{ "Enchanted", { { kFrame, Catalog::kAutoFrame }, { kMagic, 1 }, { kGlow, 0 }, { kLight, 5 } } },
			{ "Winter hall", { { kFrame, 10 }, { kTone, 3 }, { kMagic, 7 }, { kGlow, 1 }, { kLight, 3 } } },
		} };

		std::array<int, kTabCount> PresetValues(int a_preset, const std::array<int, kTabCount>& a_now)
		{
			std::array<int, kTabCount> v = a_now;
			for (const int t : { kFilter, kTone, kMagic, kGlow, kLight }) {
				v[t] = 0;
			}
			for (int t = kIntensity; t < kDayNight; ++t) {
				switch (t) {
				case kStashOpen: case kStashAmount: case kStashSpeed: case kVolume: case kPlayMode: case kQuality: case kSlideTime:
				case kNightStart: case kNightEnd: case kPlate: case kPlateFont: case kPlateHue: case kPlateSize:
					break;  // not part of a look
				default:
					v[t] = Look::kDefaults[t - kIntensity];
				}
			}
			for (const auto& [t, value] : kPresets[std::clamp(a_preset, 0, static_cast<int>(kPresets.size()) - 1)].set) {
				v[t] = value;
			}
			return v;
		}
		std::vector<Style> styles;
		bool               stylesLoaded{ false };

		std::filesystem::path StylesFile() { return std::filesystem::current_path() / "Data" / "PersonalGallery" / "styles.txt"; }

		void LoadStyles()  // with styleLock held
		{
			stylesLoaded = true;
			if (FILE* f = _wfopen(StylesFile().c_str(), L"r")) {
				char line[2048];
				while (fgets(line, sizeof(line), f)) {
					std::string l(line);
					const auto  bar = l.find('|');
					if (l.empty() || l[0] == '#' || bar == std::string::npos) {
						continue;
					}
					Style st;
					st.name = l.substr(0, bar);
					std::vector<int> read;
					for (size_t pos = bar + 1; pos < l.size() && read.size() < kTabCount;) {
						read.push_back(std::atoi(l.c_str() + pos));
						pos = l.find(',', pos);
						pos = pos == std::string::npos ? l.size() : pos + 1;
					}
					// a style saved by an older version has fewer choices: they were always added just before the
					// day / night pair at the end, so the first ones line up and the rest take their defaults
					const int n = static_cast<int>(read.size());
					for (int i = 0; i < kTabCount; ++i) {
						st.v[i] = i >= kIntensity && i < kDayNight ? Look::kDefaults[i - kIntensity] : 0;
					}
					for (int i = 0; i < n - 2 && i < kDayNight; ++i) {
						st.v[i] = read[i];
					}
					if (n >= 2) {
						st.v[kDayNight] = read[n - 2];
						st.v[kNightPic] = read[n - 1];
					}
					styles.push_back(std::move(st));
				}
				fclose(f);
			}
		}

		void SaveStyles()  // with styleLock held
		{
			if (FILE* f = _wfopen(StylesFile().c_str(), L"w")) {
				fputs("# Personal Gallery saved styles: name|choices (picture excluded)\n", f);
				for (const auto& st : styles) {
					fputs(st.name.c_str(), f);
					fputc('|', f);
					for (int i = 0; i < kTabCount; ++i) {
						fprintf(f, i ? ",%d" : "%d", st.v[i]);
					}
					fputc('\n', f);
				}
				fclose(f);
			}
		}

		// the render thread's copy of the picture library, refreshed only when it changes
		std::vector<Pictures::Info> lib;
		std::uint64_t               libGen{ 0 };

		void RefreshLib()
		{
			if (Pictures::Generation() != libGen) {
				lib = Pictures::Snapshot(libGen);
			}
		}

		const Pictures::Info& PicInfo(int i)
		{
			static const Pictures::Info none{};
			return i >= 0 && i < static_cast<int>(lib.size()) ? lib[i] : none;
		}

		std::string PanLabel(int i, const char* lo, const char* hi)
		{
			if (i == 4) {
				return "Centre";
			}
			return std::string(i < 4 ? lo : hi) + " " + std::to_string(std::abs(i - 4));
		}

		int Count(int tab)
		{
			switch (tab) {
			case kPicture:
				return static_cast<int>(lib.size());
			case kFilter:
				return static_cast<int>(Catalog::kFilters.size());
			case kTone:
				return static_cast<int>(Catalog::kTones.size());
			case kLife:
				return static_cast<int>(Catalog::kLife.size());
			case kMagic:
				return static_cast<int>(Catalog::kMagic.size());
			case kDisplay:
				return static_cast<int>(Catalog::kDisplays.size());
			case kFrame:
				return static_cast<int>(Catalog::kFrames.size()) + 1;  // + Auto
			case kSize:
				return static_cast<int>(Catalog::kSizes.size());
			case kLight:
				return static_cast<int>(Catalog::kLights.size());
			case kGlow:
				return static_cast<int>(Catalog::kGlowStyles.size());
			case kSlideshow:
				return static_cast<int>(Catalog::kSlideshow.size());
			case kIntensity:
				return static_cast<int>(Catalog::kIntensities.size());
			case kFrameWidth:
				return static_cast<int>(Catalog::kFrameWidths.size());
			case kFrameDepth:
				return static_cast<int>(Catalog::kFrameDepths.size());
			case kMat:
				return static_cast<int>(Catalog::kMats.size());
			case kGlass:
				return 2;  // none, or a pane of glass
			case kAge:
				return static_cast<int>(Catalog::kAges.size());
			case kLamp:
				return static_cast<int>(Catalog::kLamps.size());
			case kPlate:
				return static_cast<int>(Catalog::kPlates.size());
			case kZoom:
				return static_cast<int>(Catalog::kZooms.size());
			case kPanX:
			case kPanY:
				return Catalog::kPanSteps;
			case kFlip:
				return static_cast<int>(Catalog::kFlips.size());
			case kVolume:
				return Catalog::kVolumeSteps;
			case kPlayMode:
				return static_cast<int>(Catalog::kPlayModes.size());
			case kQuality:
				return static_cast<int>(Catalog::kQualities.size());
			case kSlideTime:
				return static_cast<int>(Catalog::kSlideTimes.size());
			case kStashOpen:
				return static_cast<int>(Catalog::kStashOpenings.size());
			case kMagicHue:
			case kLightHue:
				return 73;  // its own colour, then every 5 degrees
			case kMagicStrength:
			case kMagicSpeed:
			case kLightBright:
				return 41;
			case kLightReach:
			case kPicBright:
			case kPicContrast:
			case kPicGamma:
			case kPicSat:
			case kPicWarm:
				return 31;
			case kMagicSat:
			case kMagicAlpha:
			case kMagicDepth:
			case kMixEdge:
			case kMixAura:
			case kMixSheen:
			case kMixVeil:
				return 21;
			case kMagicMotion:
				return static_cast<int>(Catalog::kMotions.size());
			case kSweep:
				return 2;
			case kTintHue:
			case kFrameHue:
			case kPlateHue:
			case kMatHue:
				return 73;
			case kMatTint:
				return 21;
			case kVideoFps:
				return 5;
			case kPlateSize:
				return 3;
			case kFrameTint:
				return 21;
			case kSheenSpeed:
			case kLightFlickerSpeed:
				return 41;
			case kTintStrength:
			case kVignette:
			case kSharpness:
			case kPicGloss:
			case kPicOpacity:
			case kLightSide:
			case kLightHeight:
			case kLightOut:
				return 21;
			case kLightFlicker:
				return static_cast<int>(Catalog::kFlickers.size());
			case kLightWhen:
			case kMagicWhen:
				return static_cast<int>(Catalog::kWhen.size());
			case kNightStart:
			case kNightEnd:
				return 24;
			case kMixCorners:
				return 3;
			case kSheenAngle:
				return 24;  // every 15 degrees
			case kMatWidth:
				return 4;
			case kPlateFont:
				return 5;
			case kGlassThick:
				return 3;
			case kBeam:
			case kVision:
				return 11;
			case kProjTilt:
			case kProjSize:
			case kProjTurn:
				return 21;
			case kFilterStrength:
			case kStashAmount:
			case kGlassShine:
				return 21;
			case kStashSpeed:
				return 29;  // 0.2 to 3 seconds
			case kDayNight:
				return 2;
			case kNightPic:
				return static_cast<int>(lib.size());
			default:
				return static_cast<int>(Catalog::kLightLevels.size());
			}
		}

		std::string Label(int tab, int i)
		{
			if (i < 0 || i >= Count(tab)) {
				return {};
			}
			auto str = [](std::string_view v) { return std::string(v); };
			switch (tab) {
			case kPicture:
				return PicInfo(i).name;
			case kFilter:
				return str(Catalog::kFilters[i]);
			case kTone:
				return str(Catalog::kTones[i]);
			case kLife:
				return str(Catalog::kLife[i]);
			case kMagic:
				return str(Catalog::kMagic[i]);
			case kDisplay:
				return str(Catalog::kDisplays[i]);
			case kFrame:
				return i == Catalog::kAutoFrame ? std::string("Auto") : str(Catalog::kFrames[i]);
			case kSize:
				return str(Catalog::kSizes[i]);
			case kLight:
				return str(Catalog::kLights[i]);
			case kGlow:
				return str(Catalog::kGlowStyles[i]);
			case kSlideshow:
				return str(Catalog::kSlideshow[i]);
			case kIntensity:
				return str(Catalog::kIntensities[i]);
			case kFrameWidth:
				return str(Catalog::kFrameWidths[i]);
			case kFrameDepth:
				return str(Catalog::kFrameDepths[i]);
			case kMat:
				return str(Catalog::kMats[i]);
			case kGlass:
				return str(Catalog::kGlass[i]);
			case kAge:
				return str(Catalog::kAges[i]);
			case kLamp:
				return str(Catalog::kLamps[i]);
			case kPlate:
				return str(Catalog::kPlates[i]);
			case kZoom:
				return str(Catalog::kZooms[i]);
			case kPanX:
				return PanLabel(i, "Left", "Right");
			case kPanY:
				return PanLabel(i, "Up", "Down");
			case kFlip:
				return str(Catalog::kFlips[i]);
			case kVolume:
				return i == 0 ? std::string("Mute") : std::to_string(i * 5) + "%";
			case kPlayMode:
				return str(Catalog::kPlayModes[i]);
			case kQuality:
				return str(Catalog::kQualities[i]);
			case kSlideTime:
				return str(Catalog::kSlideTimes[i]);
			case kStashOpen:  // short: which way it goes
				{
					static constexpr const char* kShort[]{ "Off", "Swing left", "Swing right", "Lift up", "Drop down", "Slide left",
						"Slide right", "Slide up" };  // words: the serif face has no arrows
					return kShort[std::clamp(i, 0, 7)];
				}
			case kMagicHue:
			case kLightHue:
				return i == 0 ? std::string("Its own colour") : std::to_string((i - 1) * 5) + "\xC2\xB0";
			case kMagicStrength:
			case kLightBright:
			case kLightReach:
				return std::to_string(i * 10) + "%";
			case kMagicSpeed:
				{
					if (i == 0) {
						return "Still";
					}
					char b[16];
					std::snprintf(b, sizeof(b), "%.1fx", i * 0.1f);
					return b;
				}
			case kMagicDepth:
				return i == 0 ? std::string("Its own") : std::to_string(i * 5) + "%";
			case kMagicSat:
			case kMagicAlpha:
			case kMixEdge:
			case kMixAura:
			case kMixSheen:
			case kMixVeil:
				return std::to_string(i * 5) + "%";
			case kMagicMotion:
				return str(Catalog::kMotions[i]);
			case kSweep:
				return i ? std::string("Reverse") : std::string("Forward");
			case kTintHue:
			case kFrameHue:
			case kPlateHue:
			case kMatHue:
				return i == 0 ? std::string("Its own") : std::to_string((i - 1) * 5) + "\xC2\xB0";
			case kMatTint:
				return std::to_string(i * 5) + "%";
			case kVideoFps:
				{
					static constexpr const char* rates[]{ "As the mod menu says", "15", "24", "30", "60" };
					return rates[std::clamp(i, 0, 4)];
				}
			case kPlateSize:
				{
					static constexpr const char* sz[]{ "Small", "Normal", "Large" };
					return sz[std::clamp(i, 0, 2)];
				}
			case kFrameTint:
				return std::to_string(i * 5) + "%";
			case kTintStrength:
			case kVignette:
			case kPicOpacity:
				return std::to_string(i * 5) + "%";
			case kPicGloss:
				return i == 0 ? std::string("Matte") : std::to_string(i * 5) + "%";
			case kSharpness:
				return i == 10 ? std::string("Untouched") : i < 10 ? "Softer " + std::to_string((10 - i) * 10) + "%" : "Sharper " + std::to_string((i - 10) * 10) + "%";
			case kSheenSpeed:
			case kLightFlickerSpeed:
				{
					if (i == 0) {
						return "Still";
					}
					char b[16];
					std::snprintf(b, sizeof(b), "%.1fx", i * 0.1f);
					return b;
				}
			case kLightSide:
				return i == 10 ? std::string("Centred") : i < 10 ? "Left " + std::to_string(10 - i) : "Right " + std::to_string(i - 10);
			case kLightHeight:
				return i == 10 ? std::string("Level") : i < 10 ? "Lower " + std::to_string(10 - i) : "Higher " + std::to_string(i - 10);
			case kLightOut:
				return std::to_string(std::max(2, i) * 10) + "%";
			case kLightFlicker:
				return str(Catalog::kFlickers[i]);
			case kBeam:
			case kVision:
				return i == 0 ? std::string("Off") : std::to_string(i * 10) + "%";
			case kProjTilt:
				return i == 10 ? std::string("Level") : i < 10 ? "Away " + std::to_string((10 - i) * 3) + "\xC2\xB0" : "Towards you " + std::to_string((i - 10) * 3) + "\xC2\xB0";
			case kProjSize:
				return std::to_string(50 + i * 10) + "%";
			case kProjTurn:
				return i == 10 ? std::string("Straight") : i < 10 ? "Left " + std::to_string((10 - i) * 3) : "Right " + std::to_string((i - 10) * 3);
			case kGlassThick:
				{
					static constexpr const char* t[]{ "Thin", "Normal", "Thick" };
					return t[std::clamp(i, 0, 2)];
				}
			case kPlateFont:
				{
					static constexpr const char* fonts[]{ "Classic", "Elegant", "Script", "Decorative", "Engraved capitals" };
					return fonts[std::clamp(i, 0, 4)];
				}
			case kMatWidth:
				{
					static constexpr const char* widths[]{ "Thin", "Normal", "Wide", "Extra wide" };
					return widths[std::clamp(i, 0, 3)];
				}
			case kMixCorners:
				{
					static constexpr const char* gems[]{ "As the glow style", "Always", "Never" };
					return gems[std::clamp(i, 0, 2)];
				}
			case kSheenAngle:
				return std::to_string(i * 15) + "\xC2\xB0";
			case kFilterStrength:
				return std::to_string(i * 5) + "%";
			case kStashAmount:
			case kGlassShine:
				return std::to_string(std::max(1, i) * 10) + "%";
			case kStashSpeed:
				{
					char b[16];
					std::snprintf(b, sizeof(b), "%.1f s", 0.2f + 0.1f * i);
					return b;
				}
			case kLightWhen:
			case kMagicWhen:
				return str(Catalog::kWhen[i]);
			case kNightStart:
			case kNightEnd:
				return i == 0 ? std::string("12 am") : i < 12 ? std::to_string(i) + " am" : i == 12 ? std::string("12 pm") : std::to_string(i - 12) + " pm";
			case kPicBright:
			case kPicContrast:
			case kPicGamma:
			case kPicSat:
			case kPicWarm:
				{
					const int v = (i - 15) * 100 / 15;
					return v == 0 ? std::string("Untouched") : (v > 0 ? "+" : "") + std::to_string(v) + "%";
				}
			case kDayNight:
				return i ? std::string("Another picture at night") : std::string("Same all day");
			case kNightPic:
				return PicInfo(i).name;
			default:
				return str(Catalog::kLightLevels[i]);
			}
		}

		bool Contains(const std::string& a_text, const char* a_find)
		{
			if (!*a_find) {
				return true;
			}
			auto lower = [](std::string v) {
				std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return v;
			};
			return lower(a_text).find(lower(a_find)) != std::string::npos;
		}

		// the cards a choice shows, in order
		std::vector<int> Visible(int tab, const State& s, const std::vector<Pictures::Info>& L)
		{
			std::vector<int> v;
			if (tab == kFrame) {
				v.push_back(Catalog::kAutoFrame);  // Auto first
			}
			const int n = tab == kFrame ? static_cast<int>(Catalog::kFrames.size()) : tab == kPicture ? static_cast<int>(L.size()) : Count(tab);
			for (int i = 0; i < n; ++i) {
				if (tab == kDisplay && (i == 9 || i == 12 || i == 14) && s.sel[kDisplay] != i) {
					continue;  // the scrying orb and basins are no longer offered (a painting that is one keeps it)
				}
				if (tab == kPicture) {
					const auto& p = L[i];
					const float ratio = Catalog::kAspects[std::clamp(p.aspect, 0, static_cast<int>(Catalog::kAspects.size()) - 1)];
					const int   shape = ratio < 0.9f ? 1 : ratio <= 1.1f ? 2 : ratio < 2.0f ? 3 : 4;
					if (!p.available || (s.shape != 0 && s.shape != shape) || (s.picFilter == 1 && p.video) || (s.picFilter == 2 && !p.video) || (s.picFilter == 3 && !p.favourite) ||
						(!s.album.empty() && p.album != s.album) || !(Contains(p.name, s.search) || Contains(p.album, s.search))) {
						continue;  // removed from the folder, or filtered out
					}
				}
				v.push_back(i);
			}
			return v;
		}

		bool UsesFrame(int display) { return display == 0 || display == 7; }

		// what a choice goes back to (Reset): as the scripts have it
		int DefaultOf(int sel)
		{
			return sel >= kIntensity && sel < kDayNight ? Look::kDefaults[sel - kIntensity] : sel == kLife ? 1 : 0;
		}

		// why a page currently does nothing (nullptr if it applies)
		const char* NotApplicable(int page, const State& s)
		{
			if (page == pVideo && !PicInfo(s.sel[kPicture]).video) {
				return "Only for videos - this picture is a still.";
			}
			if (page == pStyle && PicInfo(s.sel[kPicture]).video) {
				return "Only for still pictures - a video plays exactly as it is.";
			}
			if (page == pFrame && !UsesFrame(s.sel[kDisplay])) {
				return "This display has no frame. Choose Framed or Table frame under Display to use one.";
			}
			if (page == pFinish && s.sel[kDisplay] != 0) {
				return "Mat, glass and nameplate are for Framed paintings. Age works on every display.";
			}
			if (page == pStash && !Catalog::HangsOnWall(s.sel[kDisplay])) {
				return "Only for displays that hang on a wall: there is no wall here to hide a safe in.";
			}
			return nullptr;
		}

		// what the rail shows under a page's name
		std::string Summary(int page, const State& s)
		{
			switch (page) {
			case pPicture:
				return Label(kPicture, s.sel[kPicture]);
			case pChange:
				return s.sel[kDayNight] == 1 ? std::string("Another at night") : s.sel[kSlideshow] > 0 ? "Slideshow, " + Label(kSlideshow, s.sel[kSlideshow]) :
				                                                                                        std::string("Never");
			case pVideo:
				return Label(kLife, s.sel[kLife]);
			case pDisplay:
				return Label(kDisplay, s.sel[kDisplay]);
			case pFrame:
				return Label(kFrame, s.sel[kFrame]);
			case pFinish:
				{
					std::string out;
					auto add = [&](const std::string& part) { out += (out.empty() ? "" : ", ") + part; };
					if (s.sel[kMat] > 0) {
						add(Label(kMat, s.sel[kMat]) + " mat");
					}
					if (s.sel[kGlass] > 0) {
						add("glass");
					}
					if (s.sel[kPlate] > 0) {
						add("nameplate");
					}
					if (s.sel[kAge] > 0) {
						add(Label(kAge, s.sel[kAge]));
					}
					return out.empty() ? std::string("Plain") : out;
				}
			case pStyle:
				{
					const int filter = PicInfo(s.sel[kPicture]).video ? 0 : s.sel[kFilter];  // (a video is never painted over)
					return filter == 0 && s.sel[kTone] == 0 ? std::string("As it is") :
					       filter == 0 ? Label(kTone, s.sel[kTone]) : Label(kFilter, filter) + (s.sel[kTone] > 0 ? ", " + Label(kTone, s.sel[kTone]) : std::string());
				}
			case pLight:
				return s.sel[kLight] > 0 ? Label(kLight, s.sel[kLight]) : s.sel[kLamp] > 0 ? Label(kLamp, s.sel[kLamp]) : std::string("None");
			case pMagic:
				return s.sel[kMagic] == 0 ? std::string("None") : Label(kMagic, s.sel[kMagic]) + ", " + Label(kGlow, s.sel[kGlow]);
			default:
				return s.sel[kStashOpen] > 0 && Catalog::HangsOnWall(s.sel[kDisplay]) ? Label(kStashOpen, s.sel[kStashOpen]) : std::string("Off");
			}
		}

		Tex MakeTexture(const std::uint8_t* rgba, int w, int h, int pitch)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = w;
			desc.Height = h;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA init{ rgba, static_cast<UINT>(pitch), 0 };
			ID3D11Texture2D*       tex = nullptr;
			Tex                    out;
			if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &tex))) {
				device->CreateShaderResourceView(tex, nullptr, &out.srv);
				tex->Release();
				out.w = w;
				out.h = h;
			}
			return out;
		}

		const Tex* Thumb(int pic, int filter, int tone)
		{
			const long long key = (static_cast<long long>(pic) << 16) | (filter << 8) | tone;
			if (auto it = thumbs.find(key); it != thumbs.end()) {
				thumbUsed[key] = uiFrame;
				return it->second.srv ? &it->second : nullptr;
			}
			if (texturesThisFrame >= 4) {
				return nullptr;  // next frame
			}
			auto t = Pictures::Thumbnail(pic, filter, tone);
			if (!t) {
				return nullptr;  // still being made
			}
			++texturesThisFrame;
			if (t->rgba.empty()) {
				thumbs[key] = {};
				return nullptr;
			}
			// many pictures: let go of the thumbnails not seen for a while (never ones drawn this frame)
			for (int n = 0; n < 4 && thumbs.size() >= kMaxThumbs; ++n) {
				auto oldest = thumbUsed.end();
				for (auto it = thumbUsed.begin(); it != thumbUsed.end(); ++it) {
					if (it->second < uiFrame - 2 && (oldest == thumbUsed.end() || it->second < oldest->second)) {
						oldest = it;
					}
				}
				if (oldest == thumbUsed.end()) {
					break;
				}
				if (auto t2 = thumbs.find(oldest->first); t2 != thumbs.end()) {
					if (t2->second.srv) {
						t2->second.srv->Release();
					}
					thumbs.erase(t2);
				}
				thumbUsed.erase(oldest);
			}
			thumbs[key] = MakeTexture(t->rgba.data(), t->w, t->h, t->w * 4);
			thumbUsed[key] = uiFrame;
			return &thumbs[key];
		}

		const Tex* Asset(const std::string& name)
		{
			if (auto it = assets.find(name); it != assets.end()) {
				return it->second.srv ? &it->second : nullptr;
			}
			if (texturesThisFrame >= 4) {
				return nullptr;
			}
			++texturesThisFrame;
			Tex                   tex;
			DirectX::ScratchImage img, rgba;
			const auto            path = std::filesystem::current_path() / "Data" / "PersonalGallery" / "UI" / (name + ".png");
			if (SUCCEEDED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, img))) {
				const DirectX::Image* im = img.GetImage(0, 0, 0);
				if (im->format != DXGI_FORMAT_R8G8B8A8_UNORM &&
					SUCCEEDED(DirectX::Convert(*im, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba))) {
					im = rgba.GetImage(0, 0, 0);
				}
				tex = MakeTexture(im->pixels, static_cast<int>(im->width), static_cast<int>(im->height), static_cast<int>(im->rowPitch));
			}
			assets[name] = tex;
			return tex.srv ? &assets[name] : nullptr;
		}

		const Tex* CardImage(int tab, int i, const State& s)
		{
			switch (tab) {
			case kPicture:
				return Thumb(i, 0, 0);
			case kFilter:
				return Thumb(s.sel[kPicture], i, s.sel[kTone]);
			case kTone:
				return Thumb(s.sel[kPicture], s.sel[kFilter], i);
			case kDisplay:
				return Asset("display_" + std::string(Catalog::kDisplayKeys[i]));
			case kFrame:
				return Asset("frame_" + std::string(Catalog::kFrameKeys[i]));
			default:
				return nullptr;
			}
		}

		void SendSelect(std::string_view what, int index)
		{
			Dev::Log("select", "{} = {}", what, index);
			SKSE::ModCallbackEvent ev{ "PG_Select", RE::BSFixedString(what), static_cast<float>(index), nullptr };
			SKSE::GetModCallbackEventSource()->SendEvent(&ev);
		}

		void Rule(ImDrawList* dl, float x0, float x1, float y, ImU32 c = kAccent)
		{
			dl->AddLine({ x0, y }, { x1, y }, c, 1.0f);
			const float m = (x0 + x1) * 0.5f;
			dl->AddQuadFilled({ m - 6, y }, { m, y - 5 }, { m + 6, y }, { m, y + 5 }, c);
		}

		float TextW(ImFont* f, const char* t) { return f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0, t).x; }

		std::string Fit(ImFont* f, std::string t, float maxW)
		{
			while (t.size() > 3 && TextW(f, t.c_str()) > maxW) {
				t = t.substr(0, t.size() - 4) + "...";
			}
			return t;
		}

		// A key cap with its action after it; returns the width used.
		float KeyHint(ImDrawList* dl, ImVec2 p, const char* key, const char* action, float k, bool draw = true)
		{
			const float kw = TextW(fontSmall, key) + 16 * k, h = fontSmall->LegacySize + 8 * k;
			const float aw = TextW(fontSmall, action);
			if (draw) {
				dl->AddRectFilled(p, { p.x + kw, p.y + h }, IM_COL32(255, 255, 255, 18), 3 * k);
				dl->AddRect(p, { p.x + kw, p.y + h }, IM_COL32(236, 234, 226, 150), 3 * k);
				dl->AddText(fontSmall, fontSmall->LegacySize, { p.x + 8 * k, p.y + 4 * k }, kInk, key);
				dl->AddText(fontSmall, fontSmall->LegacySize, { p.x + kw + 8 * k, p.y + 4 * k }, kDim, action);
			}
			return kw + 8 * k + aw + 22 * k;
		}

		// A toggle's key: lit gold while it is on.
		float ToggleHint(ImDrawList* dl, ImVec2 p, const char* key, const char* action, bool on, float k, bool draw = true)
		{
			const float kw = TextW(fontSmall, key) + 16 * k, h = fontSmall->LegacySize + 8 * k;
			const float aw = TextW(fontSmall, action);
			if (draw) {
				dl->AddRectFilled(p, { p.x + kw, p.y + h }, on ? kSel : IM_COL32(255, 255, 255, 18), 3 * k);
				dl->AddRect(p, { p.x + kw, p.y + h }, on ? kGold : IM_COL32(236, 234, 226, 150), 3 * k);
				dl->AddText(fontSmall, fontSmall->LegacySize, { p.x + 8 * k, p.y + 4 * k }, on ? kSelInk : kInk, key);
				dl->AddText(fontSmall, fontSmall->LegacySize, { p.x + kw + 8 * k, p.y + 4 * k }, on ? kGold : kDim, action);
			}
			return kw + 8 * k + aw + 22 * k;
		}

		struct Toggle
		{
			const char* key;
			const char* action;
			bool        on;
		};

		void ToggleRow(ImDrawList* dl, float cx, float y, const std::vector<Toggle>& hints, float k)
		{
			float w = 0;
			for (auto& t : hints) {
				w += ToggleHint(dl, {}, t.key, t.action, t.on, k, false);
			}
			float x = cx - w * 0.5f;
			for (auto& t : hints) {
				x += ToggleHint(dl, { x, y }, t.key, t.action, t.on, k);
			}
		}

		// game units as metres (about 70 units to the metre)
		std::string Metres(float a_units)
		{
			char b[16];
			std::snprintf(b, sizeof(b), "%.2f m", a_units / 70.0f);
			return b;
		}

		// A row of key hints centred on `cx`.
		void HintRow(ImDrawList* dl, float cx, float y, const std::vector<std::pair<const char*, const char*>>& hints, float k)
		{
			float w = 0;
			for (auto& [key, action] : hints) {
				w += KeyHint(dl, {}, key, action, k, false);
			}
			float x = cx - w * 0.5f;
			for (auto& [key, action] : hints) {
				x += KeyHint(dl, { x, y }, key, action, k);
			}
		}

		// A little framed picture showing glow `style` in magic `magic`'s colour, moving like the real thing.
		// The picture inside a little preview frame: the chosen picture (cropped to fit), else a plain gradient.
		void MiniPicture(ImDrawList* dl, ImVec2 pa, ImVec2 pb, int pic)
		{
			if (const Tex* t = Thumb(pic, 0, 0)) {
				const float box = (pb.x - pa.x) / (pb.y - pa.y), img = static_cast<float>(t->w) / t->h;
				ImVec2      uv0{ 0, 0 }, uv1{ 1, 1 };
				if (img > box) {
					uv0.x = (1 - box / img) * 0.5f;
					uv1.x = 1 - uv0.x;
				} else {
					uv0.y = (1 - img / box) * 0.5f;
					uv1.y = 1 - uv0.y;
				}
				dl->AddImage(reinterpret_cast<ImTextureID>(t->srv), pa, pb, uv0, uv1);
				return;
			}
			dl->AddRectFilledMultiColor(pa, pb, IM_COL32(120, 128, 140, 255), IM_COL32(150, 140, 120, 255), IM_COL32(90, 80, 70, 255), IM_COL32(70, 80, 90, 255));
		}

		void GlowArt(ImDrawList* dl, ImVec2 mid, int magic, int style, float k, int pic)
		{
			const auto&  m = Catalog::kMagicLooks[magic > 0 ? magic : 1];
			const float  t = static_cast<float>(ImGui::GetTime());
			const float  pulse = 0.8f + 0.2f * std::sin(t * (1.5f + m.speed * 3.0f) + style);
			const ImVec2 fa{ mid.x - 58 * k, mid.y - 43 * k }, fb{ mid.x + 58 * k, mid.y + 43 * k };
			const ImVec2 pa{ fa.x + 9 * k, fa.y + 9 * k }, pb{ fb.x - 9 * k, fb.y - 9 * k };
			auto         col = [&](float a) { return IM_COL32(int(m.r * 255), int(m.g * 255), int(m.b * 255), int(std::clamp(a, 0.0f, 1.0f) * 255)); };
			const auto&  mix = Catalog::kGlowMix[style];
			if (mix[1] > 0) {  // aura
				for (int r = 6; r >= 1; --r) {
					dl->AddRect({ fa.x - r * 4 * k, fa.y - r * 4 * k }, { fb.x + r * 4 * k, fb.y + r * 4 * k }, col(0.07f * pulse * mix[1] * (7 - r)), 6 * k, 0, 4 * k);
				}
			}
			dl->AddRectFilled(fa, fb, IM_COL32(70, 52, 34, 255), 2 * k);  // frame
			MiniPicture(dl, pa, pb, pic);
			dl->PushClipRect(pa, pb, true);
			if (mix[0] > 0) {  // edge band
				for (int r = 0; r < 4; ++r) {
					dl->AddRect({ pa.x + r * 2 * k, pa.y + r * 2 * k }, { pb.x - r * 2 * k, pb.y - r * 2 * k }, col((0.9f - r * 0.22f) * pulse * mix[0]), 0, 0, 2 * k);
				}
			}
			if (mix[3] > 0) {  // sheen sweeping across, then resting
				const float ph = std::fmod(t / 3.0f, 1.0f) * 2.0f - 0.3f;
				const float x = pa.x + (pb.x - pa.x) * ph;
				for (int r = 3; r >= 0; --r) {
					const float wdt = (3 + r * 4) * k;
					dl->AddQuadFilled({ x - wdt, pb.y }, { x + 12 * k - wdt, pa.y }, { x + 12 * k + wdt, pa.y }, { x + wdt, pb.y }, col(0.18f * mix[3]));
				}
			}
			if (mix[4] > 0) {  // veil drifting over it
				for (int c = 0; c < 4; ++c) {
					const float cx = pa.x + std::fmod(c * 23.0f * k + t * 6.0f * k, pb.x - pa.x + 30 * k) - 15 * k;
					const float cy = pa.y + (0.25f + 0.17f * c) * (pb.y - pa.y);
					dl->AddCircleFilled({ cx, cy }, (9 + c * 3) * k, col(0.12f * pulse * mix[4]));
				}
			}
			dl->PopClipRect();
			if (mix[2] > 0) {  // corner gems, twinkling
				const ImVec2 cs[]{ pa, { pb.x, pa.y }, pb, { pa.x, pb.y } };
				for (int c = 0; c < 4; ++c) {
					const float tw = (0.6f + 0.4f * std::sin(t * 2.3f + c * 1.7f)) * mix[2];
					const float sz = 8 * k * tw;
					dl->AddCircleFilled(cs[c], 4 * k * tw, col(0.5f * tw));
					dl->AddLine({ cs[c].x - sz, cs[c].y }, { cs[c].x + sz, cs[c].y }, col(0.9f * tw), 1.5f * k);
					dl->AddLine({ cs[c].x, cs[c].y - sz }, { cs[c].x, cs[c].y + sz }, col(0.9f * tw), 1.5f * k);
				}
			}
			if (style == Catalog::kClassicGlow) {  // the old spell shader: the whole piece shimmers
				dl->AddRectFilled({ fa.x - 3 * k, fa.y - 3 * k }, { fb.x + 3 * k, fb.y + 3 * k }, col(0.28f * pulse), 4 * k);
				dl->AddRect({ fa.x - 3 * k, fa.y - 3 * k }, { fb.x + 3 * k, fb.y + 3 * k }, col(0.7f * pulse), 4 * k, 0, 2 * k);
			}
		}

		// Content drawn inside a card when there is no thumbnail.
		void CardArt(ImDrawList* dl, int tab, int i, ImVec2 a, ImVec2 b, bool on, float k, const State& s)
		{
			const ImVec2 mid{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
			if (tab == kGlow) {
				GlowArt(dl, mid, s.sel[kMagic], i, k, s.sel[kPicture]);
			} else if (tab == kFrame) {  // Auto
				const char* t = "Auto";
				dl->AddText(fontTitle, 30 * k, { mid.x - fontTitle->CalcTextSizeA(30 * k, FLT_MAX, 0, t).x * 0.5f, mid.y - 16 * k }, on ? kGold : kDim, t);
			} else if (tab == kLight) {
				auto c = Catalog::kLightColours[i];
				if (i == Catalog::kMatchMagicLight && s.sel[kMagic] > 0) {
					const auto& m = Catalog::kMagicLooks[s.sel[kMagic]];
					c = { m.r, m.g, m.b };
				}
				for (int r = 5; r >= 1; --r) {
					dl->AddCircleFilled(mid, r * 9 * k, IM_COL32(int(c[0] * 255), int(c[1] * 255), int(c[2] * 255), i == 0 ? 50 : 30 + r * 8));
				}
			} else if (tab == kSize) {
				const float side = std::min(b.x - a.x, b.y - a.y) * (0.2f + 0.14f * std::log2(Catalog::kSizeScales[i] * 2.0f + 1.0f));
				dl->AddRect({ mid.x - side * 0.6f, mid.y - side * 0.45f }, { mid.x + side * 0.6f, mid.y + side * 0.45f }, on ? kGold : kDim, 0, 0, 2.0f);
			} else if (tab == kMagic) {
				if (i > 0) {
					GlowArt(dl, mid, i, s.sel[kGlow], k, s.sel[kPicture]);  // this magic, in the chosen glow style
				} else {
					const ImVec2 fa{ mid.x - 58 * k, mid.y - 43 * k }, fb{ mid.x + 58 * k, mid.y + 43 * k };
					const ImVec2 pa{ fa.x + 9 * k, fa.y + 9 * k }, pb{ fb.x - 9 * k, fb.y - 9 * k };
					dl->AddRectFilled(fa, fb, IM_COL32(70, 52, 34, 255), 2 * k);
					MiniPicture(dl, pa, pb, s.sel[kPicture]);
					dl->AddRect(pa, pb, IM_COL32(0, 0, 0, 120), 0, 0, 3 * k);  // plain: just the inset shadow
				}
			} else if (tab == kLife) {
				static constexpr const char* icons[]{ "||", "( o )", ">>" };
				dl->AddText(fontTitle, 34 * k, { mid.x - fontTitle->CalcTextSizeA(34 * k, FLT_MAX, 0, icons[i]).x * 0.5f, mid.y - 18 * k }, on ? kGold : kDim, icons[i]);
			} else {
				const char* t = tab == kSlideshow ? (i ? "On" : "Off") : tab == kMagic ? "-" : "...";
				dl->AddText(fontTitle, 30 * k, { mid.x - fontTitle->CalcTextSizeA(30 * k, FLT_MAX, 0, t).x * 0.5f, mid.y - 16 * k }, on ? kGold : kDim, t);
			}
		}

		bool Primary(const char* label, ImVec2 size)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(kSel)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(255, 252, 244, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(210, 206, 196, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(kSelInk)));
			const bool r = ImGui::Button(label, size);
			ImGui::PopStyleColor(4);
			return r;
		}

		bool Link(const char* label)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(kDim)));
			const bool r = ImGui::SmallButton(label);
			ImGui::PopStyleColor(2);
			return r;
		}

		// Thin gold trim around a panel with a diamond at each corner.
		void Ornament(ImDrawList* dl, ImVec2 a, ImVec2 b, float k)
		{
			const ImU32 trim = IM_COL32(214, 186, 120, 90);
			const float in = 7 * k;
			dl->AddRect({ a.x + in, a.y + in }, { b.x - in, b.y - in }, trim, 3 * k, 0, 1.0f);
			for (const ImVec2 c : { ImVec2{ a.x + in, a.y + in }, ImVec2{ b.x - in, a.y + in }, ImVec2{ b.x - in, b.y - in }, ImVec2{ a.x + in, b.y - in } }) {
				dl->AddQuadFilled({ c.x - 5 * k, c.y }, { c.x, c.y - 5 * k }, { c.x + 5 * k, c.y }, { c.x, c.y + 5 * k }, kGold);
			}
		}

		// A row of choices: a label, then one button per option (the chosen one lit).
		// Is this row drawn right now? (Magic's rows wait for a magic; the stash row only on wall displays)
		// what the painting being edited can show: its model's own parts (a tapestry has no frame, mat or glass)
		struct Parts
		{
			bool known{ false }, frame{ false }, mat{ false }, glass{ false }, grime{ false }, lamp{ false }, plate{ false };
		} parts;

		void ScanParts()
		{
			Parts p;
			auto* ref = Place::Current();
			if (auto* root = ref ? ref->Get3D() : nullptr) {
				p.known = true;
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
					const std::string      lowered = Catalog::Lower(g->name.c_str());
					const std::string_view n = lowered;
					p.frame |= n.starts_with("frame_");
					p.mat |= n.starts_with("mat|");
					p.glass |= n.size() >= 5 && _strnicmp(n.data(), "glass", 5) == 0;  // "GLASS" in the game's string pool
					p.grime |= n == "grime";
					p.lamp |= n.starts_with("lamp_");
					p.plate |= n.starts_with("plate_");
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			p.glass |= p.mat;  // framed paintings are built with glass beside their mat
			static RE::NiAVObject* logged = nullptr;
			if (auto* r = Place::Current(); r && r->Get3D() && r->Get3D() != logged) {
				logged = r->Get3D();
				SKSE::log::info("editing {:08X}: frame {} mat {} glass {} age {} lamp {} plate {}", r->GetFormID(), p.frame, p.mat, p.glass,
					p.grime, p.lamp, p.plate);
			}
			parts = p;
		}

		// A page's rows in named sections; a click on a heading folds it away or opens it. The fine-tuning ones
		// start folded, so a page opens short and clean and nothing is hidden behind a separate link.
		struct Section
		{
			int         page, first;  // the row it starts at
			const char* name;
			bool        folded;       // starts closed
			int         cards{ -1 };  // a choice shown as a grid of cards at its top (filters, colour moods, lights)
		};
		const Section kSections[]{
			{ pDisplay, kBeam, "PROJECTION", false },
			{ pFrame, kFrameWidth, "SHAPE", false }, { pFrame, kFrameHue, "COLOUR", false },
			{ pFinish, kMat, "MAT", false }, { pFinish, kGlass, "GLASS AND AGE", false }, { pFinish, kPlate, "NAMEPLATE", false },
			{ pStyle, kFilterStrength, "PAINTED AS", false, kFilter }, { pStyle, kPicBright, "COLOUR MOOD", false, kTone },
			{ pStyle, kPicContrast, "FINE-TUNE", true },
			{ pLight, kLightLevel, "LIGHT", false, kLight }, { pLight, kLightHue, "FINE-TUNE", true },
			{ pLight, kLightWhen, "WHEN IT SHINES", true },
			{ pMagic, kMagicHue, "COLOUR", false }, { pMagic, kMagicMotion, "MOTION", false },
			{ pMagic, kMagicWhen, "WHEN IT SHINES", true },
		};
		std::set<int> sectionFlipped;  // sections clicked away from how they start (index in kSections)

		int SectionOf(int page, int sel)  // which section a row is in, -1 for the rows above the first heading
		{
			int at = -1;
			for (const auto& [rowSel, rowName] : kPages[page].rows) {
				for (int i = 0; i < static_cast<int>(std::size(kSections)); ++i) {
					if (kSections[i].page == page && kSections[i].first == rowSel) {
						at = i;
					}
				}
				if (rowSel == sel) {
					return at;
				}
			}
			return -1;
		}

		bool SectionOpen(int i) { return i < 0 || kSections[i].folded == (sectionFlipped.count(i) > 0); }

		// a_unfolded: as if "More settings" were open (is there anything it would show?)
		bool RowShown(const State& s, int sel, bool a_unfolded = false)
		{
			if (!a_unfolded && !SectionOpen(SectionOf(s.tab, sel))) {
				return false;  // its section is folded away
			}
			if (s.tab == pChange) {  // only the rows of the way it changes
				if ((sel == kSlideshow || sel == kSlideTime) && (s.sel[kSlideshow] == 0 || s.sel[kDayNight] == 1)) {
					return false;
				}
				if ((sel == kNightStart || sel == kNightEnd) && s.sel[kDayNight] != 1) {
					return false;
				}
			}
			if ((sel == kNightStart || sel == kNightEnd) &&
				((s.tab == pLight && s.sel[kLightWhen] == 0) || (s.tab == pMagic && s.sel[kMagicWhen] == 0))) {
				return false;  // the night's hours matter here only when it shines by night or by day
			}
			if ((sel == kMatHue || sel == kMatTint) && (s.sel[kMat] == 0 || (parts.known && !parts.mat))) {
				return false;  // only with a mat
			}
			if (sel == kMatTint && s.sel[kMatHue] == 0) {
				return false;  // only with a colour chosen
			}
			if (parts.known) {  // only what this display has
				if ((sel == kFrameWidth || sel == kFrameDepth) && !(parts.frame && parts.mat)) {
					return false;
				}
				if (((sel == kMat || sel == kMatWidth) && !parts.mat) || ((sel == kGlass || sel == kGlassShine || sel == kGlassThick) && !parts.glass) || (sel == kAge && !parts.grime) ||
					(sel == kLamp && !parts.lamp) || (sel == kPlate && !parts.plate)) {
					return false;
				}
			}
			if (s.tab == pMagic && s.sel[kMagic] == 0 && sel != kMagic) {
				return false;
			}
			if ((sel == kStashAmount || sel == kStashSpeed) && (s.sel[kStashOpen] == 0 || !Catalog::HangsOnWall(s.sel[kDisplay]))) {
				return false;  // how it opens: only with a stash
			}
			if ((sel == kGlassShine || sel == kGlassThick) && s.sel[kGlass] == 0) {
				return false;
			}
			if (sel == kFrameTint && s.sel[kFrameHue] == 0) {  // only with a colour chosen
				return false;
			}
			if (sel == kMatWidth && s.sel[kMat] == 0) {  // only with a mat
				return false;
			}
			if (sel == kSheenAngle && Catalog::kGlowMix[std::clamp(s.sel[kGlow], 0, static_cast<int>(Catalog::kGlowMix.size()) - 1)][3] <= 0) {
				return false;  // only a glow with a sweeping sheen has a direction to sweep in
			}
			if (sel == kVision && Catalog::kDisplayKeys[std::clamp(s.sel[kDisplay], 0, static_cast<int>(Catalog::kDisplayKeys.size()) - 1)].find("projector") == std::string_view::npos &&
				Catalog::kDisplayKeys[std::clamp(s.sel[kDisplay], 0, static_cast<int>(Catalog::kDisplayKeys.size()) - 1)].find("basin") == std::string_view::npos) {
				return false;  // projectors and the scrying basin
			}
			if ((sel == kBeam || sel == kProjTilt || sel == kProjSize || sel == kProjTurn) && Catalog::kDisplayKeys[std::clamp(s.sel[kDisplay], 0, static_cast<int>(Catalog::kDisplayKeys.size()) - 1)].find("projector") == std::string_view::npos) {
				return false;  // only on projectors
			}
			if ((sel == kPlateFont || sel == kPlateHue || sel == kPlateSize) && s.sel[kPlate] == 0) {  // only with a nameplate
				return false;
			}
			if (sel == kFilterStrength && s.sel[kFilter] == 0) {
				return false;
			}
			return sel != kStashOpen || Catalog::HangsOnWall(s.sel[kDisplay]);
		}

		bool IsSlider(int sel)
		{
			switch (sel) {
			case kZoom:
			case kPanX:
			case kPanY:
			case kVolume:
			case kMagicHue:
			case kMagicSat:
			case kMagicStrength:
			case kMagicAlpha:
			case kMagicSpeed:
			case kMagicDepth:
			case kMixEdge:
			case kMixAura:
			case kMixSheen:
			case kMixVeil:
			case kLightHue:
			case kLightBright:
			case kLightReach:
			case kPicBright:
			case kPicContrast:
			case kPicGamma:
			case kPicSat:
			case kPicWarm:
			case kTintHue:
			case kFrameHue:
			case kPlateHue:
			case kFrameTint:
			case kMatHue:
			case kMatTint:
			case kTintStrength:
			case kVignette:
			case kSharpness:
			case kPicGloss:
			case kPicOpacity:
			case kSheenSpeed:
			case kLightSide:
			case kLightHeight:
			case kLightOut:
			case kLightFlickerSpeed:
			case kNightStart:
			case kNightEnd:
			case kFilterStrength:
			case kStashAmount:
			case kGlassShine:
			case kBeam:
			case kVision:
			case kProjTilt:
			case kProjSize:
			case kProjTurn:
			case kStashSpeed:
				return true;
			default:
				return false;
			}
		}

		// Choices whose first entry is "off": shown as an on / off switch, then the styles.
		bool HasSwitch(int sel) { return sel == kMat || sel == kGlass || sel == kLamp || sel == kPlate || sel == kStashOpen; }

		// A small on / off switch; returns true when clicked.
		bool Switch(const char* id, bool on, float k)
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  w = 54 * k, h = 30 * k;
			const bool   clicked = ImGui::InvisibleButton(id, { w, h + 4 * k });
			auto*        dl = ImGui::GetWindowDrawList();
			const float  y = p.y + 2 * k;
			dl->AddRectFilled({ p.x, y }, { p.x + w, y + h }, on ? kSel : IM_COL32(255, 255, 255, 18), h * 0.5f);
			dl->AddRect({ p.x, y }, { p.x + w, y + h }, on ? kSel : IM_COL32(236, 234, 226, 90), h * 0.5f);
			const float knob = on ? p.x + w - h * 0.5f : p.x + h * 0.5f;
			dl->AddCircleFilled({ knob, y + h * 0.5f }, h * 0.36f, on ? kSelInk : kDim);
			return clicked;
		}

		// one line on what a setting does (hover its name)
		const char* Tip(int sel)
		{
			switch (sel) {
			case kFrameWidth: return "How wide the moulding is.";
			case kFrameDepth: return "How far the frame stands out from the wall.";
			case kFrameHue: return "Recolour the frame. Its own keeps its natural wood or metal.";
			case kFrameTint: return "How strongly the colour shows; the grain and carving stay.";
			case kMat: return "A board around the picture, inside the frame.";
			case kMatWidth: return "How wide the mat is around the picture.";
			case kGlass: return "A real pane in front of the picture: lamps and the sun glint on it.";
			case kGlassShine: return "How strong its reflections are.";
			case kGlassThick: return "Thicker glass stands off the picture and has a faint green edge.";
			case kAge: return "Yellowed varnish and fine cracks of old paint.";
			case kLamp: return "A brass picture light, or candle sconces either side.";
			case kPlate: return "A plaque under the painting with your own words.";
			case kPlateFont: return "The lettering style on the nameplate.";
			case kPlateHue: return "The plaque's colour.";
			case kPlateSize: return "How big the plaque is.";
			case kStashOpen: return "A wall safe behind the painting; it swings aside to open.";
			case kBeam: return "The cone of light from the projector. Off removes it.";
			case kVision: return "How strongly a projected picture (or the basin's water) lights itself. Lower it if it looks too bright at night.";
			case kProjSize: return "How big the projected picture is. Bigger stands further from the lens, as a real projection does.";
			case kProjTilt: return "Lean the projected picture towards you or away. The lens head turns with it; the stand stays where it is.";
			case kProjTurn: return "Turn the projected picture to the left or the right. The lens head turns with it.";
			case kStashAmount: return "How far the painting moves aside.";
			case kStashSpeed: return "How long it takes to move aside.";
			case kFilterStrength: return "How much of the filter shows over your picture.";
			case kTintHue: return "Wash the picture in a colour.";
			case kVignette: return "Darken the picture's edges.";
			case kSharpness: return "Sharpen, or soften, the picture.";
			case kPicGloss: return "Matte canvas to wet varnish.";
			case kLightLevel: return "How bright its light is. On this page the preview's own light is off: what you see is the painting's light alone.";
			case kLightHue: return "The light's colour. Its own keeps the lamp's natural colour.";
			case kLightSide: return "Move the light left or right of the painting.";
			case kLightHeight: return "Raise or lower the light.";
			case kLightOut: return "How far out from the painting the light sits.";
			case kLightFlicker: return "Steady, or flickering like a candle.";
			case kNightStart: return "When night begins for this painting (game hours).";
			case kNightEnd: return "When night ends.";
			case kLightWhen: return "Light always, only at night, or only by day.";
			case kMagicWhen: return "Magic always, only at night, or only by day.";
			case kVolume: return "How loud the video is, near it.";
			case kPlayMode: return "Loop, or play once.";
			case kQuality: return "The video's sharpness; lower is lighter on your PC.";
			case kVideoFps: return "How many frames a second this painting shows. The mod menu sets it for every painting; choose one here for this painting alone.";
			case kSlideTime: return "How often the slideshow changes picture.";
			default: return nullptr;
			}
		}

		// The painting takes the gallery's choices straight from the plugin (no round trip through the scripts).
		void LivePreview(const State& s)
		{
			if (auto* painting = Place::Current()) {
				Look::Set(painting, { s.sel.begin() + kIntensity, s.sel.begin() + kIntensity + Look::kOptionCount }, s.title);
			}
		}

		// The number a slider's step shows ("45%", "Left 3", "8 pm", "-20%"), for typing a value. False if it shows none.
		bool StepNumber(int sel, int i, double& a_out)
		{
			const std::string text = Label(sel, i);
			const auto        at = text.find_first_of("0123456789");
			if (at == std::string::npos) {
				return false;
			}
			double v = std::atof(text.c_str() + at);
			const bool minus = (at > 0 && text[at - 1] == '-') || text.starts_with("Softer") || text.starts_with("Left") ||
			                   text.starts_with("Lower") || text.starts_with("Away");
			if (text.ends_with("pm") && v < 12) {
				v += 12;  // hours: as a 24-hour clock
			} else if (text == "12 am") {
				v = 0;
			}
			a_out = minus ? -v : v;
			return true;
		}

		int NearestStep(int sel, double a_want)
		{
			int    best = 0;
			double bestGap = 1e18;
			for (int i = 0; i < Count(sel); ++i) {
				double v = 0;
				const double gap = StepNumber(sel, i, v) ? std::abs(v - a_want) : std::abs(a_want) + 0.001;  // a named step ("Untouched", "Off") stands for 0
				if (gap < bestGap) {
					bestGap = gap;
					best = i;
				}
			}
			return best;
		}

		bool IsColour(int sel) { return sel == kMagicHue || sel == kLightHue || sel == kTintHue || sel == kFrameHue || sel == kPlateHue || sel == kMatHue; }

		// A colour as a dropdown: its own, nine named colours, or Custom. Returns true when that is the whole row;
		// false when Custom is chosen - the caller then draws the slider (every colour, 5 degrees a step) under it.
		bool ColourRow(State& s, int sel, float k, float labelW)
		{
			struct Named
			{
				int         step;
				const char* name;
			};
			static constexpr Named named[]{ { 0, "Its own" }, { 1, "Red" }, { 7, "Orange" }, { 11, "Gold" }, { 19, "Lime" }, { 25, "Green" },
				{ 37, "Teal" }, { 45, "Blue" }, { 55, "Purple" }, { 63, "Pink" } };
			static std::set<int> custom;  // the colours being set by slider
			const Named*         now = nullptr;
			for (const auto& n : named) {
				if (n.step == s.sel[sel]) {
					now = &n;
				}
			}
			const bool isCustom = custom.count(sel) > 0 || !now;
			auto       swatch = [&](int step) {  // a dot of the colour, before its name
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const ImVec2 c{ p.x + 9 * k, p.y + ImGui::GetTextLineHeight() * 0.5f };
				if (step <= 0) {
					ImGui::GetWindowDrawList()->AddCircle(c, 6 * k, kDim, 16, 1.5f * k);
				} else {
					float r, g, b;
					Catalog::HueRGB((step - 1) * 5.0f, 0.8f, r, g, b);
					ImGui::GetWindowDrawList()->AddCircleFilled(c, 6.5f * k, ImColor(r, g, b), 16);
				}
				ImGui::Dummy({ 20 * k, 0 });
				ImGui::SameLine(0, 4 * k);
			};
			ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - 10 * k, 300 * k));
			if (ImGui::BeginCombo("##colour", isCustom ? "Custom" : now->name)) {
				for (const auto& n : named) {
					ImGui::PushID(n.step);
					swatch(n.step);
					if (ImGui::Selectable(n.name, !isCustom && now == &n)) {
						custom.erase(sel);
						if (s.sel[sel] != n.step) {
							s.sel[sel] = n.step;
							LivePreview(s);
							SendSelect(kTabKeys[sel], n.step);
						}
					}
					ImGui::PopID();
				}
				ImGui::Dummy({ 20 * k, 0 });
				ImGui::SameLine(0, 4 * k);
				if (ImGui::Selectable("Custom...", isCustom)) {
					custom.insert(sel);
				}
				ImGui::EndCombo();
			}
			if (!isCustom) {
				return true;
			}
			ImGui::Dummy({ labelW - 8 * k, 0 });  // the slider goes on the next line, under the dropdown
			ImGui::SameLine(labelW);
			return false;
		}

		void OptionRow(State& s, int sel, const char* label, float k, float labelW, bool focused)
		{
			ImGui::PushID(sel);
			if (focused) {  // keyboard / controller focus: a gold bar and a soft band behind the row
				const ImVec2 p = ImGui::GetCursorScreenPos();
				auto*        dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled({ p.x - 8 * k, p.y - 3 * k }, { p.x + ImGui::GetContentRegionAvail().x, p.y + 37 * k }, IM_COL32(240, 236, 226, 20), 4 * k);
				dl->AddRectFilled({ p.x - 8 * k, p.y - 3 * k }, { p.x - 5 * k, p.y + 37 * k }, kGold);
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ImColor(kDim), "%s", label);
			if (const char* tip = Tip(sel); tip && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", tip);
			}
			ImGui::SameLine(labelW);
			const int n = Count(sel);
			if (sel == kStashOpen && !Catalog::HangsOnWall(s.sel[kDisplay])) {  // stands in the room: no wall to hide a safe in
				ImGui::TextColored(ImColor(kFaint), "Only for displays that hang on a wall");
			} else if ((sel == kGlow || sel == kIntensity) && s.sel[kMagic] == 0) {
				ImGui::TextColored(ImColor(kFaint), "Choose a colour below first");
			} else if (IsColour(sel) && ColourRow(s, sel, k, labelW)) {
				// (a named colour was shown as a dropdown: nothing more on this row; Custom falls through to the slider)
			} else if (sel == kSheenAngle) {  // a dial: drag the arrow to the way the sheen should sweep (every 15 degrees)
				const float  d = 78 * k;
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::InvisibleButton("dial", { d, d });
				const ImVec2 c{ p.x + d * 0.5f, p.y + d * 0.5f };
				if (ImGui::IsItemActive()) {
					const ImVec2 at = ImGui::GetIO().MousePos;
					if (std::abs(at.x - c.x) + std::abs(at.y - c.y) > 4 * k) {
						const int idx = ((static_cast<int>(std::lround(std::atan2(at.y - c.y, at.x - c.x) / 0.2617994f)) % 24) + 24) % 24;
						if (idx != s.sel[sel]) {
							s.sel[sel] = idx;
							LivePreview(s);
						}
					}
				}
				if (ImGui::IsItemDeactivated()) {
					SendSelect(kTabKeys[sel], s.sel[sel]);
				}
				auto*       wl = ImGui::GetWindowDrawList();
				const float a = s.sel[sel] * 0.2617994f, r = d * 0.36f;
				const ImVec2 dir{ std::cos(a), std::sin(a) }, side{ -dir.y, dir.x };
				wl->AddRectFilled(p, { p.x + d, p.y + d }, IM_COL32(255, 255, 255, ImGui::IsItemHovered() ? 16 : 8), 3 * k);
				wl->AddRect(p, { p.x + d, p.y + d }, IM_COL32(236, 234, 226, 70), 3 * k);
				for (int tick = 0; tick < 8; ++tick) {
					const float t = tick * 0.7853982f;
					wl->AddLine({ c.x + std::cos(t) * d * 0.43f, c.y + std::sin(t) * d * 0.43f }, { c.x + std::cos(t) * d * 0.47f, c.y + std::sin(t) * d * 0.47f }, kFaint, 1.5f * k);
				}
				wl->AddLine({ c.x - side.x * r, c.y - side.y * r }, { c.x + side.x * r, c.y + side.y * r }, IM_COL32(236, 232, 222, 90), 5 * k);  // the band of light
				const ImVec2 tip{ c.x + dir.x * r, c.y + dir.y * r };
				wl->AddLine({ c.x - dir.x * r * 0.5f, c.y - dir.y * r * 0.5f }, tip, kGold, 2 * k);  // and the way it travels
				wl->AddTriangleFilled({ tip.x + dir.x * 6 * k, tip.y + dir.y * 6 * k }, { tip.x - dir.x * 4 * k + side.x * 6 * k, tip.y - dir.y * 4 * k + side.y * 6 * k },
					{ tip.x - dir.x * 4 * k - side.x * 6 * k, tip.y - dir.y * 4 * k - side.y * 6 * k }, kGold);
				ImGui::SameLine(0, 14 * k);
				ImGui::TextColored(ImColor(kFaint), "%s  -  drag the arrow", Label(sel, s.sel[sel]).c_str());
			} else if (IsSlider(sel)) {  // sliders for the many-step ones
				int         v = s.sel[sel];
				std::string shown = Label(sel, v);
				const float valueW = 118 * k;  // the value reads beside the slider, never under its handle
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - valueW - 10 * k);
				// the picture's own colour makes a new texture: send it when the slider is let go
				const bool onRelease = sel == kPicBright || sel == kPicContrast || sel == kPicGamma || sel == kPicSat || sel == kPicWarm ||
				                       sel == kTintHue || sel == kTintStrength || sel == kVignette || sel == kSharpness || sel == kFilterStrength ||
				                       sel == kFrameHue || sel == kFrameTint || sel == kPlateHue || sel == kMatHue || sel == kMatTint;
					// While it is dragged the plugin shows the change itself, at once; the scripts are told once, when it
				// is let go. (Every step used to go through the scripts: a fast drag queued dozens of events and
				// the painting trailed behind.)
				if (ImGui::SliderInt("##v", &v, 0, n - 1, "", ImGuiSliderFlags_NoInput) && v != s.sel[sel]) {
					s.sel[sel] = v;
					if (!onRelease) {
						LivePreview(s);
					}
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					SendSelect(kTabKeys[sel], s.sel[sel]);
				}
				if ((sel == kStashAmount || sel == kStashSpeed) && ImGui::IsItemDeactivatedAfterEdit() && s.sel[kStashOpen] > 0) {
					Extras::PreviewSwing(Place::Current(), s.sel[kStashOpen] - 1);  // show the new amount
				}
				if (sel == kMagicHue || sel == kLightHue || sel == kTintHue || sel == kFrameHue || sel == kPlateHue) {  // a strip of every colour under the slider
					const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
					auto*        hl = ImGui::GetWindowDrawList();
					const float  seg = (b.x - a.x - 8 * k) / 6.0f;
					for (int h = 0; h < 6; ++h) {
						float r0, g0, b0, r1, g1, b1;
						Catalog::HueRGB(h * 60.0f, 0.85f, r0, g0, b0);
						Catalog::HueRGB((h + 1) * 60.0f, 0.85f, r1, g1, b1);
						const ImU32 c0 = ImColor(r0, g0, b0), c1 = ImColor(r1, g1, b1);
						hl->AddRectFilledMultiColor({ a.x + 4 * k + h * seg, b.y - 5 * k }, { a.x + 4 * k + (h + 1) * seg, b.y - 2 * k }, c0, c1, c1, c0);
					}
				}
				{
					const std::string now = Label(sel, s.sel[sel]);
					ImGui::SameLine(0, 12 * k);
					// the value is a button: click it to type a number
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
					ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
					if (ImGui::Button((Fit(fontSmall, now, valueW - 16 * k) + "##value").c_str(), { valueW, 0 })) {
						ImGui::OpenPopup("typevalue");
					}
					ImGui::PopStyleVar();
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Click to type a number");
					}
					if (ImGui::BeginPopup("typevalue")) {
						static char typed[16];
						if (ImGui::IsWindowAppearing()) {
							typed[0] = 0;
							ImGui::SetKeyboardFocusHere();
						}
						ImGui::TextColored(ImColor(kFaint), "Type a number, then Enter");
						ImGui::SetNextItemWidth(160 * k);
						if (ImGui::InputText("##number", typed, sizeof(typed), ImGuiInputTextFlags_EnterReturnsTrue) && typed[0]) {
							const int best = NearestStep(sel, std::atof(typed));
							if (best != s.sel[sel]) {
								s.sel[sel] = best;
								LivePreview(s);
								SendSelect(kTabKeys[sel], best);
							}
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}
			} else {
				static std::map<int, int> lastOn;  // a switched-off choice comes back as it was
				int first = 0;
				if (HasSwitch(sel) && n <= 2) {  // on or off is all there is (glass): a switch
					const bool on = s.sel[sel] > 0;
					if (Switch("##switch", on, k)) {
						const int v = on ? 0 : std::max(1, lastOn.count(sel) ? lastOn[sel] : 1);
						if (on) {
							lastOn[sel] = s.sel[sel];
						}
						s.sel[sel] = v;
						SendSelect(kTabKeys[sel], v);
						if (sel == kStashOpen && v > 0) {
							Extras::PreviewSwing(Place::Current(), v - 1);
						}
					}
					if (!on) {
						ImGui::SameLine(0, 12 * k);
						ImGui::AlignTextToFramePadding();
						ImGui::TextColored(ImColor(kFaint), "Off");
						ImGui::PopID();
						return;
					}
					ImGui::SameLine(0, 10 * k);
					first = 1;
				}
				if (HasSwitch(sel) && n - first <= 1) {  // on or off is all there is (glass)
					ImGui::PopID();
					return;
				}
				// one of several: a dropdown, not a row of buttons that wraps down the page (how a stash opens stays as
				// buttons: each click plays that opening, so they are there to be tried one after another)
				if (n - first >= 3 && sel != kStashOpen) {
					ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - 10 * k, 300 * k));
					if (ImGui::BeginCombo("##choice", Label(sel, s.sel[sel]).c_str())) {
						for (int i = first; i < n; ++i) {
							const bool on = s.sel[sel] == i;
							if (ImGui::Selectable(Label(sel, i).c_str(), on) && !on) {
								s.sel[sel] = i;
								SendSelect(kTabKeys[sel], i);
								if (sel == kStashOpen && i > 0) {  // show how it will open
									Extras::PreviewSwing(Place::Current(), i - 1);
								} else if (sel == kStashSpeed && s.sel[kStashOpen] > 0) {
									Extras::PreviewSwing(Place::Current(), s.sel[kStashOpen] - 1);
								}
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopID();
					return;
				}
				bool drawn = false;
				for (int i = first; i < n; ++i) {
					// a hidden stash opens three ways, the ones that look right: a door-like swing to either side, or a
					// slide straight up (a painting that already opens another way keeps it, and shows it here)
					if (sel == kStashOpen && i >= 3 && i <= 6 && s.sel[sel] != i) {
						continue;
					}
					if (drawn) {
						ImGui::SameLine(0, 6 * k);
					}
					drawn = true;
					const bool        on = s.sel[sel] == i;
					const std::string text = Label(sel, i);
					const float       w = TextW(fontSmall, text.c_str()) + 26 * k;
					if (ImGui::GetContentRegionAvail().x < w && i > first) {
						ImGui::NewLine();  // wrap onto another line under the buttons
						ImGui::SetCursorPosX(labelW);
					}
					ImGui::PushID(i);
					ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(ImColor(kSel)) : ImVec4(ImColor(255, 255, 255, 8)));
					ImGui::PushStyleColor(ImGuiCol_Border, on ? ImVec4(ImColor(236, 232, 222, 235)) : ImVec4(ImColor(236, 234, 226, 45)));
					ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(ImColor(kSelInk)) : ImVec4(ImColor(190, 188, 180, 255)));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
					if (ImGui::Button(text.c_str(), { w, 34 * k }) && !on) {
						s.sel[sel] = i;
						SendSelect(kTabKeys[sel], i);
							if (sel == kStashOpen && i > 0) {  // show how it will open
								Extras::PreviewSwing(Place::Current(), i - 1);
							} else if (sel == kStashSpeed && s.sel[kStashOpen] > 0) {
								Extras::PreviewSwing(Place::Current(), s.sel[kStashOpen] - 1);
							}
					}
					ImGui::PopStyleVar();
					ImGui::PopStyleColor(3);
					ImGui::PopID();
				}
			}
			ImGui::PopID();
		}

		// A gold five-pointed star (favourite).
		void Star(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
		{
			ImVec2 pts[10];
			for (int i = 0; i < 10; ++i) {
				const float a = -1.5708f + i * 0.6283f, rr = i % 2 ? r * 0.45f : r;
				pts[i] = { c.x + std::cos(a) * rr, c.y + std::sin(a) * rr };
			}
			for (int i = 0; i < 10; i += 2) {  // as five triangles around the centre, so it stays convex
				dl->AddTriangleFilled(c, pts[i], pts[(i + 1) % 10], col);
				dl->AddTriangleFilled(c, pts[(i + 9) % 10], pts[i], col);
			}
		}

		// The fonts installed on this PC that the nameplate can be set in (TrueType, plain-letter names), sorted.
		const std::vector<std::string>& InstalledFonts()
		{
			static std::vector<std::string> list;
			static bool                     listed = false;
			if (!listed) {
				listed = true;
				std::set<std::wstring> names;
				if (HDC dc = GetDC(nullptr)) {
					LOGFONTW any{};
					any.lfCharSet = DEFAULT_CHARSET;
					EnumFontFamiliesExW(
						dc, &any,
						[](const LOGFONTW* a_font, const TEXTMETRICW*, DWORD a_type, LPARAM a_names) -> int {
							if (a_font->lfFaceName[0] != L'@' && (a_type & TRUETYPE_FONTTYPE)) {
								reinterpret_cast<std::set<std::wstring>*>(a_names)->insert(a_font->lfFaceName);
							}
							return 1;
						},
						reinterpret_cast<LPARAM>(&names), 0);
					ReleaseDC(nullptr, dc);
				}
				for (const auto& name : names) {
					std::string plain;
					for (const wchar_t c : name) {
						if (c < 32 || c > 126 || c == L';' || c == L'|' || c == L'#') {
							plain.clear();  // a name the menu's own font can't show, or one the title couldn't carry
							break;
						}
						plain += static_cast<char>(c);
					}
					if (!plain.empty()) {
						list.push_back(plain);
					}
				}
			}
			return list;
		}

		// the tone number the scripts send for these choices (PGControlScript.ToneCode): the grade, then the five
		// picture adjustments above it
		int ToneCodeOf(const State& s)
		{
			int code = 0, unit = 1;
			for (int i = 0; i < 5; ++i, unit *= 32) {
				if (const int v = s.sel[kPicBright + i]; v != 15) {
					code += (v + 1) * unit;
				}
			}
			return std::clamp(s.sel[kTone], 0, 15) + code * 16;
		}

		// how wide the gallery panel is (the showcase sits in the space to its right)
		float PanelWidth(float W, float k) { return std::clamp(W * 0.42f, 720 * k, 820 * k); }

		// The gallery panel, docked on the left so the painting stays in view on the right: a rail of pages in the
		// order a painting is made, the page itself, and a short footer.
		// appear: 0..1 as the panel slides and fades in
		void DrawMenu(State& s, float W, float H, float appear)
		{
			RefreshLib();
			ScanParts();
			const float  k = H / 1080.0f;
			const ImVec2 size{ PanelWidth(W, k), H };
			const float  railW = 196 * k;
			const float  ease = 1.0f - (1.0f - appear) * (1.0f - appear);
			ImGui::SetNextWindowPos({ -(1.0f - ease) * 80 * k, 0 });
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
			ImGui::SetNextWindowSize(size);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
			ImGui::Begin("Gallery", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PopStyleColor();
			ImDrawList*  dl = ImGui::GetWindowDrawList();
			const ImVec2 o = ImGui::GetWindowPos();
			bool         rightUsed = false;  // a right-click on a card (favourite) doesn't also close the panel
			const ImU32  hair = IM_COL32(240, 236, 226, 34);

			{  // black, like the title screen, fading into the world at its edge
				auto*       bg = ImGui::GetBackgroundDrawList();
				const ImU32 solid = IM_COL32(0, 0, 0, static_cast<int>(236 * ease)), clear = IM_COL32(0, 0, 0, 0);
				bg->AddRectFilled(o, { o.x + size.x, o.y + size.y }, solid);
				bg->AddRectFilledMultiColor({ o.x + size.x, o.y }, { o.x + size.x + 160 * k, o.y + size.y }, solid, clear, clear, solid);
				dl->AddLine({ o.x + size.x - 1, o.y + 40 * k }, { o.x + size.x - 1, o.y + size.y - 40 * k }, hair);
			}

			// a picture shown only to try something (by day / by night, the next slide): put back on leaving the page
			static bool trying = false;
			static int  showMe = 0;     // day and night: 0 as it is now, 1 by day, 2 by night
			static int  slideAt = -1;   // slideshow: the picture the preview has reached
			auto showPicture = [&](int pic) {
				if (auto* cur = Place::Current(); cur && pic >= 0) {
					Apply::Set(cur, pic, s.sel[kFilter], ToneCodeOf(s));
				}
			};
			auto pictureNow = [&]() {  // what it shows at this hour
				auto* cal = RE::Calendar::GetSingleton();
				const bool night = s.sel[kDayNight] == 1 && Catalog::InHours(cal ? cal->GetHour() : 12.0f, s.sel[kNightStart], s.sel[kNightEnd]);
				return night ? s.sel[kNightPic] : s.sel[kPicture];
			};
			if (trying && s.tab != pChange) {
				trying = false;
				showMe = 0;
				slideAt = -1;
				showPicture(pictureNow());
			}

			// header: what you are making, and the painting at a glance
			{
				const char* title = s.isNew ? "N E W   P A I N T I N G" : "A D J U S T   P A I N T I N G";
				dl->AddText(fontTitle, fontTitle->LegacySize * 0.64f, { o.x + 30 * k, o.y + 28 * k }, kInk, title);
				std::string glance = Label(kDisplay, s.sel[kDisplay]);
				if (UsesFrame(s.sel[kDisplay])) {
					glance += "  \xC2\xB7  " + Label(kFrame, s.sel[kFrame]);
				}
				if (s.sel[kMagic] > 0) {
					glance += "  \xC2\xB7  " + Label(kMagic, s.sel[kMagic]);
				}
				glance = Fit(fontSmall, glance, size.x - 80 * k);
				dl->AddText(fontSmall, fontSmall->LegacySize * 0.84f, { o.x + 30 * k, o.y + 62 * k }, kDim, glance.c_str());
				dl->AddLine({ o.x + 30 * k, o.y + 100 * k }, { o.x + size.x - 30 * k, o.y + 100 * k }, IM_COL32(240, 236, 226, 80));
			}

			const float footer = 176 * k, top = 112 * k, fy = size.y - footer;

			// the rail: every page that applies to this painting, in the order you make one, with what is chosen
			{
				dl->AddRectFilled({ o.x, o.y + 101 * k }, { o.x + railW, o.y + fy }, IM_COL32(255, 255, 255, 5));
				dl->AddLine({ o.x + railW, o.y + 108 * k }, { o.x + railW, o.y + fy - 8 * k }, hair);
				float y = 114 * k;
				for (const auto& g : kGroups) {
					std::vector<int> pages;
					for (const int pg : g.pages) {
						if (pg == s.tab || pg == pFinish || NotApplicable(pg, s) == nullptr) {
							pages.push_back(pg);
						}
					}
					if (pages.empty()) {
						continue;
					}
					{  // the group's name, small and spaced
						float x = o.x + 30 * k;
						for (const char* c = g.name; *c; ++c) {
							const char ch[2]{ *c, 0 };
							dl->AddText(fontSmall, fontSmall->LegacySize * 0.64f, { x, o.y + y }, kFaint, ch);
							x += TextW(fontSmall, ch) * 0.64f + 2.6f * k;
						}
					}
					y += 22 * k;
					for (const int pg : pages) {
						const bool on = pg == s.tab;
						ImGui::SetCursorPos({ 12 * k, y });
						ImGui::PushID(pg);
						if (ImGui::InvisibleButton("page", { railW - 22 * k, 46 * k })) {
							s.tab = pg;
							s.focus = kPages[pg].cards >= 0 ? -1 : 0;
							s.scrollToSel = true;
						}
						const bool   hov = ImGui::IsItemHovered();
						const ImVec2 a{ o.x + 12 * k, o.y + y }, b{ o.x + railW - 10 * k, o.y + y + 46 * k };
						if (on) {
							dl->AddRectFilled(a, b, kSel, 3 * k);
						} else if (hov) {
							dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 18), 3 * k);
						}
						dl->AddText(fontSmall, fontSmall->LegacySize * 0.96f, { o.x + 28 * k, o.y + y + 4 * k }, on ? kSelInk : kInk, kPages[pg].name);
						const std::string what = Fit(fontSmall, Summary(pg, s), (railW - 46 * k) / 0.66f);
						dl->AddText(fontSmall, fontSmall->LegacySize * 0.66f, { o.x + 28 * k, o.y + y + 26 * k }, on ? IM_COL32(70, 68, 62, 255) : kFaint, what.c_str());
						ImGui::PopID();
						y += 49 * k;
					}
					y += 7 * k;
				}
			}

			// the page: heading, help, its own tools, rows in folding sections, cards
			const auto& page = kPages[s.tab];
			static int               shownPage = -1;
			static Clock::time_point pageAt;
			if (shownPage != s.tab) {
				shownPage = s.tab;
				pageAt = Clock::now();
			}
			const float fade = std::clamp(std::chrono::duration<float>(Clock::now() - pageAt).count() / 0.15f, 0.0f, 1.0f);
			ImGui::SetCursorPos({ railW + 28 * k, top });
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease * (0.35f + 0.65f * fade));
			ImGui::BeginChild("content", { size.x - railW - 28 * k - 16 * k, fy - top - 6 * k }, false);
			ImGui::PushFont(fontTitle);
			ImGui::TextColored(ImColor(kInk), "%s", page.name);
			ImGui::PopFont();
			ImGui::PushFont(fontSmall);
			ImGui::PushTextWrapPos(0.0f);  // wrap at the panel's edge
			const char* notHere = NotApplicable(s.tab, s);
			ImGui::TextColored(ImColor(notHere ? kGold : kDim), "%s", notHere ? notHere : page.help);
			ImGui::PopTextWrapPos();
			ImGui::Dummy({ 0, 4 * k });

			const float labelW = 150 * k;
			auto pills = [&](const char* id, std::initializer_list<const char*> names, int current) {  // a row of buttons, one lit; returns the one clicked or -1
				int clicked = -1, i = 0;
				ImGui::PushID(id);
				for (const char* name : names) {
					if (i) {
						ImGui::SameLine(0, 6 * k);
					}
					const bool on = i == current;
					ImGui::PushID(i);
					ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(ImColor(kSel)) : ImVec4(ImColor(255, 255, 255, 8)));
					ImGui::PushStyleColor(ImGuiCol_Border, on ? ImVec4(ImColor(236, 232, 222, 235)) : ImVec4(ImColor(236, 234, 226, 45)));
					ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(ImColor(kSelInk)) : ImVec4(ImColor(kInk)));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
					if (ImGui::Button(name, { TextW(fontSmall, name) + 26 * k, 34 * k }) && !on) {
						clicked = i;
					}
					ImGui::PopStyleVar();
					ImGui::PopStyleColor(3);
					ImGui::PopID();
					++i;
				}
				ImGui::PopID();
				return clicked;
			};
			auto clockText = [](int minutes) {
				minutes = ((minutes % 1440) + 1440) % 1440;
				const int h = minutes / 60, m = minutes % 60;
				char      b[16];
				std::snprintf(b, sizeof(b), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
				return std::string(b);
			};

			if (s.tab == pPicture) {  // one bar: search, album, shape; then the kinds
				ImGui::SetNextItemWidth(210 * k);
				ImGui::InputTextWithHint("##search", "Search by name...", s.search, sizeof(s.search));
				std::set<std::string> albumSet;
				for (const auto& p : lib) {
					if (p.available && !p.album.empty()) {
						albumSet.insert(p.album);
					}
				}
				if (!albumSet.empty()) {
					ImGui::SameLine(0, 8 * k);
					ImGui::SetNextItemWidth(150 * k);
					if (ImGui::BeginCombo("##album", s.album.empty() ? "All albums" : s.album.c_str())) {
						if (ImGui::Selectable("All albums", s.album.empty())) {
							s.album.clear();
						}
						for (const auto& al : albumSet) {
							if (ImGui::Selectable(al.c_str(), s.album == al)) {
								s.album = al;
							}
						}
						ImGui::EndCombo();
					}
				}
				{  // by shape: tall pictures for a narrow wall, wide ones over a mantel
					static constexpr const char* shapes[]{ "Every shape", "Portrait", "Square", "Landscape", "Panorama" };
					ImGui::SameLine(0, 8 * k);
					ImGui::SetNextItemWidth(150 * k);
					if (ImGui::BeginCombo("##shape", shapes[std::clamp(s.shape, 0, 4)])) {
						for (int i = 0; i < 5; ++i) {
							if (ImGui::Selectable(shapes[i], s.shape == i)) {
								s.shape = i;
								s.scrollToSel = true;
							}
						}
						ImGui::EndCombo();
					}
				}
				if (const int c = pills("kinds", { "All", "Pictures", "Videos", "Favourites" }, s.picFilter); c >= 0) {
					s.picFilter = c;
					s.scrollToSel = true;
				}
				if (s.sel[kDayNight] == 1) {  // it shows another picture at night: which of the two the cards choose
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(ImColor(kDim), "Choosing");
					ImGui::SameLine(labelW);
					if (const int c = pills("which", { "Day picture", "Night picture" }, s.pickNight ? 1 : 0); c >= 0) {
						s.pickNight = c == 1;
						s.scrollToSel = true;
					}
				} else {
					s.pickNight = false;
				}
			}

			if (s.tab == pChange) {  // how the picture changes by itself, and a look at it changing
				const int kind = s.sel[kDayNight] == 1 ? 1 : s.sel[kSlideshow] > 0 ? 2 : 0;
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(ImColor(kDim), "It changes");
				ImGui::SameLine(labelW);
				if (const int c = pills("kind", { "Never", "At night", "As a slideshow" }, kind); c >= 0) {
					auto set = [&](int sel, int v) {
						if (s.sel[sel] != v) {
							s.sel[sel] = v;
							SendSelect(kTabKeys[sel], v);
						}
					};
					set(kSlideshow, c == 2 ? 1 : 0);  // one or the other, never both at once
					set(kDayNight, c == 1 ? 1 : 0);
					trying = true;  // (whatever was being tried goes back as the page is left)
					showMe = 0;
					slideAt = -1;
					showPicture(pictureNow());
				}
				ImGui::Dummy({ 0, 6 * k });
				if (kind == 1) {
					// the two pictures side by side, each with its own Change button
					const float  bw = (ImGui::GetContentRegionAvail().x - 10 * k - 12 * k) * 0.5f, bh = 120 * k;
					const ImVec2 p = ImGui::GetCursorScreenPos();
					auto*        wl = ImGui::GetWindowDrawList();
					for (int n = 0; n < 2; ++n) {
						const ImVec2 a{ p.x + n * (bw + 12 * k), p.y }, b{ a.x + bw, a.y + bh };
						const int    pic = n ? s.sel[kNightPic] : s.sel[kPicture];
						wl->AddRectFilled(a, b, kCard, 4 * k);
						wl->AddRect(a, b, kCardEdge, 4 * k);
						if (const Tex* t = Thumb(pic, 0, 0)) {
							const float  box = 100 * k, sc = std::min(box / t->w, box / t->h);
							const ImVec2 p0{ a.x + 10 * k + (box - t->w * sc) * 0.5f, a.y + 10 * k + (box - t->h * sc) * 0.5f };
							wl->AddImage(reinterpret_cast<ImTextureID>(t->srv), p0, { p0.x + t->w * sc, p0.y + t->h * sc });
						}
						wl->AddText(fontSmall, fontSmall->LegacySize, { a.x + 122 * k, a.y + 10 * k }, kInk, n ? "By night" : "By day");
						const std::string name = Fit(fontSmall, Label(kPicture, pic), (bw - 132 * k) / 0.8f);
						wl->AddText(fontSmall, fontSmall->LegacySize * 0.8f, { a.x + 122 * k, a.y + 36 * k }, kDim, name.c_str());
						ImGui::SetCursorScreenPos({ a.x + 122 * k, a.y + 70 * k });
						ImGui::PushID(n);
						if (ImGui::Button("Change", { 104 * k, 34 * k })) {  // to the Picture page, choosing this one
							s.pickNight = n == 1;
							s.tab = pPicture;
							s.scrollToSel = true;
						}
						ImGui::PopID();
					}
					ImGui::SetCursorScreenPos({ p.x, p.y + bh + 10 * k });
					ImGui::Dummy({ 1, 1 });
					// see either state now, without waiting for the clock
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(ImColor(kDim), "Show me");
					ImGui::SameLine(labelW);
					if (const int c = pills("showme", { "As it is now", "By day", "By night" }, showMe); c >= 0) {
						showMe = c;
						trying = c != 0;
						showPicture(c == 1 ? s.sel[kPicture] : c == 2 ? s.sel[kNightPic] : pictureNow());
					}
					if (auto* cal = RE::Calendar::GetSingleton()) {
						const std::string now = "In Skyrim it is " + clockText(static_cast<int>(cal->GetHour() * 60.0f)) +
						                        ". It is usually dark from about 8 PM to 6 AM.";
						ImGui::PushTextWrapPos(0.0f);
						ImGui::TextColored(ImColor(kFaint), "%s", now.c_str());
						ImGui::PopTextWrapPos();
					}
				} else if (kind == 0) {
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextColored(ImColor(kFaint), "At night: it shows another picture between the hours you choose. As a slideshow: it moves on to the next picture every so often.");
					ImGui::PopTextWrapPos();
				}
			}

			auto pickGrid = [&] {  // every still picture as a card: a click adds it to the slideshow or takes it out
				ImGui::Dummy({ 0, 4 * k });
				const float cw = 122 * k, ch = 100 * k, gap = 10 * k;
				const int   cols = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x - 14 * k + gap) / (cw + gap)));
				int         n = 0;
				for (int i = 0; i < static_cast<int>(lib.size()); ++i) {
					if (!lib[i].available || lib[i].video) {
						continue;
					}
					if (n++ % cols) {
						ImGui::SameLine(0, gap);
					}
					ImGui::PushID(i);
					const bool        clicked = ImGui::InvisibleButton("pick", { cw, ch });
					const ImVec2      a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
					const std::string id = Pictures::Id(i);
					const bool        on = s.picks.count(id) > 0;
					auto*             pdl = ImGui::GetWindowDrawList();
					pdl->AddRectFilled(a, b, ImGui::IsItemHovered() ? kCardHover : kCard, 3 * k);
					if (ImGui::IsItemVisible()) {
						if (const Tex* t = Thumb(i, 0, 1)) {
							const float  sc = std::min((cw - 12 * k) / t->w, (ch - 12 * k) / t->h);
							const ImVec2 p0{ a.x + (cw - t->w * sc) * 0.5f, a.y + (ch - t->h * sc) * 0.5f };
							pdl->AddImage(reinterpret_cast<ImTextureID>(t->srv), p0, { p0.x + t->w * sc, p0.y + t->h * sc }, { 0, 0 }, { 1, 1 },
								on ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 150));
						}
					}
					pdl->AddRect(a, b, on ? kGold : kCardEdge, 3 * k, 0, on ? 2.5f : 1.0f);
					if (on) {  // a tick in the corner
						const ImVec2 c{ b.x - 16 * k, a.y + 16 * k };
						pdl->AddCircleFilled(c, 11 * k, kSel);
						pdl->AddLine({ c.x - 5 * k, c.y }, { c.x - 1 * k, c.y + 4 * k }, kSelInk, 2.5f * k);
						pdl->AddLine({ c.x - 1 * k, c.y + 4 * k }, { c.x + 6 * k, c.y - 5 * k }, kSelInk, 2.5f * k);
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", lib[i].name.c_str());
					}
					if (clicked && !id.empty()) {
						on ? (void)s.picks.erase(id) : (void)s.picks.insert(id);
						SendSelect("slidepick|" + id, 0);
					}
					ImGui::PopID();
				}
			};

			// My picks: the slideshow's own pictures in a strip (a click takes one out), and the grid of every
			// picture folded away until pictures are to be added (videos can't take turns)
			auto slidePicker = [&] {
				static bool adding = false;
				ImGui::Dummy({ 0, 8 * k });
				ImGui::TextColored(ImColor(kDim), "In this slideshow: %zu", s.picks.size());
				if (s.picks.empty()) {
					ImGui::TextColored(ImColor(kFaint), "None yet - click the pictures below to add them.");
				} else {
					const float cw = 78 * k, ch = 62 * k, gap = 8 * k;
					const int   cols = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x - 14 * k + gap) / (cw + gap)));
					int         n = 0;
					auto*       wl = ImGui::GetWindowDrawList();
					for (int i = 0; i < static_cast<int>(lib.size()); ++i) {
						if (!lib[i].available || lib[i].video) {
							continue;
						}
						const std::string id = Pictures::Id(i);
						if (id.empty() || !s.picks.count(id)) {
							continue;
						}
						if (n++ % cols) {
							ImGui::SameLine(0, gap);
						}
						ImGui::PushID(100000 + i);
						const bool   out = ImGui::InvisibleButton("in", { cw, ch });
						const bool   hot = ImGui::IsItemHovered();
						const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
						wl->AddRectFilled(a, b, kCard, 3 * k);
						if (ImGui::IsItemVisible()) {
							if (const Tex* t = Thumb(i, 0, 1)) {
								const float  sc = std::min((cw - 8 * k) / t->w, (ch - 8 * k) / t->h);
								const ImVec2 p0{ a.x + (cw - t->w * sc) * 0.5f, a.y + (ch - t->h * sc) * 0.5f };
								wl->AddImage(reinterpret_cast<ImTextureID>(t->srv), p0, { p0.x + t->w * sc, p0.y + t->h * sc }, { 0, 0 }, { 1, 1 },
									hot ? IM_COL32(255, 255, 255, 120) : IM_COL32_WHITE);
							}
						}
						wl->AddRect(a, b, hot ? IM_COL32(220, 120, 110, 255) : kCardEdge, 3 * k, 0, hot ? 2.0f : 1.0f);
						if (hot) {
							ImGui::SetTooltip("%s\nClick to take it out", lib[i].name.c_str());
						}
						if (out) {
							s.picks.erase(id);
							SendSelect("slidepick|" + id, 0);
						}
						ImGui::PopID();
					}
				}
				ImGui::Dummy({ 0, 4 * k });
				if (Link(adding || s.picks.empty() ? "Hide the pictures to add" : "Add pictures...")) {
					adding = !(adding || s.picks.empty());
				}
				if (!s.picks.empty()) {
					ImGui::SameLine(0, 16 * k);
					if (Link("Take them all out")) {
						for (const auto& id : std::set<std::string>(s.picks)) {
							SendSelect("slidepick|" + id, 0);
						}
						s.picks.clear();
						adding = true;
					}
				}
				if (adding || s.picks.empty()) {
					pickGrid();
				}
			};

			auto section = [&](const char* name, bool open) {  // a heading with a hairline; a click folds or opens it
				ImGui::Dummy({ 0, 8 * k });
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImDrawList*  wl = ImGui::GetWindowDrawList();
				ImGui::PushID(name);
				const bool clicked = ImGui::InvisibleButton("fold", { ImGui::GetContentRegionAvail().x - 10 * k, 24 * k });
				const bool hot = ImGui::IsItemHovered();
				ImGui::PopID();
				ImGui::SetCursorScreenPos(p);
				wl->AddRectFilled({ p.x - 6 * k, p.y - 4 * k }, { p.x + ImGui::GetContentRegionAvail().x - 6 * k, p.y + 22 * k }, IM_COL32(255, 255, 255, hot ? 16 : 7), 3 * k);
				{  // a small arrow: down when open, right when folded
					const ImVec2 c{ p.x + 6 * k, p.y + 9 * k };
					const float  r = 4.5f * k;
					if (open) {
						wl->AddTriangleFilled({ c.x - r, c.y - r * 0.6f }, { c.x + r, c.y - r * 0.6f }, { c.x, c.y + r * 0.8f }, kDim);
					} else {
						wl->AddTriangleFilled({ c.x - r * 0.6f, c.y - r }, { c.x + r * 0.8f, c.y }, { c.x - r * 0.6f, c.y + r }, kDim);
					}
				}
				float x = p.x + 20 * k;
				for (const char* c = name; *c; ++c) {
					const char ch[2]{ *c, 0 };
					wl->AddText(fontSmall, fontSmall->LegacySize * 0.8f, { x, p.y + 1 * k }, kInk, ch);
					x += TextW(fontSmall, ch) * 0.8f + 2.4f * k;
				}
				ImGui::Dummy({ 0, 30 * k });
				return clicked;
			};
			auto textBox = [&](const char* label, const char* id, const char* hint, char* buf, size_t bufSize) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(ImColor(kDim), "%s", label);
				ImGui::SameLine(labelW);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10 * k);
				ImGui::InputTextWithHint(id, hint, buf, bufSize);
				return ImGui::IsItemDeactivatedAfterEdit();
			};

			// A grid of cards for one choice. compact: four across, inside a section (filters, colour moods, lights);
			// own: the grid scrolls in its own box under the page's tools (the pictures).
			auto cardGrid = [&](const int tab, const bool compact, const bool own) {
				const int   slot = tab == kPicture && s.pickNight && s.sel[kDayNight] == 1 ? kNightPic : tab;  // which choice a click sets
				const float gap = (compact ? 9 : 12) * k, avail = ImGui::GetContentRegionAvail().x - 14 * k;
				const bool  big = tab == kPicture || tab == kDisplay || tab == kFrame;
				const int   cols = compact ? 4 : big ? 3 : std::max(1, static_cast<int>((avail + gap) / (160 * k + gap)));
				const float cw = std::floor((avail - (cols - 1) * gap) / cols), ch = compact ? cw * 0.98f : big ? cw * 1.06f : 150 * k;
				const float lab = (compact ? 28 : 38) * k, pad = (compact ? 7 : 10) * k;
				if (!compact) {
					s.cols = cols;
				}
				const auto  list = Visible(tab, s, lib);
				const float need = std::ceil(static_cast<float>(list.size()) / cols) * (ch + gap) + gap;
				ImGui::PushID(tab);
				ImGui::BeginChild("cards", { 0, own ? 0.0f : need }, false, own ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				if (list.empty()) {
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextColored(ImColor(kDim), tab == kPicture && std::any_of(lib.begin(), lib.end(), [](const Pictures::Info& p) { return p.available; }) ?
					                                      (s.picFilter == 3 ? "No favourites yet - right-click a picture to add it." : "Nothing matches - try All, or another shape.") :
					                                      "Put pictures or videos in Data\\PersonalGallery\\Pictures, then Scan in the mod menu (MCM).");
					ImGui::PopTextWrapPos();
				}
				ImDrawList* cdl = ImGui::GetWindowDrawList();
				for (size_t n = 0; n < list.size(); ++n) {
					const int i = list[n];
					if (n % cols) {
						ImGui::SameLine(0, gap);
					}
					ImGui::PushID(i);
					const bool   clicked = ImGui::InvisibleButton("card", { cw, ch });
					const bool   visible = ImGui::IsItemVisible();  // off-screen cards wait: hundreds of pictures stay light
					const bool   hovered = ImGui::IsItemHovered();
					const bool   twice = !compact && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
					const bool   on = s.sel[slot] == i;
					if (on && s.scrollToSel && !compact) {
						ImGui::SetScrollHereY(0.5f);
						s.scrollToSel = false;
					}
					cdl->AddRectFilled(a, b, hovered ? kCardHover : kCard, 4 * k);
					cdl->AddRect(a, b, on ? kGold : hovered ? kAccent : kCardEdge, 4 * k, 0, on ? 2.5f : 1.0f);
					const ImVec2 ia{ a.x + pad, a.y + pad }, ib{ b.x - pad, b.y - lab };
					if (const Tex* t = !visible || (tab == kFrame && i == Catalog::kAutoFrame) ? nullptr : CardImage(tab, i, s)) {
						const float  sc = std::min((ib.x - ia.x) / t->w, (ib.y - ia.y) / t->h);
						const float  iw = t->w * sc, ih = t->h * sc;
						const ImVec2 p0{ ia.x + (ib.x - ia.x - iw) * 0.5f, ia.y + (ib.y - ia.y - ih) * 0.5f };
						cdl->AddImage(reinterpret_cast<ImTextureID>(t->srv), p0, { p0.x + iw, p0.y + ih }, { 0, 0 }, { 1, 1 }, notHere ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE);
					} else if (tab == kPicture || tab == kFilter || tab == kTone) {
						const char* wait = "...";
						cdl->AddText(fontBody, fontBody->LegacySize, { (ia.x + ib.x - TextW(fontBody, wait)) * 0.5f, (ia.y + ib.y) * 0.5f - 12 * k }, kFaint, wait);
					} else {
						CardArt(cdl, tab, i, ia, ib, on, compact ? k * 0.72f : k, s);
					}
					if (tab == kPicture) {
						const auto& info = PicInfo(i);
						if (info.video) {  // play badge on videos
							const ImVec2 c{ a.x + 24 * k, a.y + 24 * k };
							cdl->AddCircleFilled(c, 15 * k, IM_COL32(0, 0, 0, 190));
							cdl->AddCircle(c, 15 * k, IM_COL32(236, 234, 226, 120));
							cdl->AddTriangleFilled({ c.x - 5 * k, c.y - 7 * k }, { c.x - 5 * k, c.y + 7 * k }, { c.x + 8 * k, c.y }, kInk);
						}
						if (info.favourite) {
							Star(cdl, { b.x - 18 * k, b.y - 52 * k }, 10 * k, kGold);
						}
						if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
							const bool fav = !info.favourite;
							Pictures::SetFavourite(i, fav);
							Toast(s, fav ? "Added to favourites" : "Removed from favourites");
							rightUsed = true;
						}
					}
					const float       textScale = compact ? 0.8f : 1.0f;
					const std::string full = Label(tab, i);
					const std::string label = Fit(fontSmall, full, (cw - 12 * k) / textScale);
					cdl->AddText(fontSmall, fontSmall->LegacySize * textScale, { a.x + (cw - TextW(fontSmall, label.c_str()) * textScale) * 0.5f, b.y - lab + 5 * k },
						on ? kInk : kDim, label.c_str());
					if (hovered) {
						if (tab == kPicture) {
							const auto& info = PicInfo(i);
							ImGui::SetTooltip("%s%s%s\nRight-click: %s", full.c_str(), info.album.empty() ? "" : "  -  ", info.album.c_str(),
								info.favourite ? "remove from favourites" : "add to favourites");
						} else if (label != full) {
							ImGui::SetTooltip("%s", full.c_str());
						}
					}
					if ((clicked || twice) && !on) {
						s.sel[slot] = i;
						SendSelect(kTabKeys[slot], i);
					}
					if (twice) {
						s.menu = false;  // double-click: choose it and go straight to placing
					}
					ImGui::PopID();
				}
				ImGui::EndChild();
				ImGui::PopID();
			};

			if (parts.known && (s.tab == pFrame || s.tab == pFinish) && !page.rows.empty() && !notHere) {
				int hidden = 0;
				for (const auto& [sel, name] : page.rows) {
					hidden += !RowShown(s, sel, true) && sel != kGlassShine && sel != kGlassThick && sel != kMatWidth && sel != kMatHue && sel != kMatTint &&
					          sel != kFrameTint && sel != kPlateFont && sel != kPlateHue && sel != kPlateSize;
				}
				if (hidden > 0) {
					ImGui::PushTextWrapPos(0);
					ImGui::TextColored(ImColor(kFaint), "Some choices are not shown: this display doesn't have them (a framed one has them all).");
					ImGui::PopTextWrapPos();
				}
			}

			int headingDrawn = -1;
			for (int r = 0; r < static_cast<int>(page.rows.size()); ++r) {
				const int sel = page.rows[r].first;
				if (s.tab == pMagic && s.sel[kMagic] == 0 && sel != kMagic) {
					if (sel == kGlow) {
						ImGui::TextColored(ImColor(kFaint), "Choose a magic above - then every part of it is yours to tune.");
					}
					continue;
				}
				const int  sec = SectionOf(s.tab, sel);
				const bool applies = RowShown(s, sel, true);  // only with what it tunes, and only what this display has
				if (sec >= 0 && kSections[sec].cards == kFilter && PicInfo(s.sel[kPicture]).video) {
					continue;  // painted styles are for still pictures: for a video the section is not there at all
				}
				if (sec >= 0 && sec != headingDrawn && (applies || kSections[sec].cards >= 0)) {  // its heading, once
					headingDrawn = sec;
					if (section(kSections[sec].name, SectionOpen(sec))) {
						std::lock_guard fl(stateLock);  // (the keyboard reads this from the input thread)
						if (!sectionFlipped.erase(sec)) {
							sectionFlipped.insert(sec);
						}
					}
					if (SectionOpen(sec) && kSections[sec].cards >= 0) {
						cardGrid(kSections[sec].cards, true, false);
					}
				}
				if (!applies || !SectionOpen(sec)) {
					continue;
				}
				OptionRow(s, sel, page.rows[r].second, k, labelW, s.focus == r);
				if (s.tab == pFinish && sel == kPlate) {
					auto sendTitle = [&] {  // the words, then how they are lettered (Look.cpp reads it back)
						std::string full = s.title;
						if (s.plateFont[0] || s.plateStyle) {
							full += std::string(" ##font=") + (s.plateFont[0] ? s.plateFont : "Georgia") + ";";
							for (int bit = 0; bit < 5; ++bit) {
								if (s.plateStyle & (1 << bit)) {
									full += "biusc"[bit];
								}
							}
						}
						SendSelect("platetext|" + full, 0);
					};
					if (textBox("Title", "##title", "Words on the nameplate", s.title, sizeof(s.title))) {
						sendTitle();
						if (s.title[0] && s.sel[kPlate] == 0) {  // typing a title turns the plate on
							s.sel[kPlate] = 1;
							SendSelect(kTabKeys[kPlate], 1);
						}
					}
					if (s.sel[kPlate] > 0) {  // its lettering: any font on this PC, and how it is set
						ImGui::AlignTextToFramePadding();
						ImGui::TextColored(ImColor(kDim), "Font");
						ImGui::SameLine(labelW);
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10 * k);
						if (ImGui::BeginCombo("##platefont", s.plateFont[0] ? s.plateFont : "Georgia")) {
							for (const auto& name : InstalledFonts()) {
								const bool on = name == (s.plateFont[0] ? s.plateFont : "Georgia");
								if (ImGui::Selectable(name.c_str(), on) && !on) {
									strncpy_s(s.plateFont, name.c_str(), _TRUNCATE);
									sendTitle();
								}
								if (on && ImGui::IsWindowAppearing()) {
									ImGui::SetScrollHereY(0.5f);
								}
							}
							ImGui::EndCombo();
						}
						ImGui::AlignTextToFramePadding();
						ImGui::TextColored(ImColor(kDim), "Lettering");
						ImGui::SameLine(labelW);
						static constexpr const char* styles[]{ "Bold", "Italic", "Underline", "Strike", "Spaced capitals" };
						for (int bit = 0; bit < 5; ++bit) {
							if (bit) {
								ImGui::SameLine(0, 6 * k);
							}
							const bool on = (s.plateStyle & (1 << bit)) != 0;
							ImGui::PushID(bit);
							ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(ImColor(kSel)) : ImVec4(ImColor(255, 255, 255, 8)));
							ImGui::PushStyleColor(ImGuiCol_Border, on ? ImVec4(ImColor(236, 232, 222, 235)) : ImVec4(ImColor(236, 234, 226, 45)));
							ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(ImColor(kSelInk)) : ImVec4(ImColor(kInk)));
							ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
							const float w = TextW(fontSmall, styles[bit]) + 22 * k;
							if (ImGui::GetContentRegionAvail().x < w && bit) {
								ImGui::NewLine();
								ImGui::SetCursorPosX(labelW);
							}
							if (ImGui::Button(styles[bit], { w, 34 * k })) {
								s.plateStyle ^= 1 << bit;
								if (!s.plateFont[0]) {
									strncpy_s(s.plateFont, "Georgia", _TRUNCATE);
								}
								sendTitle();
							}
							ImGui::PopStyleVar();
							ImGui::PopStyleColor(3);
							ImGui::PopID();
						}
					}
				}
				if (s.tab == pStash && sel == kStashOpen && s.sel[kStashOpen] > 0 && Catalog::HangsOnWall(s.sel[kDisplay])) {
					ImGui::Dummy({ labelW - 8 * k, 0 });
					ImGui::SameLine(labelW);
					if (ImGui::Button("Show me", { 140 * k, 34 * k })) {  // play the opening on the painting
						Extras::PreviewSwing(Place::Current(), s.sel[kStashOpen] - 1);
					}
					ImGui::SameLine(0, 12 * k);
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(ImColor(kFaint), "plays how it opens");
					if (textBox("Safe name", "##stashname", "Hidden stash", s.stashName, sizeof(s.stashName))) {
						SendSelect(std::string("stashname|") + s.stashName, 0);
					}
				}
			}

			if (s.tab == pChange && s.sel[kSlideshow] > 0 && s.sel[kDayNight] != 1) {  // a look at the slideshow moving on
				ImGui::Dummy({ labelW - 8 * k, 0 });
				ImGui::SameLine(labelW);
				if (ImGui::Button("Show the next picture", { 220 * k, 34 * k })) {
					const int from = slideAt >= 0 ? slideAt : s.sel[kPicture];
					int       next = -1;
					if (s.sel[kSlideshow] == Catalog::kSlideMyPicks) {  // the next ticked still after this one
						const int n = static_cast<int>(lib.size());
						for (int step = 1; step <= n && next < 0; ++step) {
							const int i = ((from + step) % n + n) % n;
							if (lib[i].available && !lib[i].video && s.picks.count(Pictures::Id(i))) {
								next = i;
							}
						}
					} else {
						next = Pictures::NextSlide(from, s.sel[kSlideshow]);
					}
					if (next >= 0 && next != from) {
						slideAt = next;
						trying = true;
						showPicture(next);
					} else {
						Toast(s, s.sel[kSlideshow] == Catalog::kSlideMyPicks ? "Tick two or more pictures below" : "No other picture to show");
					}
				}
				if (slideAt >= 0) {
					ImGui::SameLine(0, 12 * k);
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(ImColor(kFaint), "%s", Fit(fontSmall, Label(kPicture, slideAt), 200 * k).c_str());
				}
				if (s.sel[kSlideshow] == Catalog::kSlideMyPicks) {
					slidePicker();
				}
			}

			if (!page.rows.empty() || s.tab == pChange) {  // every choice on the page back as it came
				ImGui::Dummy({ 0, 6 * k });
				if (Link("Reset this page")) {
					auto reset = [&](int sel) {
						if (const int def = DefaultOf(sel); s.sel[sel] != def) {
							s.sel[sel] = def;
							SendSelect(kTabKeys[sel], def);
						}
					};
					for (const auto& [sel, name] : page.rows) {
						reset(sel);
					}
					for (const auto& sec : kSections) {
						if (sec.page == s.tab && sec.cards >= 0) {
							reset(sec.cards);
						}
					}
					if (s.tab == pChange) {
						reset(kDayNight);
					}
					Toast(s, std::string(page.name) + ": back to default");
				}
			}
			ImGui::PopFont();

			if (page.cards >= 0) {
				ImGui::Dummy({ 0, 6 * k });
				ImGui::PushFont(fontSmall);
				cardGrid(page.cards, false, page.rows.empty());
				ImGui::PopFont();
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();

			{  // until the painting's 3D is there, the open space beside the panel says so (never just empty)
				auto* painting = Place::Current();
				if (!painting || !painting->Get3D()) {
					const char* wait = "Preparing your painting...";
					const float cx = (size.x + W) * 0.5f, cy = H * 0.5f, tw = TextW(fontBody, wait);
					auto*       fg = ImGui::GetForegroundDrawList();
					const float t = std::chrono::duration<float>(Clock::now() - pageAt).count();
					fg->PathArcTo({ cx, cy - 40 * k }, 16 * k, t * 6.0f, t * 6.0f + 4.4f, 24);
					fg->PathStroke(kInk, 0, 3 * k);
					fg->AddText(fontBody, fontBody->LegacySize, { cx - tw * 0.5f, cy }, kInk, wait);
				}
			}
			if (!rightUsed && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemActive()) {
				s.menu = false;  // right-click elsewhere: back to placing
			}

			// footer: the two buttons, one row of tools, the library line and the keys
			dl->AddLine({ o.x + 30 * k, o.y + fy }, { o.x + size.x - 30 * k, o.y + fy }, IM_COL32(240, 236, 226, 80));
			const ImVec2 bsize{ (size.x - 60 * k - 14 * k) * 0.5f, 50 * k };
			ImGui::SetCursorPos({ 30 * k, fy + 16 * k });
			if (ImGui::Button("Cancel", bsize)) {
				SendSelect("cancel", 0);
			}
			ImGui::SameLine(0, 14 * k);
			if (Primary(s.isNew ? "Place it" : "Move it", bsize)) {
				s.menu = false;
			}
			ImGui::PushFont(fontSmall);
			ImGui::SetCursorPos({ 30 * k, fy + 80 * k });
			auto tool = [&](const char* label) {
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ImColor(236, 234, 226, 55)));
				const bool r = ImGui::Button(label, { TextW(fontSmall, label) + 26 * k, 34 * k });
				ImGui::PopStyleColor();
				ImGui::PopStyleVar();
				ImGui::SameLine(0, 8 * k);
				return r;
			};
			if (tool("Undo")) {
				if (s.undoList.size() > 1) {
					ImGui::OpenPopup("undolist");
				} else {
					SendSelect("undo", 0);
				}
			}
			if (tool("Copy look")) {
				SendSelect("copy", 0);
				Toast(s, "Look copied");
			}
			if (tool("Paste look")) {
				SendSelect("paste", 0);
			}
			if (tool("Presets and styles")) {
				ImGui::OpenPopup("styles");
			}
			if (tool("More")) {
				ImGui::OpenPopup("more");
			}
			if (ImGui::BeginPopup("undolist")) {
				ImGui::TextColored(ImColor(kFaint), "Undo back to (and including):");
				for (size_t i = 0; i < s.undoList.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(s.undoList[i].c_str())) {
						SendSelect("undoto", static_cast<int>(i));
					}
					ImGui::PopID();
				}
				ImGui::EndPopup();
			}
			if (ImGui::BeginPopup("styles")) {
				std::lock_guard sl(styleLock);
				if (!stylesLoaded) {
					LoadStyles();
				}
				static int askDelete = -1;  // the style waiting for "are you sure"
				ImGui::TextColored(ImColor(kFaint), "Ready-made looks");
				for (int i = 0; i < static_cast<int>(kPresets.size()); ++i) {
					ImGui::PushID(1000 + i);
					if (ImGui::Selectable(kPresets[i].name)) {
						const auto v = PresetValues(i, s.sel);
						for (int t = 0; t < kTabCount; ++t) {
							if (t != kPicture && t != kDayNight && t != kNightPic) {
								s.sel[t] = v[t];
							}
						}
						SendSelect("style", 1000 + i);  // (GetStyle hands the scripts the same values)
						Toast(s, std::string("Look: ") + kPresets[i].name);
					}
					ImGui::PopID();
				}
				ImGui::Separator();
				const bool anyOwn = std::any_of(styles.begin(), styles.end(), [](const Style& st) { return st.name != kDefaultStyle; });
				ImGui::TextColored(ImColor(kFaint), anyOwn ? "Your own: click a style to use it" : "Your own: none yet - name this look below");
				for (int i = 0; i < static_cast<int>(styles.size()); ++i) {
					if (styles[i].name == kDefaultStyle) {
						continue;  // the default look is under More
					}
					ImGui::PushID(i);
					if (ImGui::SmallButton("Delete")) {
						askDelete = i;
						ImGui::OpenPopup("sure");
					}
					ImGui::SameLine(0, 10 * k);
					if (ImGui::Selectable(styles[i].name.c_str())) {
						for (int t = 0; t < kTabCount; ++t) {
							if (t != kPicture && t != kDayNight && t != kNightPic) {
								s.sel[t] = styles[i].v[t];
							}
						}
						SendSelect("style", i);
						Toast(s, "Style: " + styles[i].name);
					}
					if (ImGui::BeginPopup("sure")) {  // deleting asks first
						if (askDelete == i) {
							ImGui::Text("Delete the style \"%s\"?", styles[i].name.c_str());
							ImGui::TextColored(ImColor(kFaint), "This can't be undone.");
							if (ImGui::Button("Delete", { 120 * k, 34 * k })) {
								Toast(s, "Deleted style: " + styles[i].name);
								styles.erase(styles.begin() + i);
								SaveStyles();
								askDelete = -1;
								ImGui::CloseCurrentPopup();
								ImGui::EndPopup();
								ImGui::PopID();
								break;  // the list changed: draw it afresh next frame
							}
							ImGui::SameLine(0, 10 * k);
							if (ImGui::Button("Keep it", { 120 * k, 34 * k })) {
								askDelete = -1;
								ImGui::CloseCurrentPopup();
							}
						} else {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
					ImGui::PopID();
				}
				ImGui::Separator();
				ImGui::TextColored(ImColor(kDim), "Save this look as");
				ImGui::SetNextItemWidth(260 * k);
				if (ImGui::IsWindowAppearing()) {
					ImGui::SetKeyboardFocusHere();  // ready to type straight away
				}
				const bool entered = ImGui::InputTextWithHint("##stylename", "Type a name...", s.styleName, sizeof(s.styleName),
					ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if ((ImGui::Button("Save") || entered) && s.styleName[0]) {
					std::string name = s.styleName;
					std::replace(name.begin(), name.end(), '|', '/');
					styles.push_back({ name, s.sel });
					SaveStyles();
					Toast(s, "Saved style: " + name);
					s.styleName[0] = 0;
				}
				ImGui::EndPopup();
			}
			if (ImGui::BeginPopup("more")) {
				static int ask = 0;  // 1: reset everything, 2: take it down - the second click does it
				if (ImGui::IsWindowAppearing()) {
					ask = 0;
				}
				if (Link(ask == 1 ? "Click again: every setting back to default" : "Reset everything to default")) {
					if (ask == 1) {
						for (const auto& pg : kPages) {
							for (const auto& [sel, name] : pg.rows) {
								s.sel[sel] = DefaultOf(sel);
							}
						}
						for (const int sel : { kFilter, kTone, kMagic, kGlow, kLight, kSlideshow, kDayNight }) {
							s.sel[sel] = 0;
						}
						SendSelect("defaults", 0);  // the scripts do the same, once
						Toast(s, "Everything back to default");
						ImGui::CloseCurrentPopup();
					} else {
						ask = 1;
					}
				}
				ImGui::Separator();
				{  // the look new paintings start with
					std::lock_guard sl(styleLock);
					if (!stylesLoaded) {
						LoadStyles();
					}
					const auto mine = std::find_if(styles.begin(), styles.end(), [](const Style& st) { return st.name == kDefaultStyle; });
					if (ImGui::Selectable("Start new paintings with this look")) {
						if (mine != styles.end()) {
							mine->v = s.sel;
						} else {
							styles.push_back({ kDefaultStyle, s.sel });
						}
						SaveStyles();
						Toast(s, "New paintings will start with this look");
					} else if (mine != styles.end() && ImGui::Selectable("Start new paintings plain again")) {
						styles.erase(mine);
						SaveStyles();
						Toast(s, "New paintings start plain again");
					}
				}
				ImGui::Separator();
				if (s.isNew) {
					if (ImGui::Selectable("Move the nearest painting instead")) {
						SendSelect("nearest", 0);
					}
				} else if (Link(ask == 2 ? "Click again: take it down" : "Take this painting down")) {
					if (ask == 2) {
						SendSelect("takedown", 0);
						ImGui::CloseCurrentPopup();
					} else {
						ask = 2;
					}
				}
				if (ImGui::Selectable("Hide the paintings here")) {
					SendSelect("hide", 0);
				}
				if (ImGui::Selectable("Show the paintings here")) {
					SendSelect("show", 0);
				}
				ImGui::EndPopup();
			}
			{  // what the library holds (or what it is busy with), and the keys
				std::string line;
				ImU32       col = kFaint;
				if (auto status = Pictures::Status(); !status.empty()) {
					line = status;
					col = kGold;
				} else {
					int videos = 0, total = 0;
					for (const auto& p : lib) {
						if (p.available) {
							++total;
							videos += p.video ? 1 : 0;
						}
					}
					line = std::to_string(total - videos) + (total - videos == 1 ? " picture, " : " pictures, ") + std::to_string(videos) +
					       (videos == 1 ? " video" : " videos");
				}
				const std::string keyLine = "Enter place    " + ActionKey(aPanel) + " back    " + ActionKey(aSnap) + " / " + ActionKey(aPlace) + " page    " +
				                            ActionKey(aCancel) + " cancel";
				const char*       keys = keyLine.c_str();
				const float kw = TextW(fontSmall, keys) * 0.76f;
				line = Fit(fontSmall, line, (size.x - 80 * k - kw) / 0.76f);
				dl->AddText(fontSmall, fontSmall->LegacySize * 0.76f, { o.x + 30 * k, o.y + fy + 134 * k }, col, line.c_str());
				dl->AddText(fontSmall, fontSmall->LegacySize * 0.76f, { o.x + size.x - 30 * k - kw, o.y + fy + 134 * k }, kFaint, keys);
			}
			ImGui::PopFont();
			ImGui::End();
			ImGui::PopStyleVar();
		}

		std::string Value(int wheel, const Place::Info& i)
		{
			char b[32];
			switch (wheel) {
			case Place::kSize:
				std::snprintf(b, sizeof(b), "%.2fx", i.scale);
				break;
			case Place::kRotate:
				std::snprintf(b, sizeof(b), "%+.0f deg", i.rotate);
				break;
			case Place::kTilt:
				std::snprintf(b, sizeof(b), "%+.0f deg", i.tilt);
				break;
			case Place::kTurn:
				std::snprintf(b, sizeof(b), "%+.0f deg", i.turn);
				break;
			default:
				if (i.surface != 0) {
					std::snprintf(b, sizeof(b), "%+.1f", i.depth);
				} else {
					std::snprintf(b, sizeof(b), "%.0f", i.depth);
				}
				break;
			}
			return b;
		}

		// While placing: where it is going, what the wheel does, and the keys.
		void DrawPlacingHud(State& s, float W, float H, float appear)
		{
			const float rise = (1.0f - appear) * (1.0f - appear);  // slides up into place
			const float      k = H / 1080.0f;
			ImDrawList*      dl = ImGui::GetForegroundDrawList();
			const Place::Info info = Place::GetInfo();

			// status line
			static constexpr std::array<const char*, 4> kOn{ "AIM AT A WALL, FLOOR OR CEILING", "ON THE WALL", "ON THE FLOOR", "ON THE CEILING" };
			// one plain line: where it is, and the one key that changes that
			std::string status;
			if (info.standing) {
				status = info.surface == 2 ? "ON THE FLOOR  \xC2\xB7  R: size, turn or lift" : "LOOK AT THE FLOOR TO STAND IT THERE";
			} else if (!info.snap) {
				status = "FLOATING AHEAD OF YOU  \xC2\xB7  Q snaps it to a wall";
			} else if (info.surface == 0) {
				status = kOn[0];
			} else {
				status = std::string(kOn[std::clamp(info.surface, 0, 3)]) + (info.locked ? "  \xC2\xB7  LOCKED, F frees it" : "  \xC2\xB7  F locks it here");
			}
			std::vector<const char*> tags;  // states shown as small gold tags beside the status
			if (info.aligned) {
				tags.push_back("ALIGNED");
			}
			if (info.aligned) {  // a gold guide from this painting to the one it lines up with
				if (auto* cam = RE::Main::WorldRootCamera()) {
					float ax, ay, az, bx, by, bz;
					if (cam->WorldPtToScreenPt3(info.guideA, ax, ay, az, 1e-5f) && cam->WorldPtToScreenPt3(info.guideB, bx, by, bz, 1e-5f) && az > 0 && bz > 0) {
						dl->AddLine({ ax * W, (1 - ay) * H }, { bx * W, (1 - by) * H }, IM_COL32(214, 186, 120, 200), 2.0f * k);
						dl->AddCircleFilled({ bx * W, (1 - by) * H }, 5 * k, kGold);
					}
				}
			}
			const char* depthName = info.surface == 1 ? "Away from wall" : info.surface != 0 ? "Lift" : "Distance";
			const std::array<const char*, Place::kWheelCount> names{ "Size", "Spin", "Turn", "Tilt", depthName };

			const float pw = 940 * k, ph = 176 * k;
			const float x0 = (W - pw) * 0.5f, y0 = H - ph - 40 * k + rise * 60 * k;
			dl->AddRectFilled({ x0, y0 }, { x0 + pw, y0 + ph }, kPanel, 6 * k);
			dl->AddRect({ x0, y0 }, { x0 + pw, y0 + ph }, IM_COL32(236, 234, 226, 60), 6 * k);
			Ornament(dl, { x0, y0 }, { x0 + pw, y0 + ph }, k);
			{  // status, centred with its tags
				float tw = TextW(fontBody, status.c_str());
				for (const char* t : tags) {
					tw += 12 * k + TextW(fontSmall, t) * 0.85f + 16 * k;
				}
				float tx = (W - tw) * 0.5f;
				dl->AddText(fontBody, fontBody->LegacySize, { tx, y0 + 12 * k }, info.surface != 0 || !info.snap ? kAccent : kGold, status.c_str());
				tx += TextW(fontBody, status.c_str()) + 12 * k;
				for (const char* t : tags) {
					const float w = TextW(fontSmall, t) * 0.85f + 16 * k;
					dl->AddRectFilled({ tx, y0 + 15 * k }, { tx + w, y0 + 39 * k }, kSel, 12 * k);
					dl->AddText(fontSmall, fontSmall->LegacySize * 0.85f, { tx + 8 * k, y0 + 18 * k }, kSelInk, t);
					tx += w + 12 * k;
				}
			}
			{  // left: how high it hangs (gold at eye level); right: its real size
				if (info.floorGap > 0) {
					const bool  eye = std::abs(info.floorGap - 100.0f) < 8.0f;  // a gallery hangs centres about 1.45 m up
					std::string h = Metres(info.floorGap) + " above the floor";
					dl->AddText(fontSmall, fontSmall->LegacySize, { x0 + 30 * k, y0 + 17 * k }, eye ? kGold : kDim, h.c_str());
					if (eye) {
						dl->AddText(fontSmall, fontSmall->LegacySize, { x0 + 30 * k + TextW(fontSmall, h.c_str()) + 10 * k, y0 + 17 * k }, kGold,
							"\xC2\xB7 eye level");
					}
				}
				if (info.width > 0) {
					const std::string sz = Metres(info.width).substr(0, Metres(info.width).size() - 2) + "\xC3\x97 " + Metres(info.height);
					dl->AddText(fontSmall, fontSmall->LegacySize, { x0 + pw - 30 * k - TextW(fontSmall, sz.c_str()), y0 + 17 * k }, kDim, sz.c_str());
				}
			}
			Rule(dl, x0 + 30 * k, x0 + pw - 30 * k, y0 + 48 * k, IM_COL32(236, 234, 226, 110));

			// wheel: four settings, the active one lit
			std::vector<int> wheels;
			for (int w = 0; w < Place::kWheelCount; ++w) {
				if (!(info.standing && (w == Place::kRotate || w == Place::kTilt))) {  // a standing display: never spun or tilted
					wheels.push_back(w);
				}
			}
			const int   nw = static_cast<int>(wheels.size());
			const float segW = 170 * k, segH = 56 * k, segY = y0 + 62 * k;
			const float sx0 = (W - nw * segW - (nw - 1) * 10 * k) * 0.5f;
			for (int wi = 0; wi < nw; ++wi) {
				const int    w = wheels[wi];
				const float  x = sx0 + wi * (segW + 10 * k);
				const bool   on = w == info.wheel;
				const ImVec2 a{ x, segY }, b{ x + segW, segY + segH };
				dl->AddRectFilled(a, b, on ? IM_COL32(236, 232, 222, 40) : IM_COL32(255, 255, 255, 10), 4 * k);
				dl->AddRect(a, b, on ? kGold : IM_COL32(236, 234, 226, 50), 4 * k);
				dl->AddText(fontSmall, fontSmall->LegacySize, { x + 12 * k, segY + 5 * k }, on ? kInk : kDim, names[w]);
				const std::string v = Value(w, info);
				dl->AddText(fontBody, fontBody->LegacySize, { x + segW - TextW(fontBody, v.c_str()) - 12 * k, segY + 26 * k }, on ? kInk : kDim, v.c_str());
			}

			// keys
			std::array<std::string, kActionCount> cap;  // the keys as the player has them
			for (int a = 0; a < kActionCount; ++a) {
				cap[a] = ActionKey(a);
			}
			const std::string placeCap = "Click / " + cap[aPlace];
			std::vector<std::pair<const char*, const char*>> keys{ { placeCap.c_str(), s.isNew ? "place" : "put here" }, { "Right-click", "cancel" },
				{ "Wheel", "adjust" }, { cap[aWheel].c_str(), "next setting" }, { cap[aPanel].c_str(), "gallery" } };
			if (s.pad) {
				keys = { { "A", s.isNew ? "place" : "put here" }, { "B", "cancel" }, { "LT / RT", "adjust" }, { "X", "next setting" }, { "Y", "gallery" } };
			}
			// tools: toggles light up gold while they are on
			std::vector<Toggle> keys2{ { cap[aLock].c_str(), "lock spot", info.locked }, { cap[aSnap].c_str(), "snap", info.snap },
				{ cap[aAlign].c_str(), "align", info.align }, { cap[aGrid].c_str(), "grid", info.grid }, { cap[aReset].c_str(), "reset this", false },
				{ "Ctrl+Z", "undo", false } };
			if (info.locked) {
				keys2.insert(keys2.begin() + 1, { "Arrows", "slide", false });
			}
			if (info.standing) {  // on the floor: no snap, lock or align to choose
				keys2 = { { cap[aGrid].c_str(), "grid", info.grid }, { cap[aReset].c_str(), "reset this", false }, { "Ctrl+Z", "undo", false } };
			}
			if (s.pad) {
				keys2 = { { "D-pad left", "lock", info.locked }, { "D-pad right", "snap", info.snap }, { "D-pad up", "align", info.align },
					{ "D-pad down", "grid", info.grid }, { "RB", "reset this", false }, { "Back", "undo", false } };
			}
			if (PicInfo(s.sel[kPicture]).video) {  // a video: mute and pause while placing
				auto* cur = Place::Current();
				keys2.push_back({ cap[aMute].c_str(), "mute", Player::IsMuted(cur) });
				keys2.push_back({ cap[aPause].c_str(), "pause", Player::IsHeld(cur) });
			}
			if (!s.isNew) {
				keys2.push_back({ cap[aTakeDown].c_str(), "take down", false });
			}
			HintRow(dl, W * 0.5f, segY + segH + 12 * k, keys, k);
			ToggleRow(dl, W * 0.5f, segY + segH + 44 * k, keys2, k);
		}

		// The key the player has Activate on (keyboard or controller), as a short label for a key cap.
		std::string ActivateKeyName(bool a_pad)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			auto* ue = RE::UserEvents::GetSingleton();
			if (!cm || !ue) {
				return a_pad ? "A" : "E";
			}
			const auto code = cm->GetMappedKey(ue->activate, a_pad ? RE::INPUT_DEVICE::kGamepad : RE::INPUT_DEVICE::kKeyboard);
			if (a_pad) {
				static const std::map<std::uint32_t, const char*> pad{ { 0x1000, "A" }, { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" },
					{ 0x0100, "LB" }, { 0x0200, "RB" }, { 0x0009, "LT" }, { 0x000a, "RT" }, { 0x0040, "LS" }, { 0x0080, "RS" },
					{ 0x0001, "Up" }, { 0x0002, "Down" }, { 0x0004, "Left" }, { 0x0008, "Right" }, { 0x0010, "Start" }, { 0x0020, "Back" } };
				const auto it = pad.find(code);
				return it != pad.end() ? it->second : "A";
			}
			// DirectInput scan codes: letters and digits by their row, then the usual named keys
			static const char* rows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
			static const std::uint32_t starts[] = { 0x10, 0x1E, 0x2C };
			for (int r = 0; r < 3; ++r) {
				if (code >= starts[r] && code < starts[r] + std::strlen(rows[r])) {
					return std::string(1, rows[r][code - starts[r]]);
				}
			}
			if (code >= 0x02 && code <= 0x0B) {
				return std::string(1, "1234567890"[code - 0x02]);
			}
			if (code >= 0x3B && code <= 0x44) {
				return "F" + std::to_string(code - 0x3A);
			}
			static const std::map<std::uint32_t, const char*> named{ { 0x39, "Space" }, { 0x1C, "Enter" }, { 0x0F, "Tab" }, { 0x2A, "Shift" },
				{ 0x1D, "Ctrl" }, { 0x38, "Alt" }, { 0xD2, "Ins" }, { 0xD3, "Del" }, { 0xC7, "Home" }, { 0xCF, "End" }, { 0xC9, "PgUp" },
				{ 0xD1, "PgDn" }, { 0x100, "Mouse 1" }, { 0x101, "Mouse 2" }, { 0x102, "Mouse 3" }, { 0x103, "Mouse 4" }, { 0x104, "Mouse 5" } };
			const auto it = named.find(code);
			return it != named.end() ? it->second : "E";
		}

		// Looking at a painting with a hidden stash: a quiet prompt under the crosshair, like the game's own.
		void DrawStashPrompt(const State& s, float W, float H, float appear)
		{
			const float k = H / 1080.0f;
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const std::string keyName = ActivateKeyName(s.pad);
			const char*       key = keyName.c_str();
			const std::string name = Extras::StashName(Extras::StashInView().get().get());
			const std::string label = "Open " + (name.empty() ? std::string("hidden stash") : name);
			const char*       text = label.c_str();
			const int   alpha = static_cast<int>(255 * appear);
			const float kw = TextW(fontBody, key) + 18 * k, tw = TextW(fontBody, text);
			const float total = kw + 12 * k + tw;
			const float x = (W - total) * 0.5f, y = H * 0.56f;
			dl->AddRectFilled({ x - 18 * k, y - 8 * k }, { x + total + 18 * k, y + 38 * k }, IM_COL32(0, 0, 0, alpha * 120 / 255), 6 * k);
			dl->AddRectFilled({ x, y - 2 * k }, { x + kw, y + 32 * k }, IM_COL32(255, 255, 255, alpha * 22 / 255), 4 * k);
			dl->AddRect({ x, y - 2 * k }, { x + kw, y + 32 * k }, IM_COL32(236, 234, 226, alpha * 170 / 255), 4 * k, 0, 1.5f);
			dl->AddText(fontBody, fontBody->LegacySize, { x + 9 * k, y + 2 * k }, IM_COL32(236, 234, 226, alpha), key);
			dl->AddText(fontBody, fontBody->LegacySize, { x + kw + 12 * k + 1, y + 3 * k }, IM_COL32(0, 0, 0, alpha * 160 / 255), text);
			dl->AddText(fontBody, fontBody->LegacySize, { x + kw + 12 * k, y + 2 * k }, IM_COL32(236, 234, 226, alpha), text);
		}

		// Home was pressed: a quiet card with a turning ring until the gallery opens
		void DrawOpening(const State& s, float W, float H)
		{
			const float k = H / 1080.0f;
			const float t = std::chrono::duration<float>(Clock::now() - s.opening).count();
			const int   alpha = static_cast<int>(255 * std::clamp(t / 0.15f, 0.0f, 1.0f));
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const char* text = "Opening the gallery...";
			const float tw = TextW(fontBody, text);
			const float w = tw + 90 * k, x = (W - w) * 0.5f, y = H * 0.16f;
			dl->AddRectFilled({ x, y }, { x + w, y + 56 * k }, IM_COL32(0, 0, 0, alpha * 210 / 255), 2 * k);
			dl->AddRect({ x, y }, { x + w, y + 56 * k }, IM_COL32(240, 236, 226, alpha * 60 / 255), 2 * k);
			const ImVec2 c{ x + 32 * k, y + 28 * k };
			dl->PathArcTo(c, 11 * k, t * 6.0f, t * 6.0f + 4.4f, 24);
			dl->PathStroke(IM_COL32(240, 236, 226, alpha), 0, 2.5f * k);
			dl->AddText(fontBody, fontBody->LegacySize, { x + 56 * k, y + (56 * k - fontBody->LegacySize) * 0.5f }, IM_COL32(236, 234, 226, alpha), text);
		}

		void DrawToast(const State& s, float W, float H)
		{
			const auto now = Clock::now();
			if (s.toast.empty() || now >= s.toastUntil) {
				return;
			}
			const float k = H / 1080.0f;
			const float left = std::chrono::duration<float>(s.toastUntil - now).count();
			const int   alpha = static_cast<int>(255 * std::clamp(left / 0.3f, 0.0f, 1.0f));
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const float tw = TextW(fontBody, s.toast.c_str());
			const float y = H * 0.18f;
			dl->AddRectFilled({ (W - tw) * 0.5f - 24 * k, y - 10 * k }, { (W + tw) * 0.5f + 24 * k, y + 38 * k }, IM_COL32(0, 0, 0, alpha * 200 / 255), 4 * k);
			dl->AddText(fontBody, fontBody->LegacySize, { (W - tw) * 0.5f, y }, IM_COL32(236, 234, 226, alpha), s.toast.c_str());
		}

		void Style(float k)
		{
			ImGuiStyle& st = ImGui::GetStyle();
			st.WindowRounding = 0;
			st.WindowBorderSize = 0;
			st.WindowPadding = { 20 * k, 16 * k };
			st.FrameRounding = 2 * k;
			st.GrabRounding = 2 * k;
			st.GrabMinSize = 16 * k;
			st.FramePadding = { 10 * k, 6 * k };
			st.ScrollbarSize = 10 * k;
			st.ScrollbarRounding = 4 * k;
			st.ItemSpacing = { 12 * k, 10 * k };
			ImVec4* c = st.Colors;
			c[ImGuiCol_WindowBg] = ImColor(0, 0, 0, 222);
			c[ImGuiCol_ChildBg] = ImColor(0, 0, 0, 0);
			c[ImGuiCol_PopupBg] = ImColor(10, 10, 10, 240);
			c[ImGuiCol_Border] = ImColor(236, 234, 226, 80);
			c[ImGuiCol_Text] = ImColor(kInk);
			c[ImGuiCol_Header] = ImColor(255, 255, 255, 22);
			c[ImGuiCol_HeaderHovered] = ImColor(255, 255, 255, 34);
			c[ImGuiCol_HeaderActive] = ImColor(255, 255, 255, 48);
			c[ImGuiCol_Button] = ImColor(30, 30, 28, 240);
			c[ImGuiCol_ButtonHovered] = ImColor(60, 60, 56, 250);
			c[ImGuiCol_ButtonActive] = ImColor(90, 90, 84, 255);
			c[ImGuiCol_FrameBg] = ImColor(255, 255, 255, 14);
			c[ImGuiCol_FrameBgHovered] = ImColor(255, 255, 255, 24);
			c[ImGuiCol_FrameBgActive] = ImColor(255, 255, 255, 32);
			c[ImGuiCol_SliderGrab] = ImColor(236, 232, 222, 220);
			c[ImGuiCol_SliderGrabActive] = ImColor(255, 252, 244, 255);
			c[ImGuiCol_ScrollbarBg] = ImColor(0, 0, 0, 0);
			c[ImGuiCol_ScrollbarGrab] = ImColor(236, 234, 226, 60);
			c[ImGuiCol_ScrollbarGrabHovered] = ImColor(236, 234, 226, 110);
		}

		void InitImGui(IDXGISwapChain* swapChain)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			swapChain->GetDesc(&desc);
			const float k = desc.BufferDesc.Height / 1080.0f;
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.MouseDrawCursor = true;
			const char* font = "C:\\Windows\\Fonts\\georgia.ttf";  // the title screen's serif
			if (!std::filesystem::exists(font)) {
				font = "C:\\Windows\\Fonts\\bahnschrift.ttf";
			}
			if (!std::filesystem::exists(font)) {
				font = "C:\\Windows\\Fonts\\segoeui.ttf";
			}
			fontBody = io.Fonts->AddFontFromFileTTF(font, 24 * k);
			fontSmall = io.Fonts->AddFontFromFileTTF(font, 19 * k);
			fontTitle = io.Fonts->AddFontFromFileTTF(font, 34 * k);
			if (!fontBody || !fontSmall || !fontTitle) {
				fontBody = fontSmall = fontTitle = io.Fonts->AddFontDefault();
			}
			Style(k);
			ImGui_ImplDX11_Init(device, context);
			lastFrame = Clock::now();
			imguiReady = true;
		}

		// Main thread: block or release game controls to match build mode.
		// Blocks exactly these controls, and gives back exactly what it took: going from one set to another
		// through ToggleControls left walking and fast travel off until the console was opened.
		void SyncControls(UEFlag want)
		{
			static std::uint32_t tookOn = 0;  // of what we block now, what was on before we took it
			if (want == blockedNow) {
				return;
			}
			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm) {
				return;
			}
			auto&               rd = cm->GetRuntimeData();
			const std::uint32_t before = rd.enabledControls.underlying();
			const std::uint32_t was = static_cast<std::uint32_t>(blockedNow), now = static_cast<std::uint32_t>(want);
			std::uint32_t       en = before | (tookOn & was);  // ours back
			tookOn = en & now;
			en &= ~now;
			blockedNow = want;
			if (en != before) {
				rd.enabledControls = static_cast<UEFlag>(en);
				RE::UserEventEnabled ev;  // as the game itself tells its listeners (the HUD, the player's controls)
				ev.newUserEventFlag = static_cast<UEFlag>(en);
				ev.oldUserEventFlag = static_cast<UEFlag>(before);
				cm->SendEvent(&ev);
			}
		}

		// Gallery mode: a quiet card, and corners around the painting you are looking at
		void DrawBrowse(const State& s, float W, float H, float appear)
		{
			const float k = H / 1080.0f;
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			auto*       aimed = Place::PickPainting(600.0f);
			auto*       cam = RE::Main::WorldRootCamera();
			if (auto* root = aimed ? aimed->Get3D() : nullptr; root && cam) {
				const RE::NiPoint3 lo = aimed->GetBoundMin(), hi = aimed->GetBoundMax();
				const RE::NiPoint3 c[4]{ { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { lo.x, lo.y, hi.z } };
				const auto&        w = root->world;
				ImVec2             p[4];
				bool               ok = true;
				for (int i = 0; i < 4 && ok; ++i) {
					float sx, sy, sz;
					ok = cam->WorldPtToScreenPt3(w.translate + w.rotate * (c[i] * w.scale), sx, sy, sz, 1e-5f) && sz > 0;
					p[i] = { sx * W, (1 - sy) * H };
				}
				if (ok) {
					for (int i = 0; i < 4; ++i) {
						const ImVec2 a = p[i], n1 = p[(i + 1) % 4], n0 = p[(i + 3) % 4];
						dl->AddLine(a, { a.x + (n1.x - a.x) * 0.18f, a.y + (n1.y - a.y) * 0.18f }, kGold, 3 * k);
						dl->AddLine(a, { a.x + (n0.x - a.x) * 0.18f, a.y + (n0.y - a.y) * 0.18f }, kGold, 3 * k);
					}
				}
			}
			const float pw = 820 * k, ph = 108 * k;
			const float x0 = (W - pw) * 0.5f, y0 = H - ph - 40 * k + (1 - appear) * 40 * k;
			dl->AddRectFilled({ x0, y0 }, { x0 + pw, y0 + ph }, kPanel, 6 * k);
			dl->AddRect({ x0, y0 }, { x0 + pw, y0 + ph }, IM_COL32(236, 234, 226, 60), 6 * k);
			Ornament(dl, { x0, y0 }, { x0 + pw, y0 + ph }, k);
			const char* title = "GALLERY MODE";
			dl->AddText(fontBody, fontBody->LegacySize, { (W - TextW(fontBody, title)) * 0.5f, y0 + 12 * k }, kGold, title);
			const char* line = aimed ? "Change this painting" : "Look at one of your paintings to change it, or hang a new one";
			dl->AddText(fontSmall, fontSmall->LegacySize, { (W - TextW(fontSmall, line)) * 0.5f, y0 + 44 * k }, aimed ? kInk : kDim, line);
			const std::string act = s.pad ? ActivateKeyName(true) : ActivateKeyName(false) + " / Click";
			const std::string newCap = ActionKey(aNew);
			std::vector<std::pair<const char*, const char*>> keys{ { act.c_str(), "change it" }, { newCap.c_str(), "new painting" },
				{ "Ctrl+Z", "undo" }, { "Esc", "leave" } };
			if (s.pad) {
				keys = { { act.c_str(), "change it" }, { "Y", "new painting" }, { "Back", "undo" }, { "B", "leave" } };
			}
			HintRow(dl, W * 0.5f, y0 + 72 * k, keys, k);
		}

		// Dev screenshot: the finished frame (game and gallery), half size, as a JPEG in Data\PersonalGallery\dev_shots
		void DevShot(IDXGISwapChain* a_swapChain)
		{
			std::string reason;
			if (!Dev::Enabled() || !device || !Dev::TakeShot(reason)) {
				return;
			}
			ID3D11Texture2D* backBuffer = nullptr;
			if (FAILED(a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
				return;
			}
			DirectX::ScratchImage frame, half, rgba;
			if (SUCCEEDED(DirectX::CaptureTexture(device, context, backBuffer, frame))) {
				const DirectX::Image* im = frame.GetImage(0, 0, 0);
				if (im->format != DXGI_FORMAT_R8G8B8A8_UNORM && im->format != DXGI_FORMAT_B8G8R8A8_UNORM &&
					SUCCEEDED(DirectX::Convert(*im, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba))) {
					im = rgba.GetImage(0, 0, 0);
				}
				if (SUCCEEDED(DirectX::Resize(*im, im->width / 2, im->height / 2, DirectX::TEX_FILTER_DEFAULT, half))) {
					im = half.GetImage(0, 0, 0);
				}
				static int n = 0;
				const auto dir = std::filesystem::current_path() / "Data" / "PersonalGallery" / "dev_shots";
				std::error_code ec;
				std::filesystem::create_directories(dir, ec);
				char name[64];
				std::snprintf(name, sizeof(name), "%03d_%s.jpg", ++n, reason.c_str());
				if (SUCCEEDED(DirectX::SaveToWICFile(*im, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_JPEG), (dir / name).c_str()))) {
					Dev::Log("shot", "{}", name);
				}
			}
			backBuffer->Release();
		}

		HRESULT WINAPI HookedPresent(IDXGISwapChain* swapChain, UINT sync, UINT flags)
		{
			if (!device) {
				swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device));
				device->GetImmediateContext(&context);
			}
			// a loading screen (fast travel, doors, loading a save): the world is being torn down and built
			// again on other threads - leave it alone, and let the videos go (they start again on their own)
			static bool wasLoading = false;
			auto*       gameUI = RE::UI::GetSingleton();
			const bool  loading = !gameUI || gameUI->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || gameUI->IsMenuOpen(RE::MainMenu::MENU_NAME);
			if (loading) {
				if (!wasLoading) {
					Player::StopAll();
					std::lock_guard l(stateLock);
					state.browse = false;  // a new place: gallery mode ends
				}
			} else {
				Dev::Timer frameWork("per-frame work", 8.0);
				if (Dev::Enabled()) {  // a hitch: the game stalled between two frames (not loading, not in a menu)
					static auto lastPresent = Clock::now();
					const auto  nowP = Clock::now();
					const auto  gap = std::chrono::duration<double, std::milli>(nowP - lastPresent).count();
					lastPresent = nowP;
					if (gap > 120.0 && !GameMenuOpen()) {
						Dev::Log("hitch", "{:.0f} ms between frames", gap);
					}
				}
				Place::Frame();                  // painting follows the crosshair, every frame
				Magic::Frame();                  // magic glows breathe, flicker and pulse
				Extras::Frame();                 // stash swings, day / night pictures
				Look::Frame();                   // finish pieces waiting for a painting's 3D, or refitted after a reload
				Apply::Frame();                  // pictures put back on paintings whose 3D reloaded
				Apply::KeepOpaque();             // ... and never faded in or out
				Player::Frame(device, context);  // living portraits
			}
			wasLoading = loading;
			auto*      ui = RE::UI::GetSingleton();
			const bool paused = GameMenuOpen();

			State                      s;
			std::array<int, kTabCount> selAtStart{};
			MouseInput                 m;
			int                        held;
			{
				std::lock_guard l(stateLock);
				s = state;
				selAtStart = state.sel;
				for (int t = 0; t < kTabCount; ++t) {  // every choice inside its range (an old save, a hand-edited style)
					if (t != kPicture && t != kNightPic) {
						s.sel[t] = std::clamp(s.sel[t], 0, std::max(0, Count(t) - 1));
					}
				}
				m = mouse;
				mouse = {};
				held = mouseHeld;
			}
			{  // the showcase sits in the open space right of the sidebar (same width as DrawMenu's)
				DXGI_SWAP_CHAIN_DESC sd{};
				swapChain->GetDesc(&sd);
				const float vw = std::max(1.0f, static_cast<float>(sd.BufferDesc.Width)), vh = std::max(1.0f, static_cast<float>(sd.BufferDesc.Height));
				const float kk = vh / 1080.0f;
				const float side = PanelWidth(vw, kk);
				// on the Light page the showcase's own light goes out, so only the painting's light shows; a painting
				// already hung is shown on its wall (a new one stays in the showcase: on the Light page it used to fly
				// off to wherever the crosshair pointed, far down the street)
				Place::SetSideView(s.open && s.menu && !paused, ((side + vw) * 0.5f - vw * 0.5f) / (vw * 0.5f), (vw - side) / vw, vw / std::max(1.0f, vh),
					s.tab == pLight);
			}
			UEFlag want = UEFlag::kNone;
			if (!paused && s.open) {
				want = s.menu ? kMenuBlocked : gateInstalled ? UEFlag::kNone : kPlaceBlocked;
			} else if (!paused && s.browse) {
				want = gateInstalled ? UEFlag::kNone : kPlaceBlocked;  // E and clicks are ours; you walk and look around
			} else if (!paused && held > 0 && blockedNow != UEFlag::kNone) {
				want = kPlaceBlocked;  // the click that placed it must not also swing a sword
			}
			SyncControls(want);
			const bool stashPrompt = !s.open && !s.browse && !paused && Extras::StashInView().get();
			const bool opening = !s.open && Clock::now() - s.opening < std::chrono::seconds(6);
			if ((s.open || stashPrompt || opening || s.browse) && !paused) {
				if (!imguiReady) {
					InitImGui(swapChain);
				}
				texturesThisFrame = 0;
				++uiFrame;
				DXGI_SWAP_CHAIN_DESC desc{};
				swapChain->GetDesc(&desc);
				ImGuiIO&    io = ImGui::GetIO();
				const auto  now = Clock::now();
				const float W = static_cast<float>(desc.BufferDesc.Width), H = static_cast<float>(desc.BufferDesc.Height);
				io.DeltaTime = std::max(1e-4f, std::chrono::duration<float>(now - lastFrame).count());
				lastFrame = now;
				io.DisplaySize = ImVec2(W, H);
				if (cursor.x < 0) {
					cursor = { W * 0.35f, H * 0.5f };
				}
				cursor.x = std::clamp(cursor.x + m.dx, 0.0f, W - 1);
				cursor.y = std::clamp(cursor.y + m.dy, 0.0f, H - 1);
				io.AddMousePosEvent(s.menu ? cursor.x : -FLT_MAX, s.menu ? cursor.y : -FLT_MAX);
				for (auto [button, down] : m.buttons) {
					io.AddMouseButtonEvent(button, down);
				}
				if (m.wheel != 0) {
					io.AddMouseWheelEvent(0, m.wheel);
				}
				for (auto [key, down] : m.keys) {  // typing in a text box
					io.AddKeyEvent(static_cast<ImGuiKey>(key), down);
				}
				for (auto c : m.chars) {
					io.AddInputCharacter(c);
				}
				io.MouseDrawCursor = s.menu;

				if (s.menu != lastMenu || !hudShown) {
					shownAt = now;  // the panel (or the placing HUD) just came up: animate it in
				}
				lastMenu = s.menu;
				hudShown = true;
				const float appear = std::clamp(std::chrono::duration<float>(now - shownAt).count() / 0.2f, 0.0f, 1.0f);

				ImGui_ImplDX11_NewFrame();
				ImGui::NewFrame();
				if (s.menu) {
					DrawMenu(s, W, H, appear);
					if (auto* cur = Place::Current(); cur && (Apply::IsPending(cur) || !cur->Get3D())) {
						// over the preview: a turning ring while its new look is being made
						const float kk = H / 1080.0f, side = PanelWidth(W, kk);
						const ImVec2 c{ (side + W) * 0.5f - 120 * kk, 70 * kk };
						const float  t = static_cast<float>(ImGui::GetTime());
						auto*        fg = ImGui::GetForegroundDrawList();
						fg->AddRectFilled({ c.x - 30 * kk, c.y - 22 * kk }, { c.x + 240 * kk, c.y + 22 * kk }, IM_COL32(0, 0, 0, 170), 3 * kk);
						fg->PathArcTo({ c.x - 8 * kk, c.y }, 10 * kk, t * 6.0f, t * 6.0f + 4.4f, 24);
						fg->PathStroke(IM_COL32(240, 236, 226, 230), 0, 2.5f * kk);
						fg->AddText(fontSmall, fontSmall->LegacySize, { c.x + 14 * kk, c.y - fontSmall->LegacySize * 0.5f }, kInk, "Making your look...");
					}
				} else if (s.open) {
					DrawPlacingHud(s, W, H, appear);
				} else if (opening) {
					DrawOpening(s, W, H);
				} else if (s.browse) {
					DrawBrowse(s, W, H, appear);
				} else {
					DrawStashPrompt(s, W, H, appear);
				}
				DrawToast(s, W, H);
				ImGui::Render();
				{
					std::lock_guard l(stateLock);  // keep what the menu changed (tab, selection, closing)
					state.tab = s.tab;
					for (int t = 0; t < kTabCount; ++t) {  // only what the menu changed: the scripts may have set others mid-frame (undo, paste)
						if (s.sel[t] != selAtStart[t]) {
							state.sel[t] = s.sel[t];
						}
					}
					state.picFilter = s.picFilter;
					state.shape = s.shape;
					state.album = s.album;
					std::memcpy(state.search, s.search, sizeof(s.search));
					std::memcpy(state.title, s.title, sizeof(s.title));
					std::memcpy(state.plateFont, s.plateFont, sizeof(s.plateFont));
					state.plateStyle = s.plateStyle;
					std::memcpy(state.stashName, s.stashName, sizeof(s.stashName));  // or the Safe name box undid every key
					state.picks = s.picks;
					state.typing = s.menu && io.WantTextInput;
					state.focus = s.focus;
					std::memcpy(state.styleName, s.styleName, sizeof(s.styleName));
					if (!s.toast.empty() && s.toastUntil != state.toastUntil) {
						state.toast = s.toast;
						state.toastUntil = s.toastUntil;
					}
					state.cols = s.cols;
					state.scrollToSel = s.scrollToSel;
					if (!s.menu) {
						state.menu = false;
					}
				}

				ID3D11Texture2D* backBuffer = nullptr;
				if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
					ID3D11RenderTargetView* rtv = nullptr;
					if (SUCCEEDED(device->CreateRenderTargetView(backBuffer, nullptr, &rtv))) {
						ID3D11RenderTargetView* oldRtv = nullptr;
						ID3D11DepthStencilView* oldDsv = nullptr;
						context->OMGetRenderTargets(1, &oldRtv, &oldDsv);
						context->OMSetRenderTargets(1, &rtv, nullptr);
						ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
						context->OMSetRenderTargets(1, &oldRtv, oldDsv);
						if (oldRtv) {
							oldRtv->Release();
						}
						if (oldDsv) {
							oldDsv->Release();
						}
						rtv->Release();
					}
					backBuffer->Release();
				}
			}
			if (!s.open) {
				hudShown = false;
			}
			DevShot(swapChain);
			return originalPresent(swapChain, sync, flags);
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_events || gateDispatching) {  // (the gate already gave us these)
					return RE::BSEventNotifyControl::kContinue;
				}
				if (GameMenuOpen()) {
					std::lock_guard l(stateLock);
					mouseHeld = 0;  // a game menu has the mouse: whatever was held, its release won't reach us
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* e = *a_events; e; e = e->next) {
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
						continue;  // keyboard and mouse only: controller support was unreliable and has been taken out
					}
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
						auto* mm = static_cast<RE::MouseMoveEvent*>(e);
						std::lock_guard l(stateLock);
						if (state.open && state.menu) {
							mouse.dx += mm->mouseInputX * 1.2f;
							mouse.dy += mm->mouseInputY * 1.2f;
						}
						continue;
					}
					auto* b = e->AsButtonEvent();
					if (!b) {
						continue;
					}
					const auto raw = b->GetIDCode();  // the key as pressed (typing, held keys, the gallery key) ...
					const auto key = b->GetDevice() == RE::INPUT_DEVICE::kKeyboard ? BuiltInKey(raw) : raw;  // ... and as its action's built-in key
					bool       open, menu;
					{
						std::lock_guard l(stateLock);
						open = state.open;
						menu = state.menu;
					}
					if (b->GetDevice() == RE::INPUT_DEVICE::kMouse) {
						SetPad(false);
						Mouse(key, b, open, menu);
						continue;
					}
					if (b->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
						continue;  // (see above)
					}
					if (b->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
						continue;
					}
					if (key == 87 && b->IsDown() && Dev::Enabled()) {  // F11: a dev screenshot, any time
						Dev::RequestShot("manual", true);
						continue;
					}
					if (key == kKeyLCtrl || key == kKeyRCtrl) {
						ctrl = b->IsPressed();
					} else if (key == kKeyLShift || key == kKeyRShift) {
						shift = b->IsPressed();
					}
					if (!open) {
						if (Browsing()) {
							if (b->IsDown()) {
								SetPad(false);
								BrowseKey(key, IsActivate(b), false, ctrl);
							}
							continue;
						}
						if (b->IsDown() && IsActivate(b)) {
							SetPad(false);
							OpenStashInView();
						}
						if (b->IsDown()) {  // the gallery key: say so at once - the scripts take a moment to open it
							std::lock_guard l(stateLock);
							if (key == static_cast<std::uint32_t>(state.menuKey) && !state.browse) {
								state.opening = Clock::now();
							}
						}
						continue;
					}
					if (b->IsDown()) {
						SetPad(false);
					}
					if (!menu) {
						Place::KeyState(raw, b->IsPressed());  // held keys (Ctrl, arrows, the old , . [ ] - = keys)
					}
					bool typing;
					{
						std::lock_guard l(stateLock);
						typing = menu && state.typing;
					}
					// A key let go always reaches the text box's side, typing or not. (Enter ends the typing as it goes
					// down; its release then came after typing had stopped and was never passed on, so the menu took
					// Enter as held for good - and every later text box closed itself a moment after it was clicked.)
					if (typing || b->IsUp()) {
						Type(raw, b);
						if (typing) {
							continue;
						}
					}
					if (b->IsDown() && ctrl && (key == kKeyZ || key == kKeyC || key == kKeyV)) {  // undo, copy look, paste look
						SendSelect(key == kKeyZ ? "undo" : key == kKeyC ? "copy" : "paste", 0);
						if (key == kKeyC) {
							std::lock_guard l(stateLock);
							Toast(state, "Look copied");
						}
						continue;
					}
					if (!menu && key == kKeyZ && !ctrl) {  // held for a moment: not just the wheel's setting - everything
						static bool all = false;
						if (b->IsUp()) {
							all = false;
						} else if (!all && b->IsPressed() && b->HeldDuration() > 0.6f) {
							all = true;
							Place::ResetAll();
							std::lock_guard l(stateLock);
							Toast(state, "Everything back: size, turn, tilt and depth");
						}
					}
					if (b->IsDown()) {
						menu ? MenuKey(key) : PlacingKey(key);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		public:  // (the input gate uses these helpers too)
			static inline bool ctrl{ false };
			static inline bool shift{ false };
			static inline Clock::time_point stickNext{};
			static inline Clock::time_point triggerNext{};

			static bool Browsing()
			{
				std::lock_guard l(stateLock);
				return state.browse;
			}

			// Gallery mode: Activate (or a click) changes the painting in view, N hangs a new one, Esc leaves
			static void BrowseKey(std::uint32_t key, bool activate, bool pad, bool ctrlDown)
			{
				std::string toast;
				if (activate) {
					if (auto* ref = Place::PickPainting(600.0f)) {
						SKSE::ModCallbackEvent ev{ "PG_Browse", "edit", 0.0f, ref };
						SKSE::GetModCallbackEventSource()->SendEvent(&ev);
					} else {
						toast = "Look at one of your paintings to change it";
					}
				} else if (pad ? key == static_cast<std::uint32_t>(Pad::kY) : key == kKeyN) {
					SKSE::ModCallbackEvent ev{ "PG_Browse", "new", 0.0f, nullptr };
					SKSE::GetModCallbackEventSource()->SendEvent(&ev);
				} else if (pad ? key == static_cast<std::uint32_t>(Pad::kB) : key == kKeyEsc) {
					std::lock_guard l(stateLock);
					state.browse = false;
					return;
				} else if (pad ? key == static_cast<std::uint32_t>(Pad::kBack) : (key == kKeyZ && ctrlDown)) {
					SendSelect("undo", 0);
				}
				if (!toast.empty()) {
					std::lock_guard l(stateLock);
					Toast(state, std::move(toast));
				}
			}

			// the player's Activate, wherever they have mapped it
			static bool IsActivate(RE::ButtonEvent* a_button)
			{
				auto* ue = RE::UserEvents::GetSingleton();
				return ue && a_button->QUserEvent() == ue->activate;
			}

			// Activate on a painting with a hidden stash: the scripts move it aside and open the safe.
			static void OpenStashInView()
			{
				auto ref = Extras::StashInView().get();
				if (!ref) {
					return;
				}
				SKSE::ModCallbackEvent ev{ "PG_Stash", "", 0.0f, ref.get() };
				SKSE::GetModCallbackEventSource()->SendEvent(&ev);
			}

			static void SetPad(bool a_pad)
			{
				std::lock_guard l(stateLock);
				state.pad = a_pad;
			}

			// A text box has the keyboard: keys and characters go to it (US layout).
			static void Type(std::uint32_t key, RE::ButtonEvent* b)
			{
				static const std::map<std::uint32_t, ImGuiKey> keys{ { kKeyBack, ImGuiKey_Backspace }, { kKeyEnter, ImGuiKey_Enter },
					{ kKeyPadEnter, ImGuiKey_Enter }, { kKeyLeft, ImGuiKey_LeftArrow }, { kKeyRight, ImGuiKey_RightArrow },
					{ kKeyHome, ImGuiKey_Home }, { kKeyEnd, ImGuiKey_End }, { kKeyDelete, ImGuiKey_Delete }, { kKeyEsc, ImGuiKey_Escape },
					{ kKeyTab, ImGuiKey_Escape } };
				std::lock_guard l(stateLock);
				if (auto it = keys.find(key); it != keys.end()) {
					if (b->IsDown() || b->IsUp()) {
						mouse.keys.emplace_back(static_cast<int>(it->second), b->IsDown());
					}
					return;
				}
				if (!b->IsDown()) {
					return;
				}
				static constexpr const char* lower = "\0\0" "1234567890-=\0\0" "qwertyuiop[]\0\0" "asdfghjkl;'`\0\\" "zxcvbnm,./";
				static constexpr const char* upper = "\0\0" "!@#$%^&*()_+\0\0" "QWERTYUIOP{}\0\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?";
				if (key == 57) {
					mouse.chars.push_back(' ');
				} else if (key < 54 && (shift ? upper : lower)[key]) {
					mouse.chars.push_back(static_cast<unsigned char>((shift ? upper : lower)[key]));
				}
			}

			static void Mouse(std::uint32_t key, RE::ButtonEvent* b, bool open, bool menu)
			{
				if (key <= 1 && (b->IsDown() || b->IsUp())) {
					// one bit a button (a count went wrong for good when a release was missed, and the controls stayed blocked)
					std::lock_guard l(stateLock);
					mouseHeld = b->IsDown() ? (mouseHeld | (1 << key)) : (mouseHeld & ~(1 << key));
				}
				if (!open) {
					if (key == 0 && b->IsDown() && Browsing()) {  // gallery mode: a click changes the painting you look at
						BrowseKey(kKeyE, true, false, false);
					}
					return;
				}
				const bool wheel = key == 8 || key == 9;
				if (menu) {
					std::lock_guard l(stateLock);
					if (wheel) {
						if (b->IsDown() || b->IsPressed()) {
							mouse.wheel += key == 8 ? 1.0f : -1.0f;
						}
					} else if (key <= 2 && (b->IsDown() || b->IsUp())) {  // right-click: the panel decides (favourite, or back to placing)
						mouse.buttons.emplace_back(static_cast<int>(key), b->IsDown());
					}
					return;
				}
				if (wheel && b->IsDown()) {
					Place::Scroll(key == 8 ? 1.0f : -1.0f);
				} else if (key == 0 && b->IsDown()) {
					SendSelect("place", 0);
				} else if (key == 1 && b->IsDown()) {
					SendSelect("cancel", 0);
				}
			}

			static void PlacingKey(std::uint32_t key)
			{
				std::string toast;
				switch (key) {
				case kKeyE:
				case kKeyEnter:
				case kKeyPadEnter:
					SendSelect("place", 0);
					return;
				case kKeyBack:
				case kKeyEsc:  // Esc while placing: put everything back, as Backspace does
					SendSelect("cancel", 0);
					return;
				case kKeyR:
					{
						static constexpr const char* names[]{ "Wheel: size", "Wheel: spin", "Wheel: turn", "Wheel: tilt", "Wheel: depth" };
						toast = names[Place::NextWheel()];
						break;
					}
				case kKeyF:
				case kKeyL:
					toast = Place::ToggleLock() ? "Locked - arrows slide it" : (Place::GetInfo().snap ? "Following your aim" : "Lock works when snapped");
					break;
				case kKeyQ:
				case kKeyEnd:
					toast = Place::ToggleSnap() ? "Snap to surfaces" : "Free placement";
					break;
				case kKeyZ:
				case kKeyBackslash:
					{  // only the box you are in goes back to its default
						static constexpr const char* names[]{ "Size: back to 1x", "Spin: back to 0", "Turn: back to 0", "Tilt: back to 0",
							"Depth: back to default" };
						toast = names[Place::ResetWheel()];
					}
					break;
				case kKeyT:
					toast = Place::ToggleAlign() ? "Align with nearby paintings: on" : "Align: off";
					break;
				case kKeyG:
					toast = Place::ToggleGrid() ? "Grid steps: on" : "Grid steps: off";
					break;
				case kKeyM:  // a video painting: mute while placing
					{
						const int m = Player::ToggleMute(Place::Current());
						toast = m < 0 ? "Only video paintings have sound" : m ? "Muted while placing" : "Sound on";
					}
					break;
				case kKeyP:  // a video painting: pause while placing
					{
						const int p = Player::TogglePause(Place::Current());
						toast = p < 0 ? "Only video paintings play" : p ? "Paused while placing" : "Playing";
					}
					break;
				case kKeyX:
					{
						bool isNew;
						{
							std::lock_guard l(stateLock);
							isNew = state.isNew;
						}
						if (!isNew) {
							SendSelect("takedown", 0);
						}
						return;
					}
				default:
					break;
				}
				std::lock_guard l(stateLock);
				if (key == kKeyTab || key == static_cast<std::uint32_t>(state.menuKey)) {
					state.menu = true;
					state.scrollToSel = true;
					return;
				}
				if (!toast.empty()) {
					Toast(state, std::move(toast));
				}
			}

			// Keyboard / controller in the gallery. Up and down move between the option rows and the cards;
			// left and right change the row in focus, or move through the cards.
			static void MenuKey(std::uint32_t key)
			{
				int              tab = -1, index = 0;
				std::string_view what;
				{
					std::lock_guard l(stateLock);
					auto&           s = state;
					if (key == kKeyTab || key == static_cast<std::uint32_t>(s.menuKey) || key == kKeyEnter || key == kKeyPadEnter) {
						s.menu = false;  // done choosing: place it
						return;
					}
					const auto& page = kPages[s.tab];
					const int   rows = static_cast<int>(page.rows.size());
					const int   cards = page.cards;
					if (key == kKeyEsc) {  // Esc in the gallery: back to placing (Esc again cancels)
						s.menu = false;
						return;
					}
					if (key == kKeyBack) {
						what = "cancel";
					} else if (key == kKeyQ || key == kKeyE || key == kKeyPgUp || key == kKeyPgDn) {
						// the rail's pages that apply to this painting, in order (Video stays in: whether the picture is one
						// is the render thread's to know)
						std::vector<int> order;
						for (int pg = 0; pg < kPageCount; ++pg) {
							if (pg == s.tab || !((pg == pFrame && !UsesFrame(s.sel[kDisplay])) || (pg == pStash && !Catalog::HangsOnWall(s.sel[kDisplay])))) {
								order.push_back(pg);
							}
						}
						const int  at = static_cast<int>(std::find(order.begin(), order.end(), s.tab) - order.begin());
						const bool next = key == kKeyE || key == kKeyPgDn;
						const int  n = static_cast<int>(order.size());
						s.tab = order[(std::min(at, n - 1) + (next ? 1 : n - 1)) % n];
						s.focus = kPages[s.tab].cards >= 0 ? -1 : 0;
						s.scrollToSel = true;
						return;
					} else if (s.focus >= 0 && s.focus < rows) {  // a row has focus
						const int sel = page.rows[s.focus].first;
						if (key == kKeyUp || key == kKeyDown) {  // to the next row that is shown
							const int step = key == kKeyUp ? -1 : 1;
							int       f = s.focus + step;
							while (f >= 0 && f < rows && !RowShown(s, page.rows[f].first)) {
								f += step;
							}
							if (f >= 0 && f < rows) {
								s.focus = f;
							} else if (step > 0 && cards >= 0) {
								s.focus = -1;
							}
							return;
						}
						if (!RowShown(s, sel) || (key != kKeyLeft && key != kKeyRight)) {
							return;  // never change what can't be seen; and only left and right change a row
						}
						const int n = Count(sel);
						s.sel[sel] = std::clamp(s.sel[sel] + (key == kKeyRight ? 1 : -1), 0, n - 1);
						tab = sel;
						index = s.sel[sel];
						what = kTabKeys[sel];
					} else if (cards < 0 && rows > 0) {  // a page of rows only, nothing in focus yet: the first row shown
						int f = 0;
						while (f < rows - 1 && !RowShown(s, page.rows[f].first)) {
							++f;
						}
						s.focus = f;
						return;
					} else if (cards >= 0 && (key == kKeyLeft || key == kKeyRight || key == kKeyUp || key == kKeyDown)) {
						std::uint64_t gen = 0;
						const auto    list = Visible(cards, s, cards == kPicture ? Pictures::Snapshot(gen) : std::vector<Pictures::Info>{});
						if (list.empty()) {
							return;
						}
						const int n = static_cast<int>(list.size());
						const int slot = cards == kPicture && s.pickNight && s.sel[kDayNight] == 1 ? kNightPic : cards;  // which choice the cards set
						int       at = static_cast<int>(std::find(list.begin(), list.end(), s.sel[slot]) - list.begin());
						if (at >= n) {
							at = 0;
						}
						if (key == kKeyUp && at < s.cols && rows > 0) {  // from the top row of cards up into the options
							int f = rows - 1;
							while (f > 0 && !RowShown(s, page.rows[f].first)) {
								--f;
							}
							s.focus = f;
							return;
						}
						const int step = key == kKeyLeft ? -1 : key == kKeyRight ? 1 : key == kKeyUp ? -s.cols : s.cols;
						at = std::clamp(at + step, 0, n - 1);
						s.sel[slot] = list[at];
						s.scrollToSel = true;
						tab = slot;
						index = s.sel[slot];
						what = kTabKeys[tab];
					} else {
						return;
					}
				}
				SendSelect(what, index);
				if (tab == kStashOpen && index > 0) {  // show how it will open, as a click does
					Extras::PreviewSwing(Place::Current(), index - 1);
				}
			}
		};
	}

	// While building, every key and button reaches the gallery alone: the game and other mods' hotkeys (SKSE
	// RegisterForKey, other input sinks) don't fire. Hooks the one call the game dispatches input through; if that
	// call isn't where Address Library says (another game version), it is left alone and only the game's own
	// controls are held back, as before.
	struct InputGate
	{
		static inline InputSink sink;

		static void Thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events)
		{
			// the gallery panel: its keys go nowhere else - unless a game menu has come up over it (a message box,
			// the console, another mod's menu), which must still get its clicks and keys or the game is stuck
			if (a_events && *a_events && MenuOpen() && !GameMenuOpen()) {
				sink.ProcessEvent(a_events, a_source);
				// The game and the other mods still get their turn, with one event that means nothing (no key, not
				// pressed, not let go, no control's name) in place of ours. (They used to get an empty list. The game
				// copes with one; another input mod that reads the first event without looking crashed on it.)
				// (The list is ours to hand over and theirs to change: an input mod may put its own events in front of
				// ours or take ours out - InputManager writes to the list, and crashed when it was read-only. So it
				// is writable, and set up afresh each time from an event we keep hold of ourselves: whatever was
				// written into it last time is never read back.)
				static RE::ButtonEvent* nothing = nullptr;
				static RE::InputEvent*  quiet[2]{ nullptr, nullptr };
				if (!nothing) {
					nothing = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, "", 0xFF, 0.0f, 0.0f);
				}
				if (nothing) {
					nothing->next = nullptr;
					quiet[0] = nothing;
					quiet[1] = nullptr;
					Original(a_source, quiet);
				}
				return;
			}
			bool placing = false, browsing = false;
			{
				std::lock_guard l(stateLock);
				placing = state.open;
				browsing = state.browse;
			}
			auto* ui = RE::UI::GetSingleton();
			if (a_events && *a_events && (placing || browsing) && ui && !GameMenuOpen()) {
				// placing / gallery mode: we see every key first, then the game gets them without ours -
				// walking, looking, sprinting and the rest work as ever
				sink.ProcessEvent(a_events, a_source);
				for (auto* e = *a_events; e; e = e->next) {
					if (auto* b = e->AsButtonEvent(); b && Ours(b, placing)) {
						b->userEvent = "";  // no game control answers an unnamed event
					}
				}
				gateDispatching = true;
				Original(a_source, a_events);
				gateDispatching = false;
				return;
			}
			Original(a_source, a_events);
		}

		static inline REL::Relocation<decltype(Thunk)> Original;

		// the keys placing (or gallery mode) answers to: the game must not also act on them
		static bool Ours(RE::ButtonEvent* b, bool placing)
		{
			const auto key = b->GetDevice() == RE::INPUT_DEVICE::kKeyboard ? BuiltInKey(b->GetIDCode()) : b->GetIDCode();
			switch (b->GetDevice()) {
			case RE::INPUT_DEVICE::kMouse:
				if (!placing) {  // gallery mode: a click is ours only on a painting (doors and chests still open)
					return key == 0 && Place::PickPainting(600.0f);
				}
				return key <= 2 || key == 8 || key == 9;  // clicks (no sword swings) and the wheel
			case RE::INPUT_DEVICE::kGamepad:
				return false;  // the gallery takes no controller buttons
			case RE::INPUT_DEVICE::kKeyboard:
				break;
			default:
				return false;
			}
			if (!placing) {  // gallery mode: Activate only on a painting; N, Esc and Ctrl+Z always
				if (InputSink::IsActivate(b) || key == kKeyE) {
					return Place::PickPainting(600.0f) != nullptr;
				}
				return key == kKeyEsc || key == kKeyN || key == kKeyZ;
			}
			if (InputSink::IsActivate(b) || key == kKeyE || key == kKeyEsc || key == kKeyN || key == kKeyZ || key == kKeyLCtrl ||
				key == kKeyRCtrl) {
				return true;
			}
			static constexpr std::uint32_t keys[]{ kKeyEnter, kKeyPadEnter, kKeyBack, kKeyTab, kKeyR, kKeyF, kKeyL, kKeyQ, kKeyEnd,
				kKeyBackslash, kKeyT, kKeyG, kKeyC, kKeyV, kKeyX, kKeyM, kKeyP, kKeyUp, kKeyDown, kKeyLeft, kKeyRight, kKeyPgUp,
				kKeyPgDn, kKeyDelete, 210 /* Insert */, 26 /* [ */, 27 /* ] */, 51 /* , */, 52 /* . */, 12 /* - */, 13 /* = */ };
			return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> target{ REL::RelocationID(67315, 68617), 0x7B };
			if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
				SKSE::log::warn("input dispatch not found - other mods' hotkeys stay live while building");
				return;
			}
			SKSE::AllocTrampoline(14);
			Original = SKSE::GetTrampoline().write_call<5>(target.address(), Thunk);
			gateInstalled = true;
		}
	};

	void Install()
	{
		auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		auto* swapChain = window ? reinterpret_cast<IDXGISwapChain*>(window->swapChain) : nullptr;
		if (!swapChain) {
			SKSE::log::error("no swap chain - build mode UI unavailable");
			return;
		}
		void** vtable = *reinterpret_cast<void***>(swapChain);
		DWORD  old = 0;
		VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
		originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
		vtable[8] = reinterpret_cast<void*>(&HookedPresent);
		VirtualProtect(&vtable[8], sizeof(void*), old, &old);

		Dev::Log("start", "UI installed, input gate {}", gateInstalled ? "on" : "pending");
		RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&InputGate::sink);
		InputGate::Install();
	}

	void SetHotkey(int a_action, int a_key)
	{
		if (a_action >= 0 && a_action < kActionCount && a_key > 0 && a_key < 256) {
			boundKey[a_action] = static_cast<std::uint32_t>(a_key);
		}
	}

	void SetMenuKey(int a_key)
	{
		std::lock_guard l(stateLock);
		state.menuKey = a_key;
	}

	void CancelOpening()
	{
		std::lock_guard l(stateLock);
		state.opening = {};
	}

	void Browse(bool a_on)
	{
		std::lock_guard l(stateLock);
		state.browse = a_on;
		state.opening = {};
	}

	bool IsBrowsing()
	{
		std::lock_guard l(stateLock);
		return state.browse;
	}

	void SetUndoList(const std::vector<std::string>& a_items)
	{
		std::lock_guard l(stateLock);
		state.undoList.clear();
		for (const auto& i : a_items) {
			if (!i.empty()) {
				state.undoList.push_back(i);
			}
		}
	}

	void SetSlidePicks(const std::vector<std::string>& a_ids)
	{
		std::lock_guard l(stateLock);
		state.picks = { a_ids.begin(), a_ids.end() };
	}

	void Open(const std::array<int, kTabCount>& a_selection, bool a_menu, int a_menuKey, bool a_isNew, const std::string& a_title,
		const std::string& a_stashName)
	{
		std::lock_guard l(stateLock);
		state.open = true;
		state.menu = a_menu;
		if (a_isNew) {
			state.tab = 0;  // pPicture: a new painting starts with choosing its picture
		}
		state.isNew = a_isNew;
		state.sel = a_selection;
		state.menuKey = a_menuKey;
		state.scrollToSel = true;
		state.toast.clear();
		state.typing = false;
		{  // the title as the scripts keep it: its words, then (if chosen) " ##font=<face>;<letters>"
			std::string words = a_title, face, letters;
			if (const auto at = words.rfind(" ##font="); at != std::string::npos) {
				const std::string spec = words.substr(at + 8);
				words.erase(at);
				const auto semi = spec.find(';');
				face = spec.substr(0, semi);
				letters = semi == std::string::npos ? std::string() : spec.substr(semi + 1);
			}
			strncpy_s(state.title, words.c_str(), _TRUNCATE);
			strncpy_s(state.plateFont, face.c_str(), _TRUNCATE);
			state.plateStyle = 0;
			for (int bit = 0; bit < 5; ++bit) {
				if (letters.find("biusc"[bit]) != std::string::npos) {
					state.plateStyle |= 1 << bit;
				}
			}
		}
		strncpy_s(state.stashName, a_stashName.c_str(), _TRUNCATE);
		state.opening = {};
		if (!a_isNew) {
			Toast(state, "Editing this painting - Move it puts you at the wall to shift it");
		}
		mouse = {};
	}

	void Set(int a_tab, int a_index)
	{
		std::lock_guard l(stateLock);
		if (a_tab >= 0 && a_tab < kTabCount) {
			state.sel[a_tab] = a_index;
		}
	}

	void Close()
	{
		Player::ClearHolds();  // done placing: it plays and sounds as it should
		std::lock_guard l(stateLock);
		state.open = false;
		state.menu = false;
	}

	std::vector<int> GetDefaultStyle()
	{
		std::lock_guard l(styleLock);
		if (!stylesLoaded) {
			LoadStyles();
		}
		for (const auto& st : styles) {
			if (st.name == kDefaultStyle) {
				return { st.v.begin(), st.v.end() };
			}
		}
		return {};
	}

	std::vector<int> GetStyle(int a_index)
	{
		std::lock_guard l(styleLock);
		if (!stylesLoaded) {
			LoadStyles();
		}
		if (a_index >= 1000 && a_index < 1000 + static_cast<int>(kPresets.size())) {  // a ready-made look
			std::array<int, kTabCount> now;
			{
				std::lock_guard sl(stateLock);
				now = state.sel;
			}
			const auto v = PresetValues(a_index - 1000, now);
			return { v.begin(), v.end() };
		}
		if (a_index < 0 || a_index >= static_cast<int>(styles.size())) {
			return {};
		}
		return { styles[a_index].v.begin(), styles[a_index].v.end() };
	}

	bool MenuOpen()
	{
		std::lock_guard l(stateLock);
		return state.open && state.menu;
	}

	bool IsOpen()
	{
		std::lock_guard l(stateLock);
		return state.open;
	}
}
