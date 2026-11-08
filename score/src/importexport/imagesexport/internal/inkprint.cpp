/*
 * Score (Laurence Who Studio) — InkPrint
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "inkprint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

namespace {
//! Band-limited noise: random values on a grid of `cellPx`, smoothly interpolated.
class SmoothNoise
{
public:
    SmoothNoise(int w, int h, double cellPx, double amp, std::mt19937& rng)
        : m_cell(std::max(cellPx, 1.0)), m_amp(float(amp))
    {
        m_gw = int(w / m_cell) + 4;
        m_gh = int(h / m_cell) + 4;
        std::normal_distribution<float> n(0.0f, 1.0f);
        m_grid.resize(size_t(m_gw) * m_gh);
        for (float& v : m_grid) {
            v = n(rng);
        }
    }

    float at(int x, int y) const
    {
        const double gx = x / m_cell;
        const double gy = y / m_cell;
        const int ix = int(gx);
        const int iy = int(gy);
        float fx = float(gx - ix);
        float fy = float(gy - iy);
        fx = fx * fx * (3.0f - 2.0f * fx); // smoothstep: no visible creases
        fy = fy * fy * (3.0f - 2.0f * fy);
        const float* r0 = &m_grid[size_t(iy) * m_gw + ix];
        const float* r1 = r0 + m_gw;
        const float a = r0[0] + (r0[1] - r0[0]) * fx;
        const float b = r1[0] + (r1[1] - r1[0]) * fx;
        return (a + (b - a) * fy) * m_amp;
    }

private:
    double m_cell;
    float m_amp;
    int m_gw = 0;
    int m_gh = 0;
    std::vector<float> m_grid;
};

inline float hashNoise(uint32_t x, uint32_t y, uint32_t seed)
{
    // cheap per-pixel gaussian-ish noise (sum of 3 uniforms), deterministic per position
    auto h = [](uint32_t v) {
        v ^= v >> 16;
        v *= 0x7feb352dU;
        v ^= v >> 15;
        v *= 0x846ca68bU;
        v ^= v >> 16;
        return v;
    };
    const uint32_t k = h(x * 0x9E3779B1U ^ h(y + seed * 0x85EBCA77U));
    const float u1 = float(k & 0x3FF) / 1023.0f;
    const float u2 = float((k >> 10) & 0x3FF) / 1023.0f;
    const float u3 = float((k >> 20) & 0x3FF) / 1023.0f;
    return (u1 + u2 + u3 - 1.5f) * 2.0f; // ~unit std
}

template<typename F>
void parallelRows(int h, F&& fn)
{
    const int n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < n; ++t) {
        pool.emplace_back([&, t]() {
            for (int y = t; y < h; y += n) {
                fn(y);
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }
}

void gaussianBlur(std::vector<float>& img, int w, int h, double sigma)
{
    if (sigma < 0.2) {
        return;
    }
    const int r = int(std::ceil(sigma * 3.0));
    std::vector<float> k(size_t(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) {
        k[size_t(i + r)] = float(std::exp(-(i * i) / (2.0 * sigma * sigma)));
        sum += k[size_t(i + r)];
    }
    for (float& v : k) {
        v /= sum;
    }
    std::vector<float> tmp(img.size());
    parallelRows(h, [&](int y) {
        const float* row = &img[size_t(y) * w];
        float* out = &tmp[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) {
                const int xx = std::clamp(x + i, 0, w - 1);
                acc += row[xx] * k[size_t(i + r)];
            }
            out[x] = acc;
        }
    });
    parallelRows(h, [&](int y) {
        float* out = &img[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) {
                const int yy = std::clamp(y + i, 0, h - 1);
                acc += tmp[size_t(yy) * w + x] * k[size_t(i + r)];
            }
            out[x] = acc;
        }
    });
}

// 3x3 cross erosion/dilation on a 0/1 mask
void morph(std::vector<uint8_t>& m, int w, int h, bool dilate)
{
    std::vector<uint8_t> src = m;
    parallelRows(h, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t c = src[size_t(y) * w + x];
            const uint8_t l = x > 0 ? src[size_t(y) * w + x - 1] : c;
            const uint8_t r = x < w - 1 ? src[size_t(y) * w + x + 1] : c;
            const uint8_t u = y > 0 ? src[size_t(y - 1) * w + x] : c;
            const uint8_t d = y < h - 1 ? src[size_t(y + 1) * w + x] : c;
            m[size_t(y) * w + x] = dilate ? uint8_t(c | l | r | u | d) : uint8_t(c & l & r & u & d);
        }
    });
}
}

QImage mu::iex::imagesexport::inkPrint(const QImage& page, double dpi, double strength, unsigned seed)
{
    const QImage gray = page.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width();
    const int h = gray.height();
    if (w < 4 || h < 4) {
        return page;
    }
    const double MM = dpi / 25.4;
    const double S = strength;
    std::mt19937 rng(seed);

    // 1) ink amount, 1 = ink
    std::vector<float> ink(size_t(w) * h);
    parallelRows(h, [&](int y) {
        const uchar* row = gray.constScanLine(y);
        float* out = &ink[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            out[x] = (255.0f - row[x]) / 255.0f;
        }
    });

    // 2) warp: edges drift up to ~0.02 mm, slowly along the page
    const SmoothNoise dx1(w, h, 3.0 * MM, 0.014 * MM * S, rng);
    const SmoothNoise dx2(w, h, 0.7 * MM, 0.005 * MM * S, rng);
    const SmoothNoise dy1(w, h, 3.0 * MM, 0.014 * MM * S, rng);
    const SmoothNoise dy2(w, h, 0.7 * MM, 0.005 * MM * S, rng);
    std::vector<float> warped(ink.size());
    parallelRows(h, [&](int y) {
        float* out = &warped[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            const float sx = x + dx1.at(x, y) + dx2.at(x, y);
            const float sy = y + dy1.at(x, y) + dy2.at(x, y);
            const int ix = int(std::floor(sx));
            const int iy = int(std::floor(sy));
            if (ix < 0 || iy < 0 || ix >= w - 1 || iy >= h - 1) {
                out[x] = 0.0f;
                continue;
            }
            const float fx = sx - ix;
            const float fy = sy - iy;
            const float* r0 = &ink[size_t(iy) * w + ix];
            const float* r1 = r0 + w;
            const float a = r0[0] + (r0[1] - r0[0]) * fx;
            const float b = r1[0] + (r1[1] - r1[0]) * fx;
            out[x] = a + (b - a) * fy;
        }
    });
    ink.clear();
    ink.shrink_to_fit();

    // 3) soften so the threshold can move edges by fractions of a pixel
    gaussianBlur(warped, w, h, 0.03 * MM);

    // 4) classic: gentle swelling/thinning of strokes + fine edge grain
    const SmoothNoise t1(w, h, 1.2 * MM, 0.11 * S, rng);
    const SmoothNoise t2(w, h, 4.0 * MM, 0.05 * S, rng);
    const float fineAmp = float(0.07 * S);
    std::vector<uint8_t> mask(warped.size());
    parallelRows(h, [&](int y) {
        const float* in = &warped[size_t(y) * w];
        uint8_t* out = &mask[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            const float t = 0.5f + t1.at(x, y) + t2.at(x, y) + hashNoise(uint32_t(x), uint32_t(y), seed) * fineAmp;
            out[x] = (in[x] > t && in[x] > 0.08f) ? 1 : 0;
        }
    });
    warped.clear();
    warped.shrink_to_fit();

    // 5) drop isolated specks (dust) and pinholes
    morph(mask, w, h, false);
    morph(mask, w, h, true);
    morph(mask, w, h, true);
    morph(mask, w, h, false);

    QImage out(w, h, QImage::Format_Mono);
    out.setColor(0, qRgb(255, 255, 255));
    out.setColor(1, qRgb(0, 0, 0));
    out.setDotsPerMeterX(page.dotsPerMeterX());
    out.setDotsPerMeterY(page.dotsPerMeterY());
    for (int y = 0; y < h; ++y) {
        uchar* row = out.scanLine(y);
        std::fill_n(row, out.bytesPerLine(), uchar(0));
        const uint8_t* m = &mask[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            if (m[x]) {
                row[x >> 3] |= uchar(0x80 >> (x & 7));
            }
        }
    }
    return out;
}
