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
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The picture library: every image and video in Data\PersonalGallery\Pictures (and its subfolders),
// converted on a background thread into cached game textures plus small thumbnails for the UI.
// Indices are stable for the whole session: a removed file keeps its slot (Available() is false),
// a renamed or moved file is recognised by its content and keeps its slot, new files are appended.
namespace Pictures
{
	struct Thumb
	{
		int                       w{ 0 };
		int                       h{ 0 };
		std::vector<std::uint8_t> rgba;
	};

	void Init();

	// Everything the gallery shows about each picture, in one go (index = picture index).
	struct Info
	{
		std::string name;
		std::string album;
		bool        video{ false };
		bool        available{ false };
		bool        favourite{ false };
		int         aspect{ 5 };  // its canvas shape (Catalog::kAspects)
	};
	int  CacheMB();     // how much disk the made looks and filtered videos take
	void ClearCache();  // removes them all; each is made again when a painting next needs it
	std::vector<Info> Snapshot(std::uint64_t& a_generation);
	std::uint64_t     Generation();  // changes when Snapshot() would

	// Texture detail for clear pictures (MCM): Standard 2048, High 4096.
	void SetDetail(bool a_high);
	// Look again for added, removed and renamed files (the MCM's Scan). Waits for it.
	// Returns [added, removed, renamed or moved].
	std::array<int, 3> Rescan();

	int         Count();           // slots, including removed pictures
	int         AvailableCount();  // pictures that exist
	bool        Available(int a_pic);
	int         Valid(int a_pic);              // a_pic if it exists, else the next one that does (-1 if none)
	int         Next(int a_pic, int a_step);   // the next existing picture after a_pic, wrapping
	std::string Key(int a_pic);   // path inside the Pictures folder
	std::string Id(int a_pic);    // content fingerprint (survives renames and moves)
	std::string Album(int a_pic);  // its subfolder ("" = the main folder)
	std::vector<std::string> Albums();
	bool        Favourite(int a_pic);
	void        SetFavourite(int a_pic, bool a_on);  // saved in Data\PersonalGallery\favourites.txt
	// Slideshow: the picture after a_pic. mode: 1 in order, 2 shuffle, 3 favourites, 4 same album.
	int         NextSlide(int a_pic, int a_mode);
	std::string Name(int a_pic);  // shown in the UI
	int         Aspect(int a_pic);
	int         Find(const std::string& a_key, const std::string& a_id);  // -1 if missing
	bool        IsVideo(int a_pic);

	std::wstring Path(int a_pic);

	// Texture path for the game ("textures\..."), or "" if it is still being made (it is then queued first).
	std::string Texture(int a_pic, int a_filter, int a_tone, int a_extra = 0);  // a_extra: Catalog::PictureExtra

	// Thumbnail of a picture with a filter and tone applied; nullptr while it is being made (queued).
	std::shared_ptr<const Thumb> Thumbnail(int a_pic, int a_filter, int a_tone);

	// "Preparing pictures 3 / 19" while work is queued, otherwise "".
	std::string Status();

	// Called on the worker thread whenever a texture finishes.
	void SetReadyCallback(std::function<void()> a_callback);
}
