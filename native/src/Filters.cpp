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

#include "Filters.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>

namespace Filters
{
	namespace
	{
		using Plane = std::vector<float>;

		float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

		// Smooth value noise, tiling horizontally (Catmull-Rom over a random grid).
		Plane Noise(int w, int h, int cellsX, int cellsY, std::mt19937& rng)
		{
			cellsX = std::max(cellsX, 2);
			cellsY = std::max(cellsY, 2);
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);
			std::vector<float>                    grid(static_cast<size_t>(cellsX) * (cellsY + 1));
			for (auto& g : grid) {
				g = dist(rng);
			}
			auto G = [&](int gx, int gy) {
				gx = ((gx % cellsX) + cellsX) % cellsX;
				gy = std::clamp(gy, 0, cellsY);
				return grid[static_cast<size_t>(gy) * cellsX + gx];
			};
			auto cr = [](float p0, float p1, float p2, float p3, float t) {
				return p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
			};
			Plane out(static_cast<size_t>(w) * h);
			for (int y = 0; y < h; ++y) {
				const float fy = static_cast<float>(y) / h * cellsY;
				const int   iy = static_cast<int>(fy);
				const float ty = fy - iy;
				for (int x = 0; x < w; ++x) {
					const float fx = static_cast<float>(x) / w * cellsX;
					const int   ix = static_cast<int>(fx);
					const float tx = fx - ix;
					float       col[4];
					for (int k = 0; k < 4; ++k) {
						const int gy = iy - 1 + k;
						col[k] = cr(G(ix - 1, gy), G(ix, gy), G(ix + 1, gy), G(ix + 2, gy), tx);
					}
					out[static_cast<size_t>(y) * w + x] = Clamp01(cr(col[0], col[1], col[2], col[3], ty));
				}
			}
			return out;
		}

		Plane BlurPlane(const Plane& in, int w, int h, float sigma)
		{
			if (sigma <= 0.3f) {
				return in;
			}
			const int          r = static_cast<int>(std::ceil(sigma * 3));
			std::vector<float> k(2 * r + 1);
			float              sum = 0;
			for (int i = -r; i <= r; ++i) {
				sum += k[i + r] = std::exp(-(i * i) / (2 * sigma * sigma));
			}
			for (auto& v : k) {
				v /= sum;
			}
			Plane tmp(in.size()), out(in.size());
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					float acc = 0;
					for (int i = -r; i <= r; ++i) {
						acc += k[i + r] * in[static_cast<size_t>(y) * w + std::clamp(x + i, 0, w - 1)];
					}
					tmp[static_cast<size_t>(y) * w + x] = acc;
				}
			}
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					float acc = 0;
					for (int i = -r; i <= r; ++i) {
						acc += k[i + r] * tmp[static_cast<size_t>(std::clamp(y + i, 0, h - 1)) * w + x];
					}
					out[static_cast<size_t>(y) * w + x] = acc;
				}
			}
			return out;
		}

		Plane Channel(const Image& img, int c)
		{
			Plane p(static_cast<size_t>(img.w) * img.h);
			for (size_t i = 0; i < p.size(); ++i) {
				p[i] = img.px[i * 3 + c];
			}
			return p;
		}

		void SetChannel(Image& img, int c, const Plane& p)
		{
			for (size_t i = 0; i < p.size(); ++i) {
				img.px[i * 3 + c] = p[i];
			}
		}

		Image Blur(const Image& img, float sigma)
		{
			Image out(img.w, img.h);
			for (int c = 0; c < 3; ++c) {
				SetChannel(out, c, BlurPlane(Channel(img, c), img.w, img.h, sigma));
			}
			return out;
		}

		Plane Luma(const Image& img)
		{
			Plane p(static_cast<size_t>(img.w) * img.h);
			for (size_t i = 0; i < p.size(); ++i) {
				p[i] = 0.299f * img.px[i * 3] + 0.587f * img.px[i * 3 + 1] + 0.114f * img.px[i * 3 + 2];
			}
			return p;
		}

		void Saturate(Image& img, float amount)
		{
			for (size_t i = 0; i < img.px.size(); i += 3) {
				const float g = (img.px[i] + img.px[i + 1] + img.px[i + 2]) / 3.0f;
				for (int c = 0; c < 3; ++c) {
					img.px[i + c] = g + (img.px[i + c] - g) * amount;
				}
			}
		}

		// Kuwahara: each pixel takes the mean colour of its least-varied quadrant.
		Image Kuwahara(const Image& img, int r)
		{
			const int w = img.w, h = img.h;
			const int W1 = w + 1;
			std::vector<double> sat(static_cast<size_t>(W1) * (h + 1) * 5, 0.0);  // r, g, b, l, l^2
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					const float* p = img.at(x, y);
					const double l = (p[0] + p[1] + p[2]) / 3.0;
					const double v[5]{ p[0], p[1], p[2], l, l * l };
					for (int k = 0; k < 5; ++k) {
						const size_t i = (static_cast<size_t>(y + 1) * W1 + x + 1) * 5 + k;
						sat[i] = v[k] + sat[(static_cast<size_t>(y) * W1 + x + 1) * 5 + k] +
						         sat[(static_cast<size_t>(y + 1) * W1 + x) * 5 + k] - sat[(static_cast<size_t>(y) * W1 + x) * 5 + k];
					}
				}
			}
			auto box = [&](int x0, int y0, int x1, int y1, int k) {  // inclusive-exclusive
				return sat[(static_cast<size_t>(y1) * W1 + x1) * 5 + k] - sat[(static_cast<size_t>(y0) * W1 + x1) * 5 + k] -
				       sat[(static_cast<size_t>(y1) * W1 + x0) * 5 + k] + sat[(static_cast<size_t>(y0) * W1 + x0) * 5 + k];
			};
			Image out(w, h);
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					double best = 1e30;
					float* o = out.at(x, y);
					for (int q = 0; q < 4; ++q) {
						const int x0 = std::max((q & 1) ? x : x - r, 0), x1 = std::min((q & 1) ? x + r : x, w - 1) + 1;
						const int y0 = std::max((q & 2) ? y : y - r, 0), y1 = std::min((q & 2) ? y + r : y, h - 1) + 1;
						const double n = static_cast<double>(x1 - x0) * (y1 - y0);
						const double m = box(x0, y0, x1, y1, 3) / n;
						const double var = box(x0, y0, x1, y1, 4) / n - m * m;
						if (var < best) {
							best = var;
							for (int c = 0; c < 3; ++c) {
								o[c] = static_cast<float>(box(x0, y0, x1, y1, c) / n);
							}
						}
					}
				}
			}
			return out;
		}

		Image Median(const Image& img, int r)
		{
			Image              out(img.w, img.h);
			std::vector<float> win;
			win.reserve((2 * r + 1) * (2 * r + 1));
			for (int y = 0; y < img.h; ++y) {
				for (int x = 0; x < img.w; ++x) {
					for (int c = 0; c < 3; ++c) {
						win.clear();
						for (int dy = -r; dy <= r; ++dy) {
							for (int dx = -r; dx <= r; ++dx) {
								win.push_back(img.at(std::clamp(x + dx, 0, img.w - 1), std::clamp(y + dy, 0, img.h - 1))[c]);
							}
						}
						std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
						out.at(x, y)[c] = win[win.size() / 2];
					}
				}
			}
			return out;
		}

		// Flat tones without false colours: quantise brightness into steps, keep each pixel's hue.
		void Posterize(Image& img, int levels)
		{
			const float L = static_cast<float>(levels);
			for (size_t i = 0; i < img.px.size(); i += 3) {
				float*      p = &img.px[i];
				const float y = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
				const float q = (std::floor(y * L) + 0.5f) / L;
				const float k = q / std::max(y, 1e-3f);
				for (int c = 0; c < 3; ++c) {
					p[c] = Clamp01(p[c] * k);
				}
			}
		}

		Plane Edges(const Plane& p, int w, int h)
		{
			Plane out(p.size());
			auto  P = [&](int x, int y) { return p[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					const float gx = P(x + 1, y - 1) + 2 * P(x + 1, y) + P(x + 1, y + 1) - P(x - 1, y - 1) - 2 * P(x - 1, y) - P(x - 1, y + 1);
					const float gy = P(x - 1, y + 1) + 2 * P(x, y + 1) + P(x + 1, y + 1) - P(x - 1, y - 1) - 2 * P(x, y - 1) - P(x + 1, y - 1);
					out[static_cast<size_t>(y) * w + x] = std::sqrt(gx * gx + gy * gy) * 0.25f;
				}
			}
			return out;
		}

		// thin crack network where smooth noise crosses its midpoint (~1px wide at any scale)
		Plane Ridges(int w, int h, int cells, std::mt19937& rng)
		{
			const Plane n = Noise(w, h, cells, std::max(2, cells * h / std::max(w, 1)), rng);
			Plane       out(n.size());
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					const float gx = n[static_cast<size_t>(y) * w + std::min(x + 1, w - 1)] - n[static_cast<size_t>(y) * w + std::max(x - 1, 0)];
					const float gy = n[static_cast<size_t>(std::min(y + 1, h - 1)) * w + x] - n[static_cast<size_t>(std::max(y - 1, 0)) * w + x];
					const float slope = std::sqrt(gx * gx + gy * gy) * 0.5f + 1e-4f;
					const float d = std::abs(n[static_cast<size_t>(y) * w + x] - 0.5f) / slope;
					out[static_cast<size_t>(y) * w + x] = std::exp(-d * d / 0.8f);
				}
			}
			return out;
		}

		Image Parchment(int w, int h, std::mt19937& rng)
		{
			const Plane blot1 = Noise(w, h, 6, 6, rng), blot2 = Noise(w, h, 24, 24, rng), fine = Noise(w, h, std::max(2, w / 8), std::max(2, h / 8), rng);
			Image       out(w, h);
			for (size_t i = 0; i < blot1.size(); ++i) {
				const float k = (0.82f + 0.25f * (blot1[i] * 0.6f + blot2[i] * 0.4f)) * (0.92f + 0.08f * fine[i]);
				out.px[i * 3] = 214 / 255.0f * k;
				out.px[i * 3 + 1] = 190 / 255.0f * k;
				out.px[i * 3 + 2] = 146 / 255.0f * k;
			}
			return out;
		}

		void Vignette(Image& img, float strength, int power)
		{
			for (int y = 0; y < img.h; ++y) {
				for (int x = 0; x < img.w; ++x) {
					const float dx = (static_cast<float>(x) / img.w - 0.5f) * 2, dy = (static_cast<float>(y) / img.h - 0.5f) * 2;
					const float v = 1 - strength * (power == 2 ? (dx * dx + dy * dy) * 0.5f : std::pow(dx, 4.0f) + std::pow(dy, 4.0f));
					float*      p = img.at(x, y);
					for (int c = 0; c < 3; ++c) {
						p[c] *= v;
					}
				}
			}
		}

		Image Oil(const Image& src, std::mt19937& rng)
		{
			// visible brushwork: wide Kuwahara strokes, then a finer pass to keep the faces readable
			const int r = std::max(3, src.w / 170);
			Image     p = Kuwahara(Kuwahara(src, r), std::max(2, r / 2));
			const Plane a = Noise(p.w, p.h, std::max(8, p.w / 20), std::max(8, p.h / 70), rng);
			const Plane b = Noise(p.w, p.h, std::max(8, p.w / 70), std::max(8, p.h / 20), rng);
			const Plane region = Noise(p.w, p.h, 10, 10, rng);
			Saturate(p, 1.2f);
			const float tint[3]{ 1.04f, 1.0f, 0.93f };
			for (size_t i = 0; i < a.size(); ++i) {
				const float stroke = region[i] > 0.5f ? a[i] : b[i];
				for (int c = 0; c < 3; ++c) {
					p.px[i * 3 + c] = Clamp01(p.px[i * 3 + c] * (0.9f + 0.2f * stroke) * tint[c]);
				}
			}
			return p;
		}

		Image Watercolor(const Image& src, std::mt19937& rng)
		{
			const int r = std::max(2, src.w / 450);
			Image     soft = Blur(Median(src, r), 2.0f);
			Image     washes = soft;
			Posterize(washes, 7);
			washes = Blur(washes, 1.5f);
			const Plane edges = BlurPlane(Edges(Luma(washes), washes.w, washes.h), washes.w, washes.h, 1.2f);
			const Image bleed = Blur(soft, 8.0f);
			const Plane tooth = Noise(src.w, src.h, std::max(16, src.w / 14), std::max(16, src.h / 14), rng);
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);
			Plane grain(tooth.size());
			for (auto& g : grain) {
				g = dist(rng);
			}
			grain = BlurPlane(grain, src.w, src.h, 1.2f);
			Image p(src.w, src.h);
			for (size_t i = 0; i < tooth.size(); ++i) {
				for (int c = 0; c < 3; ++c) {
					p.px[i * 3 + c] = washes.px[i * 3 + c] * 0.75f + bleed.px[i * 3 + c] * 0.25f;
				}
			}
			Saturate(p, 1.1f);
			for (size_t i = 0; i < tooth.size(); ++i) {
				const float pool = 1 - std::min(edges[i] * 4.0f, 0.45f);
				const float paper = 0.94f + 0.08f * (tooth[i] * 0.6f + grain[i] * 0.4f);
				for (int c = 0; c < 3; ++c) {
					float& v = p.px[i * 3 + c];
					v = (1 - (1 - v) * 0.8f) * pool * paper;
				}
			}
			return p;
		}

		Image Sketch(const Image& src, std::mt19937& rng)
		{
			Plane g = Luma(src);
			// autocontrast with a 1% cut
			std::vector<float> sorted(g);
			std::sort(sorted.begin(), sorted.end());
			const float lo = sorted[sorted.size() / 100], hi = sorted[sorted.size() * 99 / 100];
			for (auto& v : g) {
				v = Clamp01((v - lo) / std::max(hi - lo, 1e-3f));
			}
			Plane inv(g.size());
			for (size_t i = 0; i < g.size(); ++i) {
				inv[i] = 1 - g[i];
			}
			inv = BlurPlane(inv, src.w, src.h, std::max(3.0f, src.w / 250.0f));
			const Plane tone = BlurPlane(g, src.w, src.h, std::max(4.0f, src.w / 200.0f));
			Image       p = Parchment(src.w, src.h, rng);
			const float charcoal[3]{ 0.16f, 0.13f, 0.11f };
			for (size_t i = 0; i < g.size(); ++i) {
				const float dodge = Clamp01(g[i] / std::max(1 - inv[i], 0.02f));
				const float lines = std::pow(Clamp01((dodge - 0.55f) / 0.45f), 1.6f);
				const float shade = 0.72f + 0.28f * Clamp01(tone[i] * 1.4f);
				const float ink = Clamp01(lines * shade);
				for (int c = 0; c < 3; ++c) {
					p.px[i * 3 + c] = p.px[i * 3 + c] * ink + charcoal[c] * (1 - ink);
				}
			}
			Vignette(p, 0.35f, 4);
			return p;
		}

		Image Aged(const Image& src, std::mt19937& rng)
		{
			Image       p = src;
			const Plane c1 = Ridges(src.w, src.h, std::max(60, src.w / 12), rng);
			const Plane c2 = Ridges(src.w, src.h, std::max(120, src.w / 6), rng);
			const float tint[3]{ 1.04f, 0.98f, 0.82f };
			for (size_t i = 0; i < c1.size(); ++i) {
				float*      v = &p.px[i * 3];
				const float r = v[0], gg = v[1], b = v[2];
				const float sep[3]{ 0.393f * r + 0.769f * gg + 0.189f * b, 0.349f * r + 0.686f * gg + 0.168f * b,
					0.272f * r + 0.534f * gg + 0.131f * b };
				const float crack = std::min(c1[i] + c2[i] * 0.5f, 1.0f);
				for (int c = 0; c < 3; ++c) {
					float x = v[c] * 0.6f + Clamp01(sep[c]) * 0.4f;
					v[c] = (0.08f + x * 0.84f) * tint[c] * (1 - 0.15f * crack);
				}
			}
			Vignette(p, 0.5f, 2);
			return p;
		}

		// thread cells: each cell is one yarn colour (limited palette), shaded over/under
		Image Woven(const Image& src)
		{
			// a tapestry: dyed yarn in a limited range of tones, rounded stitches over and under
			const int t = std::max(5, src.w / 200);
			Image     soft = Blur(src, t * 0.4f);
			Image     p(src.w, src.h);
			for (int y = 0; y < src.h; ++y) {
				for (int x = 0; x < src.w; ++x) {
					const int    cx = x / t, cy = y / t;
					const float* s = soft.at(std::min(cx * t + t / 2, src.w - 1), std::min(cy * t + t / 2, src.h - 1));
					const float  l = 0.299f * s[0] + 0.587f * s[1] + 0.114f * s[2];
					const float  q = (std::floor(l * 12.0f) + 0.5f) / 12.0f;
					const bool   over = (cx + cy) % 2 == 0;
					const float  fx = static_cast<float>(x % t) / t, fy = static_cast<float>(y % t) / t;
					const float  round = std::sin((over ? fx : fy) * 3.14159f) * 0.75f + std::sin((over ? fy : fx) * 3.14159f) * 0.25f;
					for (int c = 0; c < 3; ++c) {
						p.at(x, y)[c] = Clamp01(s[c] * q / std::max(l, 1e-3f) * (0.7f + 0.38f * round));
					}
				}
			}
			return p;
		}
	}

	namespace
	{
		// The look of Skyrim's own paintings: soft brushwork, browned varnish, dark edges, canvas weave.
		Image SkyrimPainting(const Image& src, std::mt19937& rng)
		{
			Image       p = Blur(Kuwahara(src, std::max(2, src.w / 600)), 0.6f);
			const Plane brush = Noise(p.w, p.h, std::max(8, p.w / 20), std::max(8, p.h / 60), rng);
			const float varnish[3]{ 1.0f, 0.92f, 0.76f };
			for (int y = 0; y < p.h; ++y) {
				for (int x = 0; x < p.w; ++x) {
					float*      v = p.at(x, y);
					const float g = (v[0] + v[1] + v[2]) / 3.0f;
					const float sep[3]{ g * 1.07f, g * 0.95f, g * 0.78f };
					const float weave = 1.0f + 0.03f * std::sin(x * 1.9f) * std::sin(y * 1.9f);
					const float stroke = 0.95f + 0.1f * brush[static_cast<size_t>(y) * p.w + x];
					for (int c = 0; c < 3; ++c) {
						float t = (v[c] - g) * 0.7f + g;            // calmer colour
						t = t * 0.75f + sep[c] * 0.25f;             // pulled toward browns
						t = std::pow(Clamp01(t), 1.15f);            // deeper shadows
						v[c] = t * varnish[c] * weave * stroke;
					}
				}
			}
			Vignette(p, 0.6f, 2);
			return p;
		}

		// Pale, chalky colour on lime plaster with flaked patches - a painted temple or barrow wall.
		Image Fresco(const Image& src, std::mt19937& rng)
		{
			Image       p = Blur(src, 1.0f);
			const Plane plaster = Noise(p.w, p.h, std::max(12, p.w / 30), std::max(12, p.h / 30), rng);
			const Plane flakes = Noise(p.w, p.h, 60, 60, rng);
			const Plane cracks = Ridges(p.w, p.h, std::max(40, p.w / 20), rng);
			const float lime[3]{ 0.86f, 0.82f, 0.74f };
			for (size_t i = 0; i < plaster.size(); ++i) {
				float*      v = &p.px[i * 3];
				const float g = (v[0] + v[1] + v[2]) / 3.0f;
				const float flake = Clamp01((flakes[i] - 0.95f) * 10.0f) * 0.3f;  // the odd faint chip where paint flaked
				for (int c = 0; c < 3; ++c) {
					float t = (v[c] - g) * 0.65f + g;
					t = 0.22f + t * 0.68f;  // chalky, no deep blacks
					t *= 0.93f + 0.1f * plaster[i];
					t = t * (1 - flake) + lime[c] * flake;
					v[c] = t * (1 - 0.18f * cracks[i]);
				}
			}
			return p;
		}

		// Black-letter woodcut: hatching whose density follows the darkness, ink on parchment.
		Image Woodcut(const Image& src, std::mt19937& rng)
		{
			// a carved print: bold parallel cuts whose width follows the darkness, black outlines, ink on paper
			const Plane g = BlurPlane(Luma(src), src.w, src.h, std::max(1.5f, src.w / 500.0f));
			const Plane edge = BlurPlane(Edges(g, src.w, src.h), src.w, src.h, 1.0f);
			Image       p = Parchment(src.w, src.h, rng);
			const float period = std::max(5.0f, src.w / 110.0f);
			const float ink[3]{ 0.1f, 0.08f, 0.07f };
			for (int y = 0; y < src.h; ++y) {
				for (int x = 0; x < src.w; ++x) {
					const size_t i = static_cast<size_t>(y) * src.w + x;
					const float  dark = Clamp01((0.72f - g[i]) / 0.68f);            // 0 paper .. 1 solid ink
					const float  phase = std::fmod((y + 0.35f * x) / period, 1.0f);  // gently slanted cuts
					const float  dist = std::abs(phase - 0.5f) * 2.0f;               // 0 at the line's middle
					float        cover = Clamp01((dark - dist) * period * 0.5f + 0.5f);  // anti-aliased line width
					cover = std::max(cover, Clamp01((edge[i] - 0.12f) * 6.0f));
					if (g[i] < 0.12f) {
						cover = 1.0f;
					}
					float* v = p.at(x, y);
					for (int c = 0; c < 3; ++c) {
						v[c] = v[c] * (1 - cover) + ink[c] * cover;
					}
				}
			}
			Vignette(p, 0.25f, 4);
			return p;
		}

		// Small stone tiles with grout lines, each tile one colour.
		Image Mosaic(const Image& src, std::mt19937& rng)
		{
			// stone tesserae in staggered rows: each tile one colour (a little varied), bevelled, set in warm grout
			const int                             t = std::max(8, src.w / 55);
			const int                             grout = std::max(1, t / 7);
			std::uniform_real_distribution<float> jitter(0.86f, 1.1f);
			const float                           mortar[3]{ 0.36f, 0.33f, 0.29f };
			Image                                 p(src.w, src.h);
			for (int row = 0, cy = 0; cy < src.h; cy += t, ++row) {
				const int shift = (row % 2) * (t / 2);
				for (int cx = -shift; cx < src.w; cx += t) {
					const int x0 = std::max(cx, 0), x1 = std::min(cx + t, src.w), y1 = std::min(cy + t, src.h);
					float     col[3]{};
					int       n = 0;
					for (int y = cy; y < y1; ++y) {
						for (int x = x0; x < x1; ++x) {
							for (int c = 0; c < 3; ++c) {
								col[c] += src.at(x, y)[c];
							}
							++n;
						}
					}
					if (!n) {
						continue;
					}
					const float k = jitter(rng);
					for (int y = cy; y < y1; ++y) {
						for (int x = x0; x < x1; ++x) {
							const int   lx = x - cx, ly = y - cy;
							const bool  line = lx < grout || ly < grout;
							const float bevel = 1.0f + 0.12f * ((ly < grout * 2 || lx < grout * 2) ? 1.0f : 0.0f) -
							                    0.12f * ((ly > t - grout * 2 || lx > t - grout * 2) ? 1.0f : 0.0f);
							for (int c = 0; c < 3; ++c) {
								p.at(x, y)[c] = line ? mortar[c] : Clamp01(col[c] / n * k * bevel);
							}
						}
					}
				}
			}
			return p;
		}
	}

	namespace
	{
		float Luma3(const float* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

		void Mix(float* p, const float* col, float t)
		{
			for (int c = 0; c < 3; ++c) {
				p[c] = p[c] * (1 - t) + col[c] * t;
			}
		}

		// Monastery manuscript: flat jewel colours on vellum inside a gold-leaf border with ruled lines.
		Image Illuminated(const Image& src, std::mt19937& rng)
		{
			Image p = Blur(src, 0.8f);
			Posterize(p, 7);
			Saturate(p, 1.25f);
			const Image vellum = Parchment(p.w, p.h, rng);
			const Plane leaf = Noise(p.w, p.h, std::max(16, p.w / 10), std::max(16, p.h / 10), rng);
			const float border = std::min(p.w, p.h) * 0.035f;
			const float gold[3]{ 0.74f, 0.58f, 0.3f }, red[3]{ 0.42f, 0.1f, 0.08f }, blue[3]{ 0.14f, 0.2f, 0.42f };
			for (int y = 0; y < p.h; ++y) {
				for (int x = 0; x < p.w; ++x) {
					float*       v = p.at(x, y);
					const float* pv = vellum.at(x, y);
					for (int c = 0; c < 3; ++c) {
						v[c] = v[c] * 0.78f + pv[c] * 0.22f;  // pigment on vellum
					}
					const float d = std::min({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(p.w - 1 - x), static_cast<float>(p.h - 1 - y) });
					const size_t i = static_cast<size_t>(y) * p.w + x;
					if (d < border) {
						float g[3];
						for (int c = 0; c < 3; ++c) {
							g[c] = gold[c] * (0.75f + 0.5f * leaf[i]);
						}
						// a vine scrolling along the border
						const float along = (x + y) * 0.025f;
						const float off = std::abs(std::sin(along) * border * 0.25f + border * 0.5f - d) / (border * 0.06f);
						Mix(g, red, Clamp01(1.0f - off) * 0.6f);  // a fine vine, smooth-edged
						std::memcpy(v, g, sizeof g);
					} else if (d < border + 3) {
						Mix(v, red, 0.9f);
					} else if (d > border + 6 && d < border + 8) {
						Mix(v, blue, 0.8f);
					}
				}
			}
			return p;
		}

		// Leaded glass: irregular panes of glowing colour held in dark lead came.
		Image StainedGlass(const Image& src, std::mt19937& rng)
		{
			const int                             cell = std::max(12, src.w / 40);
			const int                             gx = src.w / cell + 2, gy = src.h / cell + 2;
			std::uniform_real_distribution<float> jit(0.15f, 0.85f);
			std::vector<std::pair<float, float>>  seeds(static_cast<size_t>(gx) * gy);
			for (int j = 0; j < gy; ++j) {
				for (int i = 0; i < gx; ++i) {
					seeds[static_cast<size_t>(j) * gx + i] = { (i + jit(rng)) * cell, (j + jit(rng)) * cell };
				}
			}
			std::vector<int> owner(static_cast<size_t>(src.w) * src.h);
			std::vector<float> edge(owner.size());
			std::vector<double> sum(seeds.size() * 3, 0.0);
			std::vector<int>    cnt(seeds.size(), 0);
			for (int y = 0; y < src.h; ++y) {
				for (int x = 0; x < src.w; ++x) {
					const int ci = x / cell, cj = y / cell;
					float     best = 1e9f, second = 1e9f;
					int       bi = 0;
					for (int dj = -1; dj <= 1; ++dj) {
						for (int di = -1; di <= 1; ++di) {
							const int i = std::clamp(ci + di, 0, gx - 1), j = std::clamp(cj + dj, 0, gy - 1);
							const auto& s = seeds[static_cast<size_t>(j) * gx + i];
							const float d = std::hypot(s.first - x, s.second - y);
							if (d < best) {
								second = best;
								best = d;
								bi = j * gx + i;
							} else if (d < second) {
								second = d;
							}
						}
					}
					const size_t k = static_cast<size_t>(y) * src.w + x;
					owner[k] = bi;
					edge[k] = second - best;
					const float* p = src.at(x, y);
					for (int c = 0; c < 3; ++c) {
						sum[static_cast<size_t>(bi) * 3 + c] += p[c];
					}
					++cnt[bi];
				}
			}
			Image       out(src.w, src.h);
			const float lead = std::max(1.5f, cell * 0.08f);
			for (size_t k = 0; k < owner.size(); ++k) {
				const int o = owner[k];
				float*    v = &out.px[k * 3];
				if (edge[k] < lead) {
					v[0] = v[1] = v[2] = 0.07f;  // lead
					continue;
				}
				for (int c = 0; c < 3; ++c) {
					v[c] = static_cast<float>(sum[static_cast<size_t>(o) * 3 + c] / std::max(1, cnt[o]));
				}
				const float glow = std::min(1.0f, (edge[k] - lead) / (cell * 0.5f));  // brighter toward each pane's middle
				for (int c = 0; c < 3; ++c) {
					v[c] = Clamp01(v[c] * (0.85f + 0.35f * glow));
				}
			}
			Saturate(out, 1.5f);
			return out;
		}

		// Brush and ink: soft sepia washes on paper, darker edges.
		Image InkWash(const Image& src, std::mt19937& rng)
		{
			Plane       g = BlurPlane(Luma(src), src.w, src.h, std::max(1.5f, src.w / 600.0f));
			const Plane edge = Edges(g, src.w, src.h);
			Image       p = Parchment(src.w, src.h, rng);
			const float ink[3]{ 0.18f, 0.12f, 0.08f };
			for (size_t i = 0; i < g.size(); ++i) {
				const float tone = std::round(Clamp01(g[i]) * 5.0f) / 5.0f;  // a few wash strengths
				const float wash = Clamp01((1 - tone) * 0.85f + std::min(edge[i] * 3.0f, 0.6f));
				Mix(&p.px[i * 3], ink, wash);
			}
			return Blur(p, 0.6f);
		}

		// A carving in grey stone: the picture becomes relief, lit from the upper left.
		Image CarvedStone(const Image& src, std::mt19937& rng)
		{
			const Plane g = BlurPlane(Luma(src), src.w, src.h, std::max(1.0f, src.w / 800.0f));
			const Plane grain = Noise(src.w, src.h, std::max(16, src.w / 6), std::max(16, src.h / 6), rng);
			const Plane blotch = Noise(src.w, src.h, 10, 10, rng);
			Image       p(src.w, src.h);
			for (int y = 0; y < src.h; ++y) {
				for (int x = 0; x < src.w; ++x) {
					const size_t i = static_cast<size_t>(y) * src.w + x;
					const float  gx = g[static_cast<size_t>(y) * src.w + std::min(x + 1, src.w - 1)] - g[static_cast<size_t>(y) * src.w + std::max(x - 1, 0)];
					const float  gy = g[static_cast<size_t>(std::min(y + 1, src.h - 1)) * src.w + x] - g[static_cast<size_t>(std::max(y - 1, 0)) * src.w + x];
					const float  relief = Clamp01(0.5f - (gx + gy) * 4.0f);
					const float  stone = (0.42f + 0.12f * grain[i] + 0.08f * blotch[i]) * (0.55f + 0.9f * relief) * (0.8f + 0.25f * g[i]);
					float*       v = p.at(x, y);
					v[0] = stone * 1.0f;
					v[1] = stone * 0.98f;
					v[2] = stone * 0.93f;
				}
			}
			return p;
		}

		// Dragonfire-scorched parchment: sepia picture, burnt brown-black edges.
		Image Scorched(const Image& src, std::mt19937& rng)
		{
			Image       p = Parchment(src.w, src.h, rng);
			const Plane burnN = Noise(src.w, src.h, 14, 14, rng);
			const float scale = std::min(src.w, src.h) * 0.12f;
			for (int y = 0; y < src.h; ++y) {
				for (int x = 0; x < src.w; ++x) {
					const float* s = src.at(x, y);
					float*       v = p.at(x, y);
					const float  g = Luma3(s);
					const float  sep[3]{ g * 1.07f, g * 0.9f, g * 0.66f };
					for (int c = 0; c < 3; ++c) {
						v[c] = v[c] * (0.35f + 0.65f * Clamp01(sep[c] * 1.1f));
					}
					const float d = std::min({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(src.w - 1 - x), static_cast<float>(src.h - 1 - y) });
					const float burn = Clamp01(1.0f - (d - burnN[static_cast<size_t>(y) * src.w + x] * scale * 0.9f) / scale);
					const float scorch[3]{ 0.32f, 0.17f, 0.06f }, black[3]{ 0.04f, 0.03f, 0.02f };
					Mix(v, scorch, Clamp01(burn * 1.6f));
					Mix(v, black, Clamp01((burn - 0.55f) * 2.5f));
				}
			}
			return p;
		}

		// Temple icon: bold flattened colour, deep umber shadows, gold leaf in the highlights.
		Image GildedIcon(const Image& src, std::mt19937& rng)
		{
			Image       p = Kuwahara(src, std::max(2, src.w / 500));
			const Plane leaf = Noise(p.w, p.h, std::max(16, p.w / 12), std::max(16, p.h / 12), rng);
			const Plane cracks = Ridges(p.w, p.h, std::max(60, p.w / 14), rng);
			Posterize(p, 6);
			const float gold[3]{ 0.9f, 0.7f, 0.3f }, umber[3]{ 0.2f, 0.11f, 0.05f };
			for (size_t i = 0; i < leaf.size(); ++i) {
				float*      v = &p.px[i * 3];
				const float g = Luma3(v);
				float       gl[3];
				for (int c = 0; c < 3; ++c) {
					gl[c] = gold[c] * (0.8f + 0.35f * leaf[i]);
				}
				Mix(v, gl, Clamp01((g - 0.62f) * 2.5f));
				Mix(v, umber, Clamp01((0.3f - g) * 2.0f));
				for (int c = 0; c < 3; ++c) {
					v[c] *= 1 - 0.12f * cracks[i];
				}
			}
			Vignette(p, 0.35f, 2);
			return p;
		}

		// Hermaeus Mora's realm: the picture in sickly green ink on black, with drifting tendrils.
		Image Apocrypha(const Image& src, std::mt19937& rng)
		{
			const Plane g = Luma(src);
			const Plane t1 = Ridges(src.w, src.h, std::max(20, src.w / 40), rng);
			const Plane t2 = Ridges(src.w, src.h, std::max(30, src.w / 25), rng);
			const float dark[3]{ 0.03f, 0.07f, 0.04f }, mid[3]{ 0.2f, 0.33f, 0.16f }, light[3]{ 0.66f, 0.8f, 0.45f };
			Image p(src.w, src.h);
			for (size_t i = 0; i < g.size(); ++i) {
				const float l = Clamp01((g[i] - 0.05f) * 1.25f);
				float*      v = &p.px[i * 3];
				for (int c = 0; c < 3; ++c) {
					v[c] = l < 0.5f ? dark[c] + (mid[c] - dark[c]) * l * 2 : mid[c] + (light[c] - mid[c]) * (l - 0.5f) * 2;
				}
				Mix(v, light, Clamp01(t1[i] * 0.35f + t2[i] * 0.2f) * (1 - l));  // tendrils show in the dark
			}
			Vignette(p, 0.7f, 2);
			return p;
		}

		// An Elder Scroll: the picture as luminous pale-blue light on deep blue, softly glowing.
		Image ElderScroll(const Image& src, std::mt19937& rng)
		{
			Plane       g = Luma(src);
			const Plane glow = BlurPlane(g, src.w, src.h, std::max(4.0f, src.w / 120.0f));
			std::uniform_real_distribution<float> star(0.0f, 1.0f);
			const float deep[3]{ 0.02f, 0.04f, 0.12f }, bright[3]{ 0.75f, 0.9f, 1.0f };
			Image       p(src.w, src.h);
			for (size_t i = 0; i < g.size(); ++i) {
				const float l = Clamp01(std::pow(Clamp01(g[i]), 1.4f) + glow[i] * 0.35f);
				float*      v = &p.px[i * 3];
				for (int c = 0; c < 3; ++c) {
					v[c] = deep[c] + (bright[c] - deep[c]) * l;
				}
				if (star(rng) > 0.9993f) {  // faint motes of light
					v[0] = v[1] = v[2] = 0.95f;
				}
			}
			Vignette(p, 0.5f, 2);
			return p;
		}
	}

	namespace
	{
	struct Grade
		{
			float sat, contrast, lift, gain, tint[3], vignette;
		};
	constexpr Grade grades[]{
			{ 1.0f, 1.0f, 0.0f, 1.0f, { 1.0f, 1.0f, 1.0f }, 0.0f },     // true colours
			{ 0.78f, 0.9f, 0.02f, 0.95f, { 1.03f, 1.0f, 0.93f }, 0.18f },  // Skyrim tone
			{ 0.85f, 0.95f, 0.0f, 0.97f, { 1.1f, 0.96f, 0.76f }, 0.35f },  // candlelit
			{ 0.6f, 0.92f, 0.05f, 0.98f, { 0.92f, 0.98f, 1.07f }, 0.15f }, // Nordic winter
			{ 0.55f, 0.75f, 0.12f, 0.95f, { 1.04f, 1.0f, 0.9f }, 0.1f },   // faded
			{ 0.75f, 1.1f, 0.0f, 0.82f, { 0.95f, 0.92f, 1.03f }, 0.45f },  // moody dusk
			{ 0.55f, 0.95f, 0.04f, 0.82f, { 0.84f, 0.92f, 1.12f }, 0.3f },  // moonlit (Masser and Secunda)
			{ 0.9f, 0.95f, 0.06f, 1.0f, { 1.12f, 1.0f, 0.76f }, 0.2f },    // Sovngarde gold
			{ 0.8f, 1.12f, 0.0f, 0.85f, { 0.8f, 1.04f, 1.14f }, 0.5f },    // Blackreach glow
		};
	}

	namespace
	{
		// the player's adjustments, each -1..1 (0 untouched)
		struct Adjust
		{
			float bright{ 0 }, contrast{ 0 }, gamma{ 0 }, sat{ 0 }, warm{ 0 };
			bool  any{ false };
		};

		Adjust Decode(int a_adj)
		{
			Adjust a;
			float* f[5]{ &a.bright, &a.contrast, &a.gamma, &a.sat, &a.warm };
			for (int i = 0; i < 5; ++i) {
				const int v = (a_adj >> (5 * i)) & 31;
				*f[i] = v ? (v - 16) / 15.0f : 0.0f;
				a.any |= v != 0;
			}
			return a;
		}

		inline void AdjustPixel(const Adjust& a, float& r, float& g, float& b)
		{
			// gentle ends: +100 % brightness and gamma together used to wash a picture out to white
			const float gain = std::exp2(a.bright * 0.5f), con = 1.0f + a.contrast * 0.6f, gam = std::exp2(-a.gamma * 0.6f), sat = 1.0f + a.sat;
			float       v[3]{ r * gain, g * gain, b * gain };
			v[0] *= 1.0f + a.warm * 0.14f;
			v[2] *= 1.0f - a.warm * 0.14f;
			const float l = 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2];
			for (float& c : v) {
				c = l + (c - l) * sat;
				c = (c - 0.5f) * con + 0.5f;
				c = std::pow(std::clamp(c, 0.0f, 1.0f), gam);
			}
			r = v[0], g = v[1], b = v[2];
		}
	}

	namespace
	{
		struct ExtraLook
		{
			float tint[3]{ 1, 1, 1 };
			float tintStrength{ 0 }, vignette{ 0 }, sharp{ 0 };
		};

		ExtraLook DecodeExtra(int a_extra)
		{
			ExtraLook x;
			const int hue = a_extra & 127;
			if (hue > 0) {
				const float h = std::fmod((hue - 1) * 5.0f, 360.0f) / 60.0f;
				const float f = 1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f);
				float       c[3]{};
				switch (static_cast<int>(h)) {
				case 0: c[0] = 1, c[1] = f; break;
				case 1: c[0] = f, c[1] = 1; break;
				case 2: c[1] = 1, c[2] = f; break;
				case 3: c[1] = f, c[2] = 1; break;
				case 4: c[0] = f, c[2] = 1; break;
				default: c[0] = 1, c[2] = f; break;
				}
				for (int i = 0; i < 3; ++i) {
					x.tint[i] = 0.35f + 0.65f * c[i];
				}
				x.tintStrength = ((a_extra >> 7) & 31) / 20.0f;
			}
			x.vignette = ((a_extra >> 12) & 31) / 20.0f;
			x.sharp = (((a_extra >> 17) & 31) - 10) / 10.0f;
			return x;
		}
	}

	void Extra(Image& a_img, int a_extra)
	{
		if (a_extra == 0 || a_img.w < 3 || a_img.h < 3) {
			return;
		}
		const ExtraLook x = DecodeExtra(a_extra);
		if (x.sharp != 0) {  // unsharp mask (or a soft blur) against a 3 x 3 box
			const Image src = a_img;
			for (int y = 1; y < a_img.h - 1; ++y) {
				for (int xx = 1; xx < a_img.w - 1; ++xx) {
					float* v = a_img.at(xx, y);
					for (int c = 0; c < 3; ++c) {
						float b = 0;
						for (int dy = -1; dy <= 1; ++dy) {
							for (int dx = -1; dx <= 1; ++dx) {
								b += src.at(xx + dx, y + dy)[c];
							}
						}
						b /= 9.0f;
						v[c] = Clamp01(x.sharp > 0 ? v[c] + (v[c] - b) * x.sharp * 1.4f : v[c] + (b - v[c]) * -x.sharp);
					}
				}
			}
		}
		if (x.tintStrength > 0) {  // a colour wash over the picture's light and dark
			for (size_t i = 0; i < a_img.px.size(); i += 3) {
				float*      v = &a_img.px[i];
				const float l = 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2];
				for (int c = 0; c < 3; ++c) {
					v[c] = Clamp01(v[c] + (l * x.tint[c] * 1.15f - v[c]) * x.tintStrength);
				}
			}
		}
		if (x.vignette > 0) {
			Vignette(a_img, x.vignette * 0.9f, 2);
		}
	}

	void Tone(Image& a_img, int a_code)
	{
		const Adjust adj = Decode(ToneAdjust(a_code));
		const int    a_tone = ToneIndex(a_code);
		if (adj.any) {
			for (size_t i = 0; i < a_img.px.size(); i += 3) {
				AdjustPixel(adj, a_img.px[i], a_img.px[i + 1], a_img.px[i + 2]);
			}
		}
		if (a_tone <= 0 || a_tone >= static_cast<int>(std::size(grades))) {
			return;
		}
		const Grade& gr = grades[a_tone];
		for (size_t i = 0; i < a_img.px.size(); i += 3) {
			float*      v = &a_img.px[i];
			const float g = 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2];
			for (int c = 0; c < 3; ++c) {
				float t = g + (v[c] - g) * gr.sat;
				t = (t - 0.5f) * gr.contrast + 0.5f;
				t = gr.lift + t * (1 - gr.lift);
				v[c] = Clamp01(t * gr.gain * gr.tint[c]);
			}
		}
		if (gr.vignette > 0) {
			Vignette(a_img, gr.vignette, 2);
		}
	}

	Image Apply(const Image& a_src, int a_filter, std::uint32_t a_seed)
	{
		std::mt19937 rng(a_seed);
		switch (a_filter) {
		case 6:
			return SkyrimPainting(a_src, rng);
		case 7:
			return Fresco(a_src, rng);
		case 8:
			return Woodcut(a_src, rng);
		case 9:
			return Mosaic(a_src, rng);
		case 10:
			return Illuminated(a_src, rng);
		case 11:
			return StainedGlass(a_src, rng);
		case 12:
			return InkWash(a_src, rng);
		case 13:
			return CarvedStone(a_src, rng);
		case 14:
			return Scorched(a_src, rng);
		case 15:
			return GildedIcon(a_src, rng);
		case 16:
			return Apocrypha(a_src, rng);
		case 17:
			return ElderScroll(a_src, rng);
		case 1:
			return Oil(a_src, rng);
		case 2:
			return Watercolor(a_src, rng);
		case 3:
			return Sketch(a_src, rng);
		case 4:
			return Aged(a_src, rng);
		case 5:
			return Woven(a_src);
		default:
			return a_src;
		}
	}

	Image FromRGBA8(const std::uint8_t* a_rgba, int a_w, int a_h, int a_rowPitch)
	{
		Image img(a_w, a_h);
		for (int y = 0; y < a_h; ++y) {
			const std::uint8_t* row = a_rgba + static_cast<size_t>(y) * a_rowPitch;
			for (int x = 0; x < a_w; ++x) {
				for (int c = 0; c < 3; ++c) {
					img.at(x, y)[c] = row[x * 4 + c] / 255.0f;
				}
			}
		}
		return img;
	}

	void ToRGBA8(const Image& a_img, std::uint8_t* a_out, int a_rowPitch)
	{
		for (int y = 0; y < a_img.h; ++y) {
			std::uint8_t* row = a_out + static_cast<size_t>(y) * a_rowPitch;
			for (int x = 0; x < a_img.w; ++x) {
				for (int c = 0; c < 3; ++c) {
					row[x * 4 + c] = static_cast<std::uint8_t>(std::lround(Clamp01(a_img.at(x, y)[c]) * 255.0f));
				}
				row[x * 4 + 3] = 255;
			}
		}
	}
}
