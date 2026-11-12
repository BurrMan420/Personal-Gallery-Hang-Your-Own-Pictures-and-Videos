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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Video decoding for living portraits. MP4/MOV/WMV/AVI/M4V go through Windows Media Foundation
// (H.264/HEVC/AAC/MP3, hardware accelerated where Windows supports it); GIF goes through WIC.
namespace Video
{
	struct Frame
	{
		double                    time{ 0 };  // seconds
		int                       w{ 0 };
		int                       h{ 0 };
		std::vector<std::uint8_t> bgra;  // top-down, w * 4 bytes per row
	};

	struct Audio
	{
		int                channels{ 0 };
		int                rate{ 0 };
		std::vector<std::int16_t> samples;  // interleaved 16-bit, mono or stereo (surround is folded down)
	};

	class Decoder
	{
	public:
		virtual ~Decoder() = default;
		// Next frame in order; false at the end. Frames earlier than a_skipBefore (seconds) are decoded but not
		// copied out - a_out.bgra is left empty for them.
		virtual bool   Next(Frame& a_out, double a_skipBefore = -1.0) = 0;
		virtual void   Rewind() = 0;
		virtual void   Seek(double a_seconds) = 0;    // the next frames come from about here (at or a little before)
		virtual double FrameRate() const = 0;         // frames a second, 0 if unknown
		virtual double Duration() const = 0;
		virtual int    Width() const = 0;
		virtual int    Height() const = 0;
	};

	bool IsVideoFile(const std::wstring& a_path);

	// nullptr if the file can't be decoded
	// a_aspect / a_maxWidth (optional): the canvas shape and the widest it will be shown. The frames then come
	// already scaled down to about that (never below it), which is far less work than full-size frames - a
	// 1920 x 1400 video at 60 fps could not be kept up with otherwise.
	std::unique_ptr<Decoder> Open(const std::wstring& a_path, float a_aspect = 0.0f, int a_maxWidth = 0);

	// Decodes the whole soundtrack (empty if there is none).
	Audio ReadAudio(const std::wstring& a_path, const std::atomic<bool>* a_run = nullptr);  // a_run false: stop early

	void Startup();   // once per process/thread before use (COM + Media Foundation)
	void Shutdown();  // a thread that called Startup, when it is done

}
