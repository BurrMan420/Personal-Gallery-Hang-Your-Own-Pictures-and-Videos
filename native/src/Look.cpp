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

#include "Look.h"

#include <atomic>
#include <charconv>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include <DirectXTex.h>

#include "Apply.h"
#include "Catalog.h"
#include "Extras.h"
#include "Magic.h"
#include "Player.h"

namespace Look
{
	namespace
	{
		struct Wanted
		{
			RE::ObjectRefHandle ref;
			int                 o[kOptionCount]{};
			std::string         title;
			int                 tries{ 0 };
		};

		std::mutex                   lock;
		std::map<RE::FormID, Wanted> pending;
		struct Known
		{
			Wanted          w;
			RE::NiAVObject* root{ nullptr };
		};
		std::map<RE::FormID, Known> known;  // every painting's look, to refit when its 3D reloads
		int                         frameCount{ 0 };
		std::atomic<bool>            scheduled{ false };
		std::atomic<bool>            castShadows{ true };

		int Clamp(int v, size_t n) { return std::clamp(v, 0, static_cast<int>(n) - 1); }

		// --- sconces from the player's own game ------------------------------------------------------------------
		// Skyrim's wall pieces stand on the wall at x = 0 and reach out along -x; a painting's front is -y.
		struct Sconce
		{
			const char* path;     // under Data\meshes, loaded from the game itself (never shipped with the mod)
			float       wickOut;  // where its candle burns: out from the wall, and up (its own units)
			float       wickUp;
		};
		constexpr Sconce kSconces[]{ { "clutter\\imperial\\impwallsconcecandle01.nif", 10.0f, 9.0f },
			{ "clutter\\ruins\\ruinscandlesconceon01.nif", 24.0f, 30.0f } };

		bool IStarts(std::string_view a_text, std::string_view a_start)
		{
			return a_text.size() >= a_start.size() && _strnicmp(a_text.data(), a_start.data(), a_start.size()) == 0;
		}

		void StripCollision(RE::NiAVObject* a_obj)  // a prop is to look at, never to bump into
		{
			if (!a_obj) {
				return;
			}
			a_obj->collisionObject.reset();
			if (auto* node = a_obj->AsNode()) {
				for (auto& child : node->GetChildren()) {
					StripCollision(child.get());
				}
			}
		}

		RE::NiNode* LoadSconce(const char* a_path)
		{
			RE::NiPointer<RE::NiNode>                 model;
			const RE::BSModelDB::DBTraits::ArgsType args{};
			if (RE::BSModelDB::Demand(a_path, model, args) != RE::BSResource::ErrorCode::kNone || !model) {
				SKSE::log::warn("sconce model not found: {}", a_path);
				return nullptr;
			}
			auto* node = netimmerse_cast<RE::NiNode*>(model->Clone());  // the game shares the loaded one: ours is a copy
			StripCollision(node);
			return node;
		}

		// Skyrim's wall pieces reach out along -x; a painting's front is -y
		RE::NiMatrix3 WallTurn()
		{
			RE::NiMatrix3 m;
			m.entry[0][0] = 0;
			m.entry[0][1] = -1;
			m.entry[0][2] = 0;
			m.entry[1][0] = 1;
			m.entry[1][1] = 0;
			m.entry[1][2] = 0;
			m.entry[2][0] = 0;
			m.entry[2][1] = 0;
			m.entry[2][2] = 1;
			return m;
		}

		// the displays' fixed props: always there, from the player's own game
		struct Fixed
		{
			const char* kind;
			const char* path;
			bool        wall;  // stands on a wall (turned to face out) or on the floor
		};
		constexpr Fixed kFixed[]{ { "stand", "dungeons\\dwemer\\animated\\dwerepository\\dwespecstand01.nif", false },
			{ "port", "dungeons\\dwemer\\pipes\\dwepipecap01.nif", true }, { "orbstand", "clutter\\soulgem\\soulgemholderstand01.nif", false },
			{ "lens", "dungeons\\dwemer\\animated\\observatory\\armillary\\focusingcrystal01.nif", false } };

		// A marker "attach|<kind>|<side>|<scale>": mount that prop there (once per 3D).
		void MountFixed(RE::NiNode* a_root, RE::BSGeometry* a_marker)
		{
			const std::string_view rest = std::string_view(a_marker->name.c_str()).substr(7);  // after "attach|"
			const auto             b1 = rest.find('|');
			const auto             b2 = b1 == std::string_view::npos ? b1 : rest.find('|', b1 + 1);
			if (b2 == std::string_view::npos) {
				return;
			}
			const std::string_view kind = rest.substr(0, b1), side = rest.substr(b1 + 1, b2 - b1 - 1);
			float                  scale = 1.0f;
			std::from_chars(rest.data() + b2 + 1, rest.data() + rest.size(), scale);
			const Fixed* prop = nullptr;
			for (const auto& f : kFixed) {
				if (kind.size() == std::strlen(f.kind) && _strnicmp(kind.data(), f.kind, kind.size()) == 0) {
					prop = &f;
				}
			}
			if (!prop) {
				return;
			}
			const std::string name = "pgprop|" + std::string(kind) + "|" + std::string(side);
			RE::NiNode*       have = nullptr;
			for (auto& child : a_root->GetChildren()) {
				if (child && _stricmp(child->name.c_str(), name.c_str()) == 0) {
					have = child->AsNode();
				}
			}
			if (!have) {
				have = LoadSconce(prop->path);
				if (!have) {
					return;
				}
				have->name = name;
				a_root->AttachChild(have, true);
			}
			have->local.translate = a_marker->GetModelData().modelBound.center;
			if (prop->wall) {
				have->local.rotate = WallTurn();
			}
			have->local.scale = std::clamp(scale, 0.02f, 5.0f);
		}

		// One of the gallery's own textures (a frame's, a mat's), recoloured (hue 1..72, amount 0..20): made once, kept
		// in the cache. a_gain lifts a dark texture so the colour shows (frames), 1 for a light one (mats).
		std::string Recoloured(const char* a_folder, const char* a_prefix, float a_gain, const std::string& a_style, int a_hue, int a_amount)
		{
			const std::string plain = std::string("textures\\PersonalGallery\\") + a_folder + "\\" + a_style + ".dds";
			if (a_hue <= 0 || a_amount <= 0) {
				return plain;
			}
			char name[96];
			std::snprintf(name, sizeof(name), "%s_%s_h%d_t%d_b.dds", a_prefix, a_style.c_str(), a_hue, a_amount);
			const auto      dir = std::filesystem::current_path() / "Data" / "textures" / "PersonalGallery" / "Cache";
			const auto      file = dir / name;
			std::error_code ec;
			if (std::filesystem::exists(file, ec)) {
				std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);  // in use: kept by the cache sweep
			} else {
				DirectX::ScratchImage src, raw;
				const auto path = std::filesystem::current_path() / "Data" / "textures" / "PersonalGallery" / a_folder / (a_style + ".dds");
				if (FAILED(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, src)) ||
					FAILED(DirectX::Decompress(*src.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, raw))) {
					return plain;
				}
				float r, g, b;
				Catalog::HueRGB((a_hue - 1) * 5.0f, 0.8f, r, g, b);
				const float k = a_amount / 20.0f;
				const auto* im = raw.GetImage(0, 0, 0);
				// The lift is fitted to the texture: a dark one is raised so the colour shows, a light one is not. (One
				// lift for all turned the pale frames - Orcish iron in gold - into a flat neon slab.)
				bool translucent = false;
				if (a_gain > 1.0f) {
					double sum = 0;
					for (size_t y = 0; y < im->height; y += 4) {
						const auto* p = im->pixels + y * im->rowPitch;
						for (size_t x = 0; x < im->width; x += 4) {
							sum += 0.3 * p[x * 4] + 0.59 * p[x * 4 + 1] + 0.11 * p[x * 4 + 2];
						}
					}
					const double mean = sum / std::max<size_t>(1, ((im->height + 3) / 4) * ((im->width + 3) / 4));
					a_gain = std::clamp(static_cast<float>(105.0 / std::max(1.0, mean)), 0.85f, a_gain);
				}
				for (size_t y = 0; y < im->height; ++y) {
					auto* p = im->pixels + y * im->rowPitch;
					for (size_t x = 0; x < im->width; ++x, p += 4) {
						translucent |= p[3] < 250;  // its fourth channel says how it shines: kept (below)
						const float l = (0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]) * a_gain;  // keep the grain, take the colour
						const float t[3]{ l * r, l * g, l * b };
						for (int c = 0; c < 3; ++c) {
							p[c] = static_cast<std::uint8_t>(std::clamp(p[c] * (1 - k) + t[c] * k, 0.0f, 255.0f));
						}
					}
				}
				DirectX::ScratchImage mips, bc;
				std::filesystem::create_directories(dir, ec);
				if (FAILED(DirectX::GenerateMipMaps(*im, DirectX::TEX_FILTER_DEFAULT, 0, mips)) ||
					FAILED(DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), translucent ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC1_UNORM,
						DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, bc)) ||
					FAILED(DirectX::SaveToDDSFile(bc.GetImages(), bc.GetImageCount(), bc.GetMetadata(), DirectX::DDS_FLAGS_NONE, file.c_str()))) {
					return plain;
				}
			}
			return std::string("textures\\PersonalGallery\\Cache\\") + name;
		}

		std::string FrameColour(const std::string& a_style, int a_hue, int a_amount)
		{
			return Recoloured("Frames", "frame", 1.5f, a_style, a_hue, a_amount);
		}

		// Mount (or take away) the chosen sconce at a marker "attach|sconce|<side>|<size>". Returns where its
		// candle burns, in the painting's space, so the flame can be put there.
		std::optional<RE::NiPoint3> MountSconce(RE::NiNode* a_root, RE::BSGeometry* a_marker, int a_lamp, float a_xScale)
		{
			const std::string_view marker = a_marker->name.c_str();
			const auto             parts = marker.substr(std::string_view("attach|sconce|").size());
			const char             side = parts.empty() ? 'L' : parts[0];
			float                  size = 75.0f;
			if (const auto bar = parts.find('|'); bar != std::string_view::npos) {
				std::from_chars(parts.data() + bar + 1, parts.data() + parts.size(), size);
			}
			const Sconce* want = a_lamp >= 2 && a_lamp - 2 < static_cast<int>(std::size(kSconces)) ? &kSconces[a_lamp - 2] : nullptr;
			const std::string prefix = std::string("pgprop|") + side + "|";
			const std::string name = prefix + std::to_string(a_lamp);
			RE::NiNode*       have = nullptr;
			for (auto& child : a_root->GetChildren()) {
				if (child && IStarts(child->name.c_str(), prefix)) {
					if (want && _stricmp(child->name.c_str(), name.c_str()) == 0) {
						have = child->AsNode();
					} else {
						a_root->DetachChild2(child.get());  // another sconce (or none) was chosen
						break;
					}
				}
			}
			if (!want) {
				return std::nullopt;
			}
			if (!have) {
				have = LoadSconce(want->path);
				if (!have) {
					return std::nullopt;
				}
				have->name = name;
				a_root->AttachChild(have, true);
			}
			RE::NiPoint3 at = a_marker->GetModelData().modelBound.center;
			at.x *= a_xScale;  // beside the frame, however wide it is
			const float k = std::clamp(size / 110.0f, 0.3f, 1.3f);
			have->local.translate = at;
			have->local.rotate.entry[0][0] = 0;  // its -x (out of the wall) becomes the painting's -y
			have->local.rotate.entry[0][1] = -1;
			have->local.rotate.entry[0][2] = 0;
			have->local.rotate.entry[1][0] = 1;
			have->local.rotate.entry[1][1] = 0;
			have->local.rotate.entry[1][2] = 0;
			have->local.rotate.entry[2][0] = 0;
			have->local.rotate.entry[2][1] = 0;
			have->local.rotate.entry[2][2] = 1;
			have->local.scale = k;
			return RE::NiPoint3{ at.x, at.y - want->wickOut * k, at.z + want->wickUp * k };
		}

		// --- nameplate -----------------------------------------------------------------------------

		// Engraves the title onto a plaque texture (brass, wood or iron) and returns its game path.
		std::string PlateTexture(const std::string& a_title, int a_style, int a_font, int a_hue)
		{
			const std::uint64_t h = std::hash<std::string>{}(a_title + "#" + std::to_string(a_style) + "#" + std::to_string(a_font) + "#" +
															  std::to_string(a_hue));
			char                name[64];
			std::snprintf(name, sizeof(name), "plate_v4_%016llx.dds", static_cast<unsigned long long>(h));  // v4: lettering that reads on any colour, in any weight
			const auto      dir = std::filesystem::current_path() / "Data" / "textures" / "PersonalGallery" / "Cache";
			const auto      file = dir / name;
			std::error_code ec;
			if (std::filesystem::exists(file, ec)) {
				std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);  // in use: kept by the cache sweep
			} else {
				constexpr int W = 1024, H = 256;
				BITMAPINFO    bi{};
				bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
				bi.bmiHeader.biWidth = W;
				bi.bmiHeader.biHeight = -H;  // top-down
				bi.bmiHeader.biPlanes = 1;
				bi.bmiHeader.biBitCount = 32;
				void*   bits = nullptr;
				HDC     dc = CreateCompatibleDC(nullptr);
				HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				if (!dc || !bmp || !bits) {  // let go of whichever was made
					if (bmp) {
						DeleteObject(bmp);
					}
					if (dc) {
						DeleteDC(dc);
					}
					return {};
				}
				auto* old = SelectObject(dc, bmp);
				// plaque face: brushed metal (or grained wood), a soft sheen, a raised bevel lit from the top left
				// and a fine engraved border line
				static constexpr COLORREF faces[]{ RGB(184, 150, 88), RGB(118, 80, 48), RGB(96, 98, 104) };
				static constexpr COLORREF inks[]{ RGB(30, 18, 4), RGB(20, 10, 4), RGB(12, 12, 14) };  // dark enough for a light-weight font
				const int      style = std::clamp(a_style - 1, 0, 2);
				bool           lightInk = false;
				COLORREF face = faces[style];
				COLORREF ink = inks[style];
				if (a_hue > 0) {  // the plate in a colour of the player's choosing (enamel / painted)
					float r, g, b;
					Catalog::HueRGB((a_hue - 1) * 5.0f, 0.6f, r, g, b);
					face = RGB(static_cast<int>(r * 170), static_cast<int>(g * 170), static_cast<int>(b * 170));
					// lettering that reads on it: pale on a dark plate (blue, purple, red), near black on a light one
					const float lum = 0.299f * GetRValue(face) + 0.587f * GetGValue(face) + 0.114f * GetBValue(face);
					lightInk = lum < 105.0f;
					ink = lightInk ? RGB(244, 236, 214) : RGB(GetRValue(face) / 6, GetGValue(face) / 6, GetBValue(face) / 6);
				}
				auto           hash = [](std::uint32_t v) {
                    v ^= v >> 16;
                    v *= 0x7feb352dU;
                    v ^= v >> 15;
                    v *= 0x846ca68bU;
                    v ^= v >> 16;
                    return (v & 0xFFFF) / 65535.0f;
				};
				auto           smooth = [&](float u, std::uint32_t seed) {  // 1D value noise
					const int   i = static_cast<int>(std::floor(u));
					const float f = u - i, w = f * f * (3 - 2 * f);
					return hash(static_cast<std::uint32_t>(i) * 2654435761U + seed) * (1 - w) + hash(static_cast<std::uint32_t>(i + 1) * 2654435761U + seed) * w;
				};
				auto* px = static_cast<std::uint32_t*>(bits);
				constexpr int bevel = 16, line = 30;
				for (int y = 0; y < H; ++y) {
					const float t = static_cast<float>(y) / H;
					const float sheen = 0.88f + 0.2f * std::exp(-std::pow((t - 0.3f) / 0.22f, 2.0f));
					for (int x = 0; x < W; ++x) {
						float k = sheen;
						if (style == 1) {  // wood: long wavy grain
							const float g = y + 6.0f * std::sin(x * 0.006f) + 3.0f * smooth(x * 0.01f, 7);
							k *= 0.9f + 0.1f * std::sin(g * 0.35f + 4.0f * smooth(g * 0.05f, 3)) + 0.05f * (smooth(x * 0.2f + y * 13.0f, 9) - 0.5f);
						} else {  // metal: fine horizontal brushing
							k *= 1.0f + 0.05f * (smooth(x * 0.004f + y * 31.0f, 11) - 0.5f) + 0.025f * (hash(static_cast<std::uint32_t>(y * W + x)) - 0.5f);
						}
						const int l = x, tp = y, r = W - 1 - x, b = H - 1 - y;
						const int edge = std::min({ l, tp, r, b });
						if (edge < bevel) {  // the bevel: lit from the top left
							const float lit = (edge == l || edge == tp) ? 1.22f : 0.62f;
							const float f = 1.0f - static_cast<float>(edge) / bevel;
							k *= 1.0f + (lit - 1.0f) * (0.4f + 0.6f * f);
						} else if (edge == line || edge == line + 1) {  // engraved border: a dark cut ...
							k *= 0.68f;
						} else if (edge == line + 2) {  // ... with a bright lip
							k *= 1.15f;
						}
						auto c = [&](int v) { return static_cast<std::uint32_t>(std::clamp(v * k, 0.0f, 255.0f)); };
						px[y * W + x] = 0xFF000000u | (c(GetRValue(face)) << 16) | (c(GetGValue(face)) << 8) | c(GetBValue(face));
					}
				}
				SetBkMode(dc, TRANSPARENT);
				// The title may end in its lettering, " ##font=<fontFace>;<letters>" (the gallery adds it): any font on this PC,
				// with b bold, i italic, u underlined, s struck through, c spaced capitals.
				std::string  words = a_title;
				std::wstring fontFace;
				bool         bold = false, italic = false, under = false, strike = false, caps = false;
				if (const auto at = words.rfind(" ##font="); at != std::string::npos) {
					const std::string spec = words.substr(at + 8);
					words.erase(at);
					const auto        semi = spec.find(';');
					const std::string name = spec.substr(0, semi);
					const std::string letters = semi == std::string::npos ? std::string() : spec.substr(semi + 1);
					bold = letters.find('b') != std::string::npos;
					italic = letters.find('i') != std::string::npos;
					under = letters.find('u') != std::string::npos;
					strike = letters.find('s') != std::string::npos;
					caps = letters.find('c') != std::string::npos;
					fontFace.assign(name.size() + 1, L'\0');
					fontFace.resize(static_cast<size_t>(std::max(1, MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, fontFace.data(), static_cast<int>(fontFace.size())))) - 1);
					if (fontFace.empty()) {
						fontFace = L"Georgia";
					}
				}
				std::wstring text(words.size() + 1, L'\0');
				text.resize(static_cast<size_t>(std::max(1, MultiByteToWideChar(CP_UTF8, 0, words.c_str(), -1, text.data(), static_cast<int>(text.size())))) - 1);
				// engraved capitals, spaced. (The spacing used to be thin-space characters between the letters; the
				// font has no such character, so every gap was drawn as an empty box.)
				const bool spaced = fontFace.empty() ? std::clamp(a_font, 0, 4) == 4 : caps;
				if (spaced) {
					for (wchar_t& c : text) {
						c = static_cast<wchar_t>(towupper(c));
					}
				}
				int  size = 150;
				SIZE ext{};
				HFONT font = nullptr;
				for (; size > 30; size -= 6) {  // largest size that fits
					if (font) {
						DeleteObject(font);
					}
					// Lettering: fonts every Windows has (Georgia, Palatino italic, Segoe Script, Gabriola, Constantia capitals)
					static constexpr const wchar_t* faces[]{ L"Georgia", L"Palatino Linotype", L"Segoe Script", L"Gabriola", L"Constantia" };
					const int f = std::clamp(a_font, 0, 4);
					if (!fontFace.empty()) {  // the player's own choice of font and style
						font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, under, strike, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
							CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, fontFace.c_str());
					} else {  // a painting from before fonts could be chosen: its lettering style
						font = CreateFontW(-size, 0, 0, 0, f == 2 ? FW_NORMAL : FW_SEMIBOLD, f == 1, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
							CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, faces[f]);
					}
					SelectObject(dc, font);
					SetTextCharacterExtra(dc, spaced ? size / 6 : 0);
					GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &ext);
					if (ext.cx < W - 140 && ext.cy < H - 90) {  // inside the engraved border
						break;
					}
				}
				const int x = (W - ext.cx) / 2, y = (H - ext.cy) / 2;
				if (lightInk) {  // pale lettering: a dark edge under it instead of a lit one
					SetTextColor(dc, RGB(GetRValue(face) / 3, GetGValue(face) / 3, GetBValue(face) / 3));
				} else {
					SetTextColor(dc, RGB(std::min(255, GetRValue(face) + 60), std::min(255, GetGValue(face) + 60), std::min(255, GetBValue(face) + 60)));
				}
				TextOutW(dc, x + 2, y + 2, text.c_str(), static_cast<int>(text.size()));  // engraved: the cut's lit lower edge
				SetTextColor(dc, ink);
				TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
				GdiFlush();
				DirectX::ScratchImage img;
				if (SUCCEEDED(img.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, W, H, 1, 1))) {
					const auto* im = img.GetImage(0, 0, 0);
					for (int r = 0; r < H; ++r) {
						auto* row = reinterpret_cast<std::uint32_t*>(im->pixels + r * im->rowPitch);
						for (int c = 0; c < W; ++c) {
							row[c] = px[r * W + c] | 0xFF000000u;
						}
					}
					DirectX::ScratchImage mips;
					std::filesystem::create_directories(dir, ec);
					if (SUCCEEDED(DirectX::GenerateMipMaps(*im, DirectX::TEX_FILTER_DEFAULT, 0, mips))) {
						DirectX::SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DirectX::DDS_FLAGS_NONE, file.c_str());
					}
				}
				SelectObject(dc, old);
				if (font) {
					DeleteObject(font);
				}
				DeleteObject(bmp);
				DeleteDC(dc);
			}
			return std::string("textures\\PersonalGallery\\Cache\\") + name;
		}

		// --- fitting the pieces ----------------------------------------------------------------------

		struct MatInfo
		{
			bool  found{ false };
			float hw{ 0 }, hh{ 0 }, m{ 0 }, backY{ 0 }, fw{ 0 }, d{ 0 };
		};

		// "mat|hw|hh|m|backY|fw|d" (written by tools/build_gallery.py)
		MatInfo ParseMat(std::string_view a_name)
		{
			MatInfo out;
			float*  fields[]{ &out.hw, &out.hh, &out.m, &out.backY, &out.fw, &out.d };
			size_t  pos = a_name.find('|');
			for (auto* f : fields) {
				if (pos == std::string_view::npos) {
					return {};
				}
				const size_t next = a_name.find('|', pos + 1);
				const auto   part = a_name.substr(pos + 1, (next == std::string_view::npos ? a_name.size() : next) - pos - 1);
				std::from_chars(part.data(), part.data() + part.size(), *f);
				pos = next;
			}
			out.found = out.hw > 0 && out.hh > 0;
			return out;
		}

		// which of the four mat widths a "mat|...|v<n>" shape is (1, normal, when it doesn't say)
		int MatVariant(std::string_view a_name)
		{
			const auto at = a_name.rfind("|v");
			return at != std::string_view::npos && at + 2 < a_name.size() ? a_name[at + 2] - '0' : 1;
		}

		RE::NiMatrix3 Diag(float x, float y, float z)
		{
			RE::NiMatrix3 m;
			m.entry[0][0] = x;
			m.entry[1][1] = y;
			m.entry[2][2] = z;
			return m;
		}

		void Show(RE::BSGeometry* g, bool on) { g->SetAppCulled(!on); }

		// Effect-shader overlay (glass, age): own material, then strength via colour alpha.
		void Overlay(RE::BSGeometry* g, bool on, float r, float gr, float b, float a)
		{
			Show(g, on);
			if (!on) {
				return;
			}
			auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get());
			auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->material) : nullptr;
			if (!mat) {
				return;
			}
			if (auto* fresh = static_cast<RE::BSEffectShaderMaterial*>(mat->Create())) {
				fresh->CopyMembers(mat);
				prop->SetMaterial(fresh, true);
				fresh->~BSEffectShaderMaterial();
				RE::free(fresh);
			}
			if (auto* own = static_cast<RE::BSEffectShaderMaterial*>(prop->material)) {
				own->baseColor = { r, gr, b, a };
			}
		}

		// Main thread. False if the painting's 3D isn't there yet.
		bool Fit(Wanted& w)
		{
			auto  ref = w.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root) {
				return false;
			}
			const int* o = w.o;
			MatInfo    info;
			const int  matPick = Clamp(o[kMatWidth], 4);
			bool       pickedMat = false;
			bool       sawGlass = false;
			std::vector<RE::BSGeometry*> markers;  // where sconces are mounted
			std::vector<RE::BSGeometry*> flames;   // one a side, each put on its own wick
			std::vector<RE::BSGeometry*> plates;   // the nameplate's parts
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
				const std::string lowered = Catalog::Lower(g->name.c_str());
				if (const std::string_view n = lowered; n.starts_with("mat|")) {  // the chosen width's mat
					if (MatVariant(n) == matPick) {
						info = ParseMat(n);
						pickedMat = true;
					} else if (!pickedMat && !info.found) {  // older models: their one mat
						info = ParseMat(n);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			const int   width = Clamp(o[kFrameWidth], Catalog::kFrameWidths.size());
			const float dy = Catalog::kFrameDepthScales[Clamp(o[kFrameDepth], Catalog::kFrameDepths.size())];
			const bool  mat = info.found && o[kMat] > 0;
			const float sx = mat ? (info.hw + info.m) / info.hw : 1.0f;
			const float sz = mat ? (info.hh + info.m) / info.hh : 1.0f;
			// the frame's outer edge with this width and mat; lamps and nameplate move with it
			static constexpr float widthK[]{ 0.6f, 1.0f, 1.6f };
			const float outer = (info.hh + info.fw * widthK[width]) * sz;
			const float dz = info.found ? outer - (info.hh + info.fw) : 0.0f;
			const float front = info.found ? info.d * (dy - 1.0f) : 0.0f;

			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
				const std::string      lowered = Catalog::Lower(g->name.c_str());
				const std::string_view name = lowered;
				if (auto* solid = netimmerse_cast<RE::BSLightingShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
					solid->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, castShadows.load());
				}
				if (name.starts_with("frame_")) {
					const auto bar = name.find('|');
					// each width and depth is its own shape: show the chosen one, never stretch it (that turned it black)
					const auto variant = bar == std::string_view::npos ? std::string_view("normal1") : name.substr(bar + 1);
					static constexpr std::string_view keys[]{ "slim", "normal", "wide" };
					const std::string want = std::string(keys[width]) + static_cast<char>('0' + Clamp(o[kFrameDepth], 4));
					// a table frame (no mat to measure) has its one shape: never hidden or resized, but still recoloured
					const bool shown = !info.found || variant == want;
					if (info.found) {
						Show(g, shown);
					}
					if (shown) {  // its colour: the frame's own, or recoloured
						std::string style(name.substr(6, (bar == std::string_view::npos ? name.size() : bar) - 6));
						const int   hue = std::clamp(o[kFrameHue], 0, 72), amount = std::clamp(o[kFrameTint], 0, 20);
						// only when it changes: remembered per painting and per shape, with the 3D it was put on (a reloaded
						// cell has new 3D - a raw geometry pointer could be reused by another painting)
						static std::map<std::pair<RE::FormID, std::string>, std::pair<RE::NiAVObject*, int>> applied;
						const int  key = hue * 32 + amount;
						const auto id = std::make_pair(ref->GetFormID(), std::string(name));
						auto       it = applied.find(id);
						const bool fresh = it == applied.end() || it->second.first != root;
						if (fresh || it->second.second != key) {
							const bool hadColour = !fresh && it->second.second != 0;
							applied[id] = { root, key };
							if (hue > 0 || hadColour) {
								Apply::SetShapeTextures(ref.get(), name, FrameColour(style, hue, amount),
									"textures\\PersonalGallery\\Frames\\" + style + "_n.dds");
							}
						}
					}
					if (info.found) {
						g->local.rotate = Diag(sx, 1, sz);  // only widened round a mat
						g->local.translate = { 0, 0, 0 };
					}
				} else if (name == "magic_aura" && info.found) {
					g->local.rotate = Diag((info.hw + info.fw * widthK[width]) * sx / (info.hw + info.fw), 1, outer / (info.hh + info.fw));
				} else if (name.starts_with("matcore|")) {  // the mat's pale bevelled cut, with its mat
					Show(g, mat && MatVariant(name) == matPick);
				} else if (name.starts_with("mat|")) {
					const bool on = mat && MatVariant(name) == matPick;
					Show(g, on);
					if (on) {
						// the chosen board, in its own colour or the player's
						Apply::SetShapeTextures(ref.get(), name,
							Recoloured("Mats", "mat", 1.0f, std::string(Catalog::kMatKeys[Clamp(o[kMat], Catalog::kMats.size())]), std::clamp(o[kMatHue], 0, 72),
								std::clamp(o[kMatTint], 0, 20)),
							"textures\\PersonalGallery\\Filters\\clear_n.dds");
					}
				} else if (_strnicmp(name.data(), "glass", 5) == 0) {  // the game may hand it back as "GLASS"
					sawGlass = true;
					// real glass (lit, reflective): its own material, with the player's shine; old glass greener and duller
					if (auto* lit = netimmerse_cast<RE::BSLightingShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get())) {
						const int   k = Clamp(o[kGlass], Catalog::kGlass.size());
						const float shine = std::clamp(o[kGlassShine], 0, 20) * 0.1f;
						Show(g, k > 0);
						if (k > 0 && lit->material) {
							if (auto* fresh = static_cast<RE::BSLightingShaderMaterialBase*>(lit->material->Create())) {
								fresh->CopyMembers(lit->material);
								lit->SetMaterial(fresh, true);
								lit->SetupGeometry(g);
								lit->FinishSetupGeometry(g);
								fresh->~BSLightingShaderMaterialBase();
								RE::free(fresh);
							}
							if (auto* own = static_cast<RE::BSLightingShaderMaterialBase*>(lit->material)) {
								// thickness: thin crown glass, normal, or thick plate (greener, deeper reflections)
								const int   thick = std::clamp(o[kGlassThick], 0, 2);
								const float green = 0.04f * thick;
								own->specularColor = k == 2 ? RE::NiColor(0.82f, 0.95f, 0.8f) : RE::NiColor(1.0f - green, 1.0f, 1.0f - green * 1.5f);
								own->specularColorScale = 3.0f * shine * (0.85f + 0.15f * thick);
								g->local.translate.y = -0.15f * thick;  // a thicker pane stands a little off the picture
								if (own->GetFeature() == RE::BSShaderMaterial::Feature::kEnvironmentMap) {
									static_cast<RE::BSLightingShaderMaterialEnvmap*>(own)->envMapScale = (k == 2 ? 0.55f : 1.0f) * shine * (0.8f + 0.2f * thick);
								}
							}
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					const int k = Clamp(o[kGlass], Catalog::kGlass.size());
					// how shiny: the player's Glass shine (100 % as it was)
					const float shine = std::clamp(o[kGlassShine], 0, 20) * 0.1f;
					Overlay(g, k > 0, k == 2 ? 0.86f : 1.0f, k == 2 ? 0.95f : 1.0f, k == 2 ? 0.8f : 1.0f, std::min(1.0f, (k == 2 ? 1.0f : 0.7f) * shine));
					if (auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get()); prop && k > 0) {
						if (auto* own = static_cast<RE::BSEffectShaderMaterial*>(prop->material)) {
							own->baseColorScale = std::max(1.0f, shine);  // past 100 %: brighter reflections
						}
					}
				} else if (_stricmp(g->name.c_str(), "holo") == 0 || _stricmp(g->name.c_str(), "ripples") == 0 || _stricmp(g->name.c_str(), "mist") == 0) {  // the projection's light, the basin's water
					const int k = std::clamp(o[kVision], 0, 10);
					Overlay(g, k > 0, 1, 1, 1, std::pow(k / 10.0f, 1.2f));
				} else if (_stricmp(g->name.c_str(), "beam") == 0) {  // the projector's cone of light, as strong as chosen
					const int k = std::clamp(o[kBeam], 0, 10);
					// a real range: 10% barely there, 100% a bright shaft
					Overlay(g, k > 0, 1, 1, 1, std::pow(k / 10.0f, 1.4f));
					if (auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(g->GetGeometryRuntimeData().shaderProperty.get()); prop && k > 0) {
						if (auto* own = static_cast<RE::BSEffectShaderMaterial*>(prop->material)) {
							own->baseColorScale = 0.3f + k * 0.2f;  // 0.5 .. 2.3
						}
					}
				} else if (name == "grime") {
					const int k = Clamp(o[kAge], Catalog::kAges.size());
					Overlay(g, k > 0, 1, 1, 1, k == 2 ? 1.0f : 0.5f);
				} else if (name == "lamp_brass") {
					Show(g, o[kLamp] == 1);
					g->local.translate = { 0, -front, dz };
				} else if (name == "lamp_sconce" || name == "lamp_candle") {
					Show(g, false);  // the sconces are the game's own now (mounted below)
				} else if (name.starts_with("lamp_flame")) {
					// only the flames are ours: shown below, once each has a wick to burn on
					Show(g, false);
					g->local.translate = { 0, 0, 0 };
					g->local.rotate = Diag(1, 1, 1);
					flames.push_back(g);
				} else if (IStarts(name, "attach|")) {
					markers.push_back(g);
				} else if (name.starts_with("plate_")) {
					const int style = Clamp(o[kPlate], Catalog::kPlates.size());
					static constexpr std::string_view bodies[]{ "", "plate_brass", "plate_wood", "plate_iron" };
					const bool on = name == "plate_text" ? style > 0 && !w.title.empty() : name == bodies[style];
					Show(g, on);
					plates.push_back(g);  // sized below, all about one point
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (o[kGlass] > 0 && !sawGlass) {  // (diagnostics: glass chosen, but this model shows none)
				static std::set<RE::NiAVObject*> told;
				if (told.insert(root).second) {
					std::string names;
					RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
						names += std::string(g->name.c_str()) + " ";
						return RE::BSVisit::BSVisitControl::kContinue;
					});
					SKSE::log::warn("glass chosen but none in {:08X}: {}", ref->GetFormID(), names);
				}
			}
			// The plaque's size: every part of it scaled about the lettering's centre, so it stays where it hangs and
			// the lettering stays on its face. (Each part used to grow about its own centre: a large plaque's body
			// grew thicker and swallowed the lettering, which lies a hair in front of it.)
			if (!plates.empty()) {
				static constexpr float sizes[]{ 0.75f, 1.0f, 1.3f };
				const float            k = sizes[Clamp(o[kPlateSize], 3)];
				RE::NiPoint3           c = plates.front()->GetModelData().modelBound.center;
				for (auto* g : plates) {
					if (std::string_view(g->name.c_str()) == "plate_text") {
						c = g->GetModelData().modelBound.center;
					}
				}
				for (auto* g : plates) {
					g->local.scale = k;
					g->local.translate = RE::NiPoint3{ 0, 0, -dz } + c * (1 - k);
				}
			}
			if (o[kPlate] > 0 && !w.title.empty()) {
				if (auto tex = PlateTexture(w.title, o[kPlate], o[kPlateFont], std::clamp(o[kPlateHue], 0, 72)); !tex.empty()) {
					Apply::SetShapeTextures(ref.get(), "plate_text", tex, "textures\\PersonalGallery\\Filters\\clear_n.dds");
				}
			}
			// a projector: its projection (the picture, its light and glow, the beam) leans and grows about the lens,
			// as a real projection does - the beam stays on the lens, the stand stays on the floor
			for (auto* m : markers) {
				if (!IStarts(m->name.c_str(), "attach|apex|")) {
					continue;
				}
				const RE::NiPoint3 apex = m->GetModelData().modelBound.center;
				const float        a = (std::clamp(o[kProjTilt], 0, 20) - 10) * 3.0f * 0.01745329f;
				const float        k = 0.5f + std::clamp(o[kProjSize], 0, 20) * 0.1f;
				const float        t = (std::clamp(o[kProjTurn], 0, 20) - 10) * 3.0f * 0.01745329f;
				RE::NiMatrix3      up, side;  // up / down about the lens's own left-right line, then left / right about the upright
				up.entry[0][0] = 1.0f;
				up.entry[1][1] = std::cos(a);
				up.entry[1][2] = -std::sin(a);
				up.entry[2][1] = std::sin(a);
				up.entry[2][2] = std::cos(a);
				side.entry[0][0] = std::cos(t);
				side.entry[0][1] = -std::sin(t);
				side.entry[1][0] = std::sin(t);
				side.entry[1][1] = std::cos(t);
				side.entry[2][2] = 1.0f;
				const RE::NiMatrix3 lean = side * up;
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
					const std::string      lowered = Catalog::Lower(g->name.c_str());
					const std::string_view n = lowered;
					if (n.starts_with("canvas") || n.starts_with("magic_") || n == "holo" || n == "beam" || n == "grime") {
						g->local.rotate = lean;
						g->local.scale = k;
						g->local.translate = apex - (lean * apex) * k;
					} else if (n == "projhead" || n == "crystal") {
						// the lens head (and the crystal in it) turns about its own front to point the same way: it does
						// not grow with the projection
						g->local.rotate = lean;
						g->local.translate = apex - lean * apex;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				break;
			}
			if (auto* node = root->AsNode(); node && !markers.empty()) {  // the chosen sconces, from the game itself
				const float xScale = info.found ? (info.hw + info.fw * widthK[width]) * sx / (info.hw + info.fw) : 1.0f;
				std::vector<RE::NiPoint3> wicks;
				for (auto* m : markers) {
					if (!IStarts(m->name.c_str(), "attach|sconce|")) {
						MountFixed(node, m);  // the display's own prop (Dwemer stand, port, orb stand)
					} else if (auto at = MountSconce(node, m, o[kLamp], xScale)) {
						wicks.push_back(*at);
					}
				}
				for (auto* flame : flames) {  // each flame on the candle of its own side (none if the sconce could not be loaded)
					const RE::NiPoint3 c = flame->GetModelData().modelBound.center;
					for (const auto& wick : wicks) {
						if ((wick.x < 0) == (c.x < 0)) {
							flame->local.translate = wick - c;
							Show(flame, true);
						}
					}
				}
			}
			RE::NiUpdateData update{};
			root->Update(update);
			return true;
		}

		void Process()
		{
			std::lock_guard l(lock);
			scheduled = false;
			for (auto it = pending.begin(); it != pending.end();) {
				auto& w = it->second;
				if (Fit(w) || ++w.tries > 600) {  // gone or never loaded: its OnLoad will ask again
					it = pending.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	bool LightSpot(RE::TESObjectREFR* a_ref, int& a_side, int& a_height, int& a_out)
	{
		if (!a_ref) {
			return false;
		}
		std::lock_guard l(lock);
		const auto it = known.find(a_ref->GetFormID());
		if (it == known.end()) {
			return false;
		}
		a_side = it->second.w.o[kLightSide];
		a_height = it->second.w.o[kLightHeight];
		a_out = it->second.w.o[kLightOut];
		return true;
	}

	void Set(RE::TESObjectREFR* a_ref, const std::vector<int>& a_options, const std::string& a_title)
	{
		if (!a_ref) {
			return;
		}
		Wanted w;
		w.ref = a_ref->CreateRefHandle();
		for (int i = 0; i < kOptionCount; ++i) {
			w.o[i] = i < static_cast<int>(a_options.size()) ? a_options[i] : kDefaults[i];
		}
		w.title = a_title;
		// choices the gallery no longer offers: whatever an older painting has saved, they take their built-in value
		for (const int gone : { kIntensity, kGlassThick, kMagicAlpha, kMagicDepth, kMixEdge, kMixAura, kMixSheen, kMixVeil, kSweep,
				 kSheenSpeed, kLightSide, kLightHeight, kLightOut, kLightFlickerSpeed, kSharpness }) {
			w.o[gone] = kDefaults[gone];
		}
		const int* o = w.o;
		Magic::SetIntensity(a_ref, Catalog::kIntensityScales[Clamp(o[kIntensity], Catalog::kIntensities.size())]);
		Extras::SetStash(a_ref, Clamp(o[kStashOpen], Catalog::kStashOpenings.size()), std::max(1, o[kStashAmount]) * 0.1f,
			0.2f + 0.1f * std::clamp(o[kStashSpeed], 0, 28));  // 0.2 .. 3 seconds
		{  // the player's own magic
			Magic::Custom c;
			c.hue = o[kMagicHue] > 0 ? (o[kMagicHue] - 1) * 5.0f + 1.0f : 0.0f;
			c.sat = o[kMagicSat] * 0.05f;
			c.strength = o[kMagicStrength] * 0.1f;
			c.alpha = o[kMagicAlpha] * 0.05f;
			c.motion = std::clamp(o[kMagicMotion], 0, static_cast<int>(Catalog::kMotions.size()) - 1);
			c.speed = o[kMagicSpeed] * 0.1f;
			c.depth = o[kMagicDepth] * 0.05f;
			for (int p = 0; p < 5; ++p) {
				c.mix[p] = o[kMixEdge + p] * 0.1f;
			}
			// corner gems: as the glow style has them (anything else), always (1) or never (2)
			c.mix[2] = o[kMixCorners] == 1 ? 100.0f : o[kMixCorners] == 2 ? 0.0f : 1.0f;
			c.reverse = o[kSweep] == 1;
			c.sweepSpeed = o[kSheenSpeed] * 0.1f;
			c.sheenAngle = std::clamp(o[kSheenAngle], 0, 23);  // every 15 degrees, all the way round
			c.when = std::clamp(o[kMagicWhen], 0, 2);
			c.nightStart = std::clamp(o[kNightStart], 0, 23);
			c.nightEnd = std::clamp(o[kNightEnd], 0, 23);
			Magic::SetCustom(a_ref, c);
		}
		Extras::SetSchedule(a_ref, std::clamp(o[kLightFlicker], 0, 3), std::max(1, o[kLightFlickerSpeed]) * 0.1f, std::clamp(o[kLightWhen], 0, 2),
			std::clamp(o[kNightStart], 0, 23), std::clamp(o[kNightEnd], 0, 23));
		const int extra = Catalog::PictureExtra(o[kTintHue], o[kTintStrength], o[kVignette], o[kSharpness], o[kFilterStrength]);
		// (never below a fifth: a projected picture is lit only by itself, and "Off" left it solid black)
		Apply::SetSurface(a_ref, o[kPicGloss] * 0.05f, o[kPicOpacity] * 0.05f, std::max(0.2f, std::clamp(o[kVision], 0, 10) * 0.1f));
		Apply::SetExtra(a_ref, extra);
		Extras::SetLightLook(a_ref, o[kLightHue] > 0 ? (o[kLightHue] - 1) * 5.0f + 1.0f : 0.0f, o[kLightBright] * 0.1f,
			std::max(1, o[kLightReach]) * 0.1f);
		Player::SetOptions(a_ref, Catalog::VolumeScale(Clamp(o[kVolume], Catalog::kVolumeSteps)), o[kPlayMode] == 1,
			Catalog::kQualityWidths[Clamp(o[kQuality], Catalog::kQualities.size())],
			Catalog::PictureExtra(o[kTintHue], o[kTintStrength], o[kVignette], o[kSharpness], o[kFilterStrength]), std::clamp(o[kVideoFps], 0, 4));
		Apply::SetView(a_ref, 1.0f, 0.0f, 0.0f, false);  // the whole picture, as it is (crop and mirror removed)
		std::lock_guard l(lock);
		// the frame, mat, glass, age, lamp and plate need refitting only when one of them (or the title) changed:
		// a crop, zoom, magic or light change must not make the mat and plate reload (that flickered)
		bool       same = false;
		const auto old = known.find(a_ref->GetFormID());
		if (old != known.end() && old->second.root == a_ref->Get3D() && old->second.w.title == w.title) {
			same = true;
			for (const int i : { kFrameWidth, kFrameDepth, kMat, kMatWidth, kGlass, kGlassShine, kAge, kLamp, kPlate, kPlateFont, kBeam, kVision, kGlassThick, kFrameHue, kFrameTint, kPlateHue, kPlateSize, kProjTilt, kProjSize, kMatHue, kMatTint, kProjTurn }) {
				same &= old->second.w.o[i] == w.o[i];
			}
		}
		known[a_ref->GetFormID()] = { w, a_ref->Get3D() };
		if (!same) {
			pending[a_ref->GetFormID()] = std::move(w);
		}
	}

	void SetShadows(bool a_on)
	{
		if (castShadows.exchange(a_on) == a_on) {
			return;
		}
		std::lock_guard l(lock);
		for (auto& [id, k] : known) {  // every painting again, with or without its shadow
			Wanted w = k.w;
			w.tries = 0;
			pending[id] = std::move(w);
		}
	}

	void Frame()
	{
		{
			std::lock_guard l(lock);
			if (++frameCount % 15 == 0) {  // a few times a second: paintings whose 3D was reloaded
				for (auto it = known.begin(); it != known.end();) {
					auto ref = it->second.w.ref.get();
					if (!ref) {
						it = known.erase(it);
						continue;
					}
					if (auto* root = ref->Get3D(); root && root != it->second.root) {
						it->second.root = root;
						Wanted w = it->second.w;
						w.tries = 0;
						pending[it->first] = std::move(w);
					}
					++it;
				}
			}
			if (pending.empty() || scheduled) {
				return;
			}
			scheduled = true;
		}
		SKSE::GetTaskInterface()->AddTask([] { Process(); });
	}
}
