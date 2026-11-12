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

// Development tracker: odd things and slow spots, written to Data\PersonalGallery\dev_log.txt.
// Off unless Data\PersonalGallery\dev.txt exists (never shipped), so players pay nothing for it.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace Dev
{
	inline bool Enabled()
	{
		static const bool on = [] {
			std::error_code ec;
			return std::filesystem::exists(std::filesystem::current_path() / "Data" / "PersonalGallery" / "dev.txt", ec);
		}();
		return on;
	}

	// Screenshots: asked for here (F11, or something odd), taken by the render thread on its next frame.
	inline std::mutex  shotLock;
	inline std::string shotReason;  // empty: none wanted

	inline void RequestShot(std::string_view a_reason, bool a_manual)
	{
		static auto     lastAuto = std::chrono::steady_clock::time_point{};
		const auto      now = std::chrono::steady_clock::now();
		std::lock_guard l(shotLock);
		if (!a_manual) {
			if (now - lastAuto < std::chrono::seconds(10)) {
				return;  // a burst of problems: one picture is enough
			}
			lastAuto = now;
		}
		shotReason = a_reason;
	}

	inline bool TakeShot(std::string& a_reason)
	{
		std::lock_guard l(shotLock);
		if (shotReason.empty()) {
			return false;
		}
		a_reason = std::move(shotReason);
		shotReason.clear();
		return true;
	}

	inline void Write(std::string_view a_tag, std::string_view a_msg)
	{
		if (a_tag == "odd" || a_tag == "fail" || a_tag == "error" || a_tag == "hitch") {
			RequestShot(a_tag, false);  // see what it looked like
		}
		static std::mutex lock;
		static FILE*      file = nullptr;
		static const auto start = std::chrono::steady_clock::now();
		std::lock_guard   l(lock);
		if (!file) {
			const auto path = std::filesystem::current_path() / "Data" / "PersonalGallery" / "dev_log.txt";
			file = _wfopen(path.c_str(), L"w");  // fresh each launch
			if (!file) {
				return;
			}
		}
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::fprintf(file, "%9.3f  %-8.*s %.*s\n", t, static_cast<int>(a_tag.size()), a_tag.data(), static_cast<int>(a_msg.size()), a_msg.data());
		std::fflush(file);  // nothing lost if the game crashes
	}

	template <class... A>
	void Log(std::string_view a_tag, std::format_string<A...> a_fmt, A&&... a_args)
	{
		if (Enabled()) {
			Write(a_tag, std::format(a_fmt, std::forward<A>(a_args)...));
		}
	}

	// Logs a block that took longer than a_limitMs.
	class Timer
	{
	public:
		Timer(std::string_view a_what, double a_limitMs) :
			what(a_what), limit(a_limitMs), start(std::chrono::steady_clock::now()) {}
		~Timer()
		{
			if (Enabled()) {
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (ms > limit) {
					Write("slow", std::format("{} took {:.1f} ms", what, ms));
				}
			}
		}

	private:
		std::string                           what;
		double                                limit;
		std::chrono::steady_clock::time_point start;
	};
}
