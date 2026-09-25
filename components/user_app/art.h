// The rasteriser for the art in sprites.h. Plain C, no LVGL and no ESP
// header, so it compiles and self-checks on the host. See test.sh.
//
// A frame is a list of two primitives in the art's own unit square. Nothing
// here knows how big the screen is: the caller asks for so many pixels per
// unit and gets smooth edges at that size, which is why the bunny is not
// blocky on a 466 px panel.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SH_ELL,     // p: cx, cy, rx, ry, lean (a shear, for an ear), unused
    SH_CONE,    // p: x0, y0, r0, x1, y1, r1 (a stroke, round at both ends)
};

typedef struct {
    uint8_t kind;
    uint8_t cut;        // 1 takes the shape out of what is under it, 0 adds it
    float   p[6];
} art_shape_t;

typedef struct {
    const art_shape_t *shape;
    uint16_t           count;
} art_t;

#define ART_N(a) ((uint16_t) (sizeof(a) / sizeof((a)[0])))

// Draws into a w by h alpha mask that the caller has already zeroed. k is how
// many pixels one art unit is worth.
void art_render(uint8_t *mask, int w, int h, const art_t *art, float k);

#ifdef __cplusplus
}
#endif
