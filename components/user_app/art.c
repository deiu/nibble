// Rasterises the shape lists in sprites.h into 8 bit coverage masks.
//
// Each shape is turned into a signed distance from its edge, in pixels, and a
// pixel whose centre lies half a pixel inside is full, half a pixel outside is
// empty, and in between is the fraction. That single line is the whole reason
// the art is smooth: the edge of an ear lands on a pixel boundary at any size,
// so the mask carries the curve instead of a staircase.
//
// Only the bounding box of each shape is touched, so a frame costs about its
// own area rather than the area of the screen.
#include "art.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The ESP32-S3 has no instruction for a float division or a square root, so
// both come from a library call, and the first draw of the art spent four
// seconds in them. Everything in the pixel loop below is therefore a multiply,
// and this is the one place a root is still needed: the classic seed with two
// Newton steps, good to about a millionth, which is far inside one step of an
// alpha byte.
static inline float rsqrt_fast(float x)
{
    union { float f; uint32_t i; } u = { x };
    u.i = 0x5F3759DFu - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

static inline float sqrt_fast(float x)
{
    return x > 0.0f ? x * rsqrt_fast(x) : 0.0f;
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// One pixel of coverage laid onto the mask: added to the fur, or cut out of it.
static inline void put(uint8_t *row, int px, uint8_t cut, float a)
{
    if (a <= 0.0f) return;
    const uint8_t v = a >= 1.0f ? 255 : (uint8_t) (a * 255.0f);
    if (cut) {
        if (255 - v < row[px]) row[px] = 255 - v;
    } else if (v > row[px]) {
        row[px] = v;
    }
}

// An ellipse, sheared sideways by lean. The middle of a body is the bulk of
// the work and none of it is interesting, so each row is split: the span that
// is solid whatever the arithmetic says is filled outright, and the distance
// is only worked out at the two edges, where it decides the pixel.
static void render_ell(uint8_t *mask, int w, int h, const art_shape_t *s, float k, float inv_k)
{
    const float *p = s->p;
    const float  cx = p[0], cy = p[1], rx = p[2], ry = p[3], lean = p[4];
    if (!(rx > 0.0f) || !(ry > 0.0f)) return;       // an ellipse with no width is
                                                    // nothing, not a filled box
    const float  irx = 1.0f / rx, iry = 1.0f / ry, lean_irx = lean * irx;
    const float  half = 0.5f * inv_k;               // half a pixel, in art units
    const float  inx = rx - half, iny = ry - half;  // the ellipse a pixel in from
    // The inset is taken along each axis rather than along the normal, which is
    // exact enough while the two radii are within about seven of each other.
    // A much flatter ellipse than that would claim edge pixels as solid.
    const int    span = inx > 0.0f && iny > 0.0f;
    const float  in_iny = span ? 1.0f / iny : 0.0f;

    const float ex = rx + fabsf(lean) * ry;         // the shear widens it
    const int   px0 = clampi((int) ((cx - ex) * k) - 1, 0, w);
    const int   px1 = clampi((int) ((cx + ex) * k) + 2, 0, w);
    const int   py0 = clampi((int) ((cy - ry) * k) - 1, 0, h);
    const int   py1 = clampi((int) ((cy + ry) * k) + 2, 0, h);

    for (int py = py0; py < py1; py++) {
        const float ay    = ((float) py + 0.5f) * inv_k;
        const float shear = lean * (cy - ay);
        const float v     = (ay - cy) * iry;
        const float v_iry = v * iry;
        uint8_t    *row   = mask + (size_t) py * w;

        // The solid span: where this row crosses the ellipse one pixel in.
        // Empty unless it is worth skipping, and never empty at both ends of
        // the same pixel, which would leave the loop below on the spot.
        int sx0 = px1, sx1 = px1;
        const float t = (ay - cy) * in_iny;
        if (span && t > -1.0f && t < 1.0f) {
            const float hw  = inx * sqrt_fast(1.0f - t * t);
            const float mid = cx + shear;
            const int f0 = clampi((int) ((mid - hw) * k + 1.0f), px0, px1);
            const int f1 = clampi((int) ((mid + hw) * k), f0, px1);
            if (f1 > f0) {
                sx0 = f0;
                sx1 = f1;
                memset(row + sx0, s->cut ? 0x00 : 0xFF, (size_t) (sx1 - sx0));
            }
        }

        for (int px = px0; px < px1; px++) {
            if (px == sx0) { px = sx1 - 1; continue; }      // already filled
            const float ax = ((float) px + 0.5f) * inv_k;
            const float u  = (ax - shear - cx) * irx;
            const float f  = u * u + v * v;
            // The gradient turns "how far outside the unit circle" into a
            // distance in art units, and it carries the shear with it, so a
            // leaning ear is as clean at its edge as an upright one.
            const float gx = 2.0f * u * irx;
            const float gy = 2.0f * (u * lean_irx + v_iry);
            const float gg = gx * gx + gy * gy;
            const float d  = gg > 1e-12f ? (f - 1.0f) * rsqrt_fast(gg) : -1e9f;
            put(row, px, s->cut, 0.5f - d * k);
        }
    }
}

// The exact distance to a capsule with a radius at each end, which draws a
// carrot, a whisker and a mouth stroke alike. After Inigo Quilez.
static void render_cone(uint8_t *mask, int w, int h, const art_shape_t *s, float k, float inv_k)
{
    const float *p = s->p;
    const float  ax0 = p[0], ay0 = p[1], r1 = p[2], r2 = p[5];
    const float  bax = p[3] - ax0, bay = p[4] - ay0;
    const float  l2  = bax * bax + bay * bay;
    const float  il2 = l2 > 1e-9f ? 1.0f / l2 : 0.0f;       // 0: both ends in one place
    const float  rr  = r1 - r2, rr2 = rr * rr;
    const float  srr = rr < 0.0f ? -1.0f : 1.0f;
    const float  a2  = l2 - rr2;
    const float  rmax = r1 > r2 ? r1 : r2;

    const int px0 = clampi((int) (fminf(ax0 - r1, p[3] - r2) * k) - 1, 0, w);
    const int px1 = clampi((int) (fmaxf(ax0 + r1, p[3] + r2) * k) + 2, 0, w);
    const int py0 = clampi((int) (fminf(ay0 - r1, p[4] - r2) * k) - 1, 0, h);
    const int py1 = clampi((int) (fmaxf(ay0 + r1, p[4] + r2) * k) + 2, 0, h);

    for (int py = py0; py < py1; py++) {
        const float ay  = ((float) py + 0.5f) * inv_k;
        const float pay = ay - ay0;
        uint8_t    *row = mask + (size_t) py * w;

        for (int px = px0; px < px1; px++) {
            const float ax = ((float) px + 0.5f) * inv_k;
            float       d;

            if (il2 == 0.0f) {
                const float dx = ax - ax0;
                d = sqrt_fast(dx * dx + pay * pay) - rmax;
            } else {
                const float pax = ax - ax0;
                const float t   = pax * bax + pay * bay;
                const float z   = t - l2;
                const float qx  = pax * l2 - bax * t;
                const float qy  = pay * l2 - bay * t;
                const float x2  = qx * qx + qy * qy;
                const float kk  = srr * rr2 * x2;
                if ((z > 0.0f ? a2 : -a2) * (z * z * l2) > kk)
                    d = sqrt_fast(x2 + z * z * l2) * il2 - r2;
                else if ((t > 0.0f ? a2 : -a2) * (t * t * l2) < kk)
                    d = sqrt_fast(x2 + t * t * l2) * il2 - r1;
                else
                    d = (sqrt_fast(x2 * a2 * il2) + t * rr) * il2 - r1;
            }
            put(row, px, s->cut, 0.5f - d * k);
        }
    }
}

void art_render(uint8_t *mask, int w, int h, const art_t *art, float k)
{
    const float inv_k = 1.0f / k;

    for (int i = 0; i < art->count; i++) {
        const art_shape_t *s = &art->shape[i];
        if (s->kind == SH_ELL) render_ell(mask, w, h, s, k, inv_k);
        else                   render_cone(mask, w, h, s, k, inv_k);
    }
}

// ---------------------------------------------------------------------------
#ifdef ART_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>

// A disc with a bite out of it, and a bar across the middle: between them they
// exercise both primitives, both operations and the soft edge.
static const art_shape_t CHECK[] = {
    { SH_ELL,  0, { 8.0f, 8.0f, 6.0f, 6.0f, 0.0f, 0.0f } },
    { SH_ELL,  1, { 8.0f, 8.0f, 2.0f, 2.0f, 0.0f, 0.0f } },
    { SH_CONE, 0, { 3.0f, 8.0f, 0.5f, 13.0f, 8.0f, 0.5f } },
};

int main(void)
{
    static uint8_t m[64 * 64];
    const art_t    a = { CHECK, ART_N(CHECK) };
    const int      k = 4, w = 16 * k;               // 16 units at 4 px each

    memset(m, 0, sizeof(m));
    art_render(m, w, w, &a, (float) k);

    assert(m[0] == 0);                                      // outside stays empty
    assert(m[(size_t) (4 * k) * w + 8 * k] == 255);         // fur, well inside the disc
    assert(m[(size_t) (8 * k) * w + 8 * k] == 255);         // the bar crosses the bite
    assert(m[(size_t) (7 * k) * w + 8 * k] == 0);           // the bite itself

    // The edge is a gradient, not a step: the ring of pixels the disc's rim
    // passes through has to hold values that are neither 0 nor 255.
    int soft = 0;
    for (int i = 0; i < w * w; i++)
        if (m[i] > 0 && m[i] < 255) soft++;
    assert(soft > 40);

    // The same art at a quarter of the size is still the same art.
    memset(m, 0, sizeof(m));
    art_render(m, 16, 16, &a, 1.0f);
    assert(m[4 * 16 + 8] == 255);
    assert(m[8 * 16 + 8] > 0);      // the bar is one unit wide: half a pixel here
    assert(m[6 * 16 + 8] < 64);     // and the bite is still a hole

    // A shape with no width draws nothing. It used to draw its whole bounding
    // box, because one over a zero radius is an infinity and the test for
    // "deep inside" let the resulting NaN through.
    static const art_shape_t FLAT[] = { { SH_ELL, 0, { 8.0f, 8.0f, 0.0f, 3.0f, 0.0f, 0.0f } } };
    const art_t flat = { FLAT, ART_N(FLAT) };
    memset(m, 0, sizeof(m));
    art_render(m, 16, 16, &flat, 1.0f);
    for (int i = 0; i < 16 * 16; i++) assert(m[i] == 0);
    assert(m[0] == 0);

    printf("art: %d soft pixels on the rim, both scales agree\n", soft);
    return 0;
}
#endif
