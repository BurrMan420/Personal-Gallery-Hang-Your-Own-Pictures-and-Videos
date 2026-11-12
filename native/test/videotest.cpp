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

// Checks the living-portrait pipeline outside the game:
//   VideoTest.exe <video> <out-dir>
// decodes every frame (timing it), saves a few as PNG, decodes the soundtrack, and runs the
// 3D audio maths for a painting to the listener's left. Nothing is played out loud.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include <DirectXTex.h>
#include <x3daudio.h>
#include <xaudio2.h>

#include "../src/Video.h"

static void SavePng(const Video::Frame& f, const std::filesystem::path& path)
{
	DirectX::Image img{ static_cast<size_t>(f.w), static_cast<size_t>(f.h), DXGI_FORMAT_B8G8R8A8_UNORM,
		static_cast<size_t>(f.w) * 4, static_cast<size_t>(f.w) * f.h * 4, const_cast<std::uint8_t*>(f.bgra.data()) };
	DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str());
}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 3) {
		return 1;
	}
	Video::Startup();
	const std::filesystem::path out = argv[2];

	if (argc >= 5 && std::wstring(argv[3]) == L"bake") {  // VideoTest <video> <out-dir> bake <filter>
		const auto        file = out / L"baked.mp4";
		std::atomic<bool> cancel{ false };
		const auto        t0 = std::chrono::steady_clock::now();
		const bool        ok = Video::Bake(argv[1], file.wstring(), 1.3333f, _wtoi(argv[4]), 1280, nullptr, cancel);
		std::wprintf(L"BAKE: %ls in %.1f s, %lld KB\n", ok ? L"ok" : L"FAILED",
			std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
			ok ? static_cast<long long>(std::filesystem::file_size(file) / 1024) : 0LL);
		if (auto d = ok ? Video::Open(file.wstring()) : nullptr) {
			Video::Frame f;
			int          n = 0;
			while (d->Next(f)) {
				if (n == 60) {
					SavePng(f, out / L"baked60.png");
				}
				++n;
			}
			std::wprintf(L"BAKE: plays back %dx%d, %d frames, %.2f s\n", d->Width(), d->Height(), n, d->Duration());
		}
		return ok ? 0 : 3;
	}

	auto dec = Video::Open(argv[1]);
	if (!dec) {
		std::wprintf(L"VIDEO: could not open\n");
		return 2;
	}
	std::wprintf(L"VIDEO: %dx%d, %.2f s\n", dec->Width(), dec->Height(), dec->Duration());
	Video::Frame f;
	int          n = 0;
	double       lastTime = 0;
	const auto   t0 = std::chrono::steady_clock::now();
	while (dec->Next(f)) {
		if (n == 0 || n == 90) {
			SavePng(f, out / (L"frame" + std::to_wstring(n) + L".png"));
		}
		lastTime = f.time;
		++n;
	}
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::wprintf(L"VIDEO: decoded %d frames (last at %.3f s) in %.2f s = %.0f fps decode speed\n", n, lastTime, secs, n / secs);
	dec->Rewind();
	if (dec->Next(f)) {
		std::wprintf(L"VIDEO: rewind ok, first frame at %.3f s\n", f.time);
	}

	auto a = Video::ReadAudio(argv[1]);
	if (a.samples.empty()) {
		std::wprintf(L"AUDIO: none\n");
	} else {
		const size_t frames = a.samples.size() / a.channels;
		int          crossings = 0;
		double       rms = 0;
		for (size_t i = 1; i < frames; ++i) {
			const float s0 = a.samples[(i - 1) * a.channels], s1 = a.samples[i * a.channels];
			crossings += (s0 < 0) != (s1 < 0);
			rms += s1 * s1;
		}
		std::wprintf(L"AUDIO: %d ch, %d Hz, %.2f s, pitch ~%.0f Hz, level %.3f rms\n", a.channels, a.rate,
			double(frames) / a.rate, crossings / 2.0 / (double(frames) / a.rate), std::sqrt(rms / frames));
	}

	// 3D audio: listener at the origin facing +Z (X3DAudio is left-handed), painting 3 m to the left
	IXAudio2*               xa = nullptr;
	IXAudio2MasteringVoice* master = nullptr;
	if (FAILED(XAudio2Create(&xa, 0)) || FAILED(xa->CreateMasteringVoice(&master))) {
		std::wprintf(L"XAUDIO2: no audio device\n");
		return 0;
	}
	DWORD mask = 0;
	master->GetChannelMask(&mask);
	XAUDIO2_VOICE_DETAILS md{};
	master->GetVoiceDetails(&md);
	X3DAUDIO_HANDLE x3d;
	X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, x3d);
	X3DAUDIO_LISTENER listener{};
	listener.OrientFront = { 0, 0, 1 };
	listener.OrientTop = { 0, 1, 0 };
	X3DAUDIO_EMITTER emitter{};
	emitter.ChannelCount = 1;
	emitter.CurveDistanceScaler = 1.0f;
	emitter.Position = { -3.0f, 0, 0 };
	float                 matrix[8]{};
	X3DAUDIO_DSP_SETTINGS dsp{};
	dsp.SrcChannelCount = 1;
	dsp.DstChannelCount = md.InputChannels;
	dsp.pMatrixCoefficients = matrix;
	X3DAudioCalculate(x3d, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);
	std::wprintf(L"XAUDIO2: %u output channels; painting on the left -> left %.2f, right %.2f\n", md.InputChannels, matrix[0],
		md.InputChannels > 1 ? matrix[1] : 0.0f);
	xa->Release();
	return 0;
}
