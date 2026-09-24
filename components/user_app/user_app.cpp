// Bunny Tamagotchi for the Waveshare ESP32-S3 1.32 inch round AMOLED.
// The display, touch and LVGL port come from the Waveshare example. This file
// is the whole game: sprites, screen layout and the one timer that drives it.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bsp.h"
#include "lcd_touch_bsp.h"
#include "lvgl.h"
#include "lvgl_port_bsp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pet.h"
#include "sprites.h"
#include "user_app.h"
#include "user_config.h"

static const char *TAG = "bunny";

I2cMasterBus  i2c_dev(ESP32_SCL_NUM, ESP32_SDA_NUM, 0);
LcdTouchPanel touch_dev(i2c_dev, TOUCH_ADDR);

LcdTouchPanel Custom_GetLcdTouchPanel(void) { return touch_dev; }

// ---------------------------------------------------------------------------
// 9 leaves 24 px between the ear tips and the FUN label, and 13 px
// between the feet and the button row. 10 left about 5 px at each end, which
// is not clearance, it is luck.
#define SCALE           9                      // 32x32 art becomes 288x288
#define IMG_W           (SPRITE_W * SCALE)
#define IMG_H           (SPRITE_H * SCALE)
#define ICON_SCALE      4                      // 16x16 art becomes 64x64
#define DROP_SCALE      2                      // water drops are smaller than that
#define PROP_MAX        3                      // most props any one action needs
#define FLY_MAX         PET_FLY_MAX            // the rule itself lives in pet.c
#define FLY_SCATTER_MS  520
#define PULSE_MS        800                    // half a breath, so 0.6 Hz in all
#define ZZZ_PULSE_MS    1500                   // a sleeper breathes slower than an
                                               // alarmed gauge does
#define HOLD_MS         5000                   // a deliberate press, not a brush
#define HOLD_HINT_MS    900                    // silence before the ring appears
#define HOLD_INSET      30                     // trim the sprite's empty corners out
                                               // of the target
#define CONFIRM_MS      7000                   // an unanswered question withdraws
#define ICON_PX_W       (ICON_W * ICON_SCALE)
#define ICON_PX_H       (ICON_H * ICON_SCALE)

#define DIM_AFTER_MS    30000                  // AMOLED, so dark costs nothing
#define OFF_AFTER_MS    60000
#define BRIGHT          255
#define DIM             25
#define OFF             0
#define TICK_MS         100                    // the chewing face changes faster than
                                               // this, so the timer has to keep up
#define CHEW_MS         260                    // one nod into the bowl
#define CHEW_TIMES      8
#define HOP_UP_MS       170
#define HOP_DOWN_MS     210
#define HOP_TIMES       4
#define SHAKE_MS        85                     // one half of a shiver
#define SHAKE_TIMES     12
#define BLINK_SLOT_MS   700
#define CHEER_SHOW_MS   2600
#define COST_FLASH_MS   900                    // the gauge that just paid for an
                                               // action is marked this long
#define SAVE_EVERY_MIN  5.0f                   // ponytail: keeps NVS writes rare

#define ARC_LEN         12                     // radial thickness of a gauge band
#define ARC_BOX         444                    // the gauge's bounding square
#define ARC_R           ((ARC_BOX - ARC_LEN) / 2)   // radius of the middle of the band
#define NOTCH_W         2                      // a hairline across the band
#define NOTCH_LEN       22                     // crosses the band, 5 px proud each side

// Three buttons on an arc, not in a row. On a round screen the corners are
// the scarce thing: a pill's far end cap is what reaches the bezel, and at
// this radius the outer two have to ride higher to stay inside it.
#define BTN_W           92
#define BTN_H           76                     // room for a 64 px icon inside
#define BTN_OUT_X       122                    // outer buttons, left and right
#define BTN_OUT_Y       100
#define BTN_MID_Y       152
#define BTN_SHOW_MS     4000                   // how long they stay after a touch
#define BTN_FADE_MS     180
#define BTN_NEED_BELOW  75.0f                  // a gauge under this keeps the
                                               // buttons out on its own

// The board's own gauge, on the rim below the bunny that the three needs leave
// empty. LVGL angles run clockwise from 3 o'clock, so 60 to 120 straddles 6
// o'clock, and the reverse mode fills it from the left, the way a battery fills.
#define BAT_ADC_CH      ADC_CHANNEL_3          // GPIO 4, the only pin on the cell
#define BAT_ARC_START   60
#define BAT_ARC_END     120
#define BAT_LOW         20                     // under this the arc turns red
#define BAT_LABEL_X     (-130)                 // left of the arc, inside the rim
#define BAT_LABEL_Y     160                    // and clear of the left button
#define BAT_EVERY_MS    5000                   // a cell moves slower than a bunny
#define BAT_EMPTY_MV    3300                   // a lithium cell with nothing left
#define BAT_FULL_MV     4150                   // and one off the charger

#define COL_FUR         0xF2E9D8
#define COL_FOOD        0xFF9E3D
#define COL_FUN         0x7ED957               // green
#define COL_TIDY        0x5AC8FA               // blue, to agree with the water it
                                               // pours and the drops it shakes off
#define COL_WATER       0x8FD8FF
#define COL_FLY         0x8A8A90
#define COL_ALERT       0xFF3B30               // a bar below its notch turns red
#define COL_TRACK       0x1E1E1E
#define COL_MARK        0xFFFFFF

// Each meter: where it sits on the rim, its colour, and the icon the bunny
// shows when this is the need it wants answered.
// LVGL measures arc angles clockwise from 3 o'clock, so on the right half of
// the screen the smaller angle is the higher point. TIDY is therefore drawn
// in reverse, so that all three gauges fill upward and every notch sits low.
static float read_hunger(const pet_t *p) { return p->hunger; }
static float read_happy (const pet_t *p) { return p->happy;  }
static float read_clean (const pet_t *p) { return p->clean;  }

struct Meter {
    int16_t     start, end;
    bool        reverse;
    float     (*read)(const pet_t *);
    uint32_t    colour;
    const char *label;
    int16_t     label_x, label_y;
    pet_mood_t  mood;
    icon_id_t   icon;                       // what this need looks like as an object
    lv_obj_t   *arc;
    bool        pulsing;
};

static Meter meters[] = {
    { 190, 240, false, read_hunger, COL_FOOD, "FOOD",   -150, -104, MOOD_HUNGRY, ICON_CARROT, NULL, false },
    { 246, 296, false, read_happy,  COL_FUN,  "FUN",       0, -182, MOOD_BORED,  ICON_BALL,   NULL, false },
    { 302, 352, true,  read_clean,  COL_TIDY, "TIDY",   152,  -99, MOOD_DIRTY,  ICON_DROP,   NULL, false },
};
#define METER_COUNT ((int) (sizeof(meters) / sizeof(meters[0])))

// Each button has its own motion and its own face. A child should be able to
// tell what happened with the sound off and the labels unread.
typedef enum { ACT_FEED, ACT_PLAY, ACT_WASH } action_t;

// Every action costs a little on the next gauge round the ring, which pet.c
// charges. A bar that falls on its own tells a child nothing, so the gauge
// that paid is marked white for a moment: a meal costs TIDY, a game costs
// FOOD, a bath costs FUN. Indexed by action_t, holding a meters[] index.
static const int COST_METER[] = { 2, 0, 1 };
static uint32_t  cost_at[METER_COUNT];      // when each meter last paid

static pet_t          pet;
static lv_image_dsc_t sprite_dsc[STAGE_COUNT][SPR_COUNT];
static lv_image_dsc_t icon_dsc[ICON_COUNT];
static lv_image_dsc_t drop_dsc[ICON_COUNT];             // the same art, drawn smaller
static lv_obj_t      *pet_img, *cheer_label, *zzz_label;
static lv_obj_t      *prop_img[PROP_MAX];               // carrot, ball, water
static lv_obj_t      *fly_img[FLY_MAX];
static int            shown_flies = -1;
static lv_obj_t      *buttons[METER_COUNT];
static lv_obj_t      *bat_arc;
static uint32_t       bat_at;                  // when the cell was last read
static int            bat_mv;                  // smoothed, so the arc does not jitter
static lv_obj_t      *confirm_box;
static uint32_t       confirm_start;
static lv_obj_t      *hold_ring;
static uint32_t       press_start;
static bool           hold_armed;              // this press landed on the rabbit
static bool           buttons_shown;
static icon_id_t      shown_carrot;
static bool           pending_cheer;                    // the bunny grew while the
                                                        // screen was off
static nvs_handle_t   nvs_h;
static int64_t        last_us;
static uint32_t       action_start;            // 0 when no action is showing
static uint32_t       action_ms;               // how long this action's face stays
static action_t       action_kind;
static uint32_t       cheer_start;
static sprite_id_t    shown_frame = SPR_COUNT;
static pet_stage_t    shown_stage = STAGE_COUNT;
static pet_stage_t    noted_stage = STAGE_COUNT;   // last stage we celebrated
static float          since_save;
static uint8_t        backlight = BRIGHT;

// ---------------------------------------------------------------------------
// Sprites. The ASCII art is expanded once into 8-bit alpha masks at full size.
// ponytail: whole pixels cost PSRAM and remove every scaling question. All
// three stages are built at boot, so growing costs nothing at the moment it
// happens. Three stages of eight frames is about 1.5 MB of the 8 MB PSRAM.
static bool mask_from_art(lv_image_dsc_t *d, const char *const *art,
                          int w, int h, int scale)
{
    int      out_w = w * scale, out_h = h * scale;
    uint8_t *mask  = (uint8_t *) heap_caps_calloc(1, (size_t) out_w * out_h, MALLOC_CAP_SPIRAM);
    if (!mask) return false;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (art[y][x] != '#') continue;
            for (int by = 0; by < scale; by++)
                memset(mask + (y * scale + by) * out_w + x * scale, 0xFF, scale);
        }

    memset(d, 0, sizeof(*d));
#ifdef LV_IMAGE_HEADER_MAGIC
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
#endif
    d->header.cf     = LV_COLOR_FORMAT_A8;
    d->header.w      = out_w;
    d->header.h      = out_h;
    d->header.stride = out_w;
    d->data_size     = (uint32_t) out_w * out_h;
    d->data          = mask;
    return true;
}

// Returns false if any one mask failed. Every later draw indexes this table
// freely, so a half-filled table is worse than no bunny at all.
static bool sprites_load(void)
{
    for (int s = 0; s < STAGE_COUNT; s++)
        for (int f = 0; f < SPR_COUNT; f++)
            if (!mask_from_art(&sprite_dsc[s][f], SPRITES[s][f], SPRITE_W, SPRITE_H, SCALE)) {
                ESP_LOGE(TAG, "no PSRAM for stage %d frame %d", s, f);
                return false;
            }
    for (int i = 0; i < ICON_COUNT; i++) {
        if (!mask_from_art(&icon_dsc[i], ICONS[i], ICON_W, ICON_H, ICON_SCALE)) {
            ESP_LOGE(TAG, "no PSRAM for icon %d", i);
            return false;
        }
        if (!mask_from_art(&drop_dsc[i], ICONS[i], ICON_W, ICON_H, DROP_SCALE)) {
            ESP_LOGE(TAG, "no PSRAM for small icon %d", i);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Saved state. The size check is the whole compatibility story: change pet_t
// and the old blob is dropped, so the bunny starts fresh instead of corrupt.
// A gauge that is not a normal number between 0 and 100 makes clamp() a no-op
// and the cast in lv_arc_set_value undefined. NaN fails every comparison here,
// which is the point.
static bool gauges_sane(const pet_t *p)
{
    const float v[] = { p->hunger, p->happy, p->clean };
    for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++)
        if (!(v[i] >= 0.0f) || !(v[i] <= 100.0f)) return false;
    return true;
}

static void state_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    pet_init(&pet);
    if (nvs_open("bunny", NVS_READWRITE, &nvs_h) != ESP_OK) {
        ESP_LOGW(TAG, "no NVS, the bunny will forget on every boot");
        return;
    }
    pet_t  saved;
    size_t len = sizeof(saved);
    if (nvs_get_blob(nvs_h, "state", &saved, &len) == ESP_OK && len == sizeof(saved)
        && saved.stage < STAGE_COUNT && gauges_sane(&saved)) {
        pet = saved;
        ESP_LOGI(TAG, "bunny is back: %s, age %u min, %u deeds, food %d fun %d tidy %d",
                 pet_stage_name(pet.stage), (unsigned) pet.age_min, (unsigned) pet.deeds,
                 (int) pet.hunger, (int) pet.happy, (int) pet.clean);
    }
}

static void state_save(void)
{
    if (!nvs_h) return;
    nvs_set_blob(nvs_h, "state", &pet, sizeof(pet));
    nvs_commit(nvs_h);
}

// ---------------------------------------------------------------------------
// Movement. A slow bob so the bunny is never frozen, and a hop when a child
// does something right.
static void set_lift(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *) obj, v, 0);
}

static void start_bob(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pet_img);
    lv_anim_set_exec_cb(&a, set_lift);
    lv_anim_set_values(&a, 0, 6);
    lv_anim_set_duration(&a, 1600);
    lv_anim_set_reverse_duration(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void set_shift(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *) obj, v, 0);
}

static void motion_done(lv_anim_t *a)
{
    LV_UNUSED(a);
    lv_obj_set_style_translate_x(pet_img, 0, 0);
    start_bob();                                // the bob resumes where the motion ends
}

// Up and sideways are separate animations, so starting one does not replace
// the other. Clear both, or a leftover motion finishes over the new one.
static void motion_stop(void)
{
    lv_anim_delete(pet_img, set_lift);
    lv_anim_delete(pet_img, set_shift);
    lv_obj_set_style_translate_x(pet_img, 0, 0);
    lv_obj_set_style_translate_y(pet_img, 0, 0);
}

// PLAY: two bounces off the floor.
static void start_hop(void)
{
    motion_stop();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pet_img);
    lv_anim_set_exec_cb(&a, set_lift);
    lv_anim_set_values(&a, 0, -30);
    lv_anim_set_duration(&a, HOP_UP_MS);
    lv_anim_set_reverse_duration(&a, HOP_DOWN_MS);
    lv_anim_set_repeat_count(&a, HOP_TIMES);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, motion_done);
    lv_anim_start(&a);                          // replaces the bob on the same object
}

// FEED: short nods down to the bowl, in time with the chewing face.
static void start_chew(void)
{
    motion_stop();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pet_img);
    lv_anim_set_exec_cb(&a, set_lift);
    lv_anim_set_values(&a, 0, 12);
    lv_anim_set_duration(&a, CHEW_MS / 2);
    lv_anim_set_reverse_duration(&a, CHEW_MS / 2);
    lv_anim_set_repeat_count(&a, CHEW_TIMES);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, motion_done);
    lv_anim_start(&a);
}

// WASH: the shiver a rabbit does to throw the water off, side to side.
static void start_shake(void)
{
    motion_stop();                              // no bobbing while it shivers

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pet_img);
    lv_anim_set_exec_cb(&a, set_shift);
    lv_anim_set_values(&a, -13, 13);
    lv_anim_set_duration(&a, SHAKE_MS);
    lv_anim_set_reverse_duration(&a, SHAKE_MS);
    lv_anim_set_repeat_count(&a, SHAKE_TIMES);
    lv_anim_set_completed_cb(&a, motion_done);
    lv_anim_start(&a);
}

// Props: the carrot it eats, the ball it plays with, the water it shakes off.
// They are the same three symbols the bunny holds up when it wants something,
// so a child learns one picture per need and meets it again in the action.
static void props_hide(void)
{
    for (int i = 0; i < PROP_MAX; i++) {
        lv_anim_delete(prop_img[i], set_lift);
        lv_obj_set_style_translate_y(prop_img[i], 0, 0);
        lv_obj_add_flag(prop_img[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void prop_show(int i, const lv_image_dsc_t *src, int16_t x, int16_t y, uint32_t colour)
{
    lv_image_set_src(prop_img[i], src);
    lv_obj_align(prop_img[i], LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_image_recolor(prop_img[i], lv_color_hex(colour), 0);
    lv_obj_remove_flag(prop_img[i], LV_OBJ_FLAG_HIDDEN);
}

static void prop_move(int i, int32_t to, uint32_t ms, uint32_t delay, bool back)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, prop_img[i]);
    lv_anim_set_exec_cb(&a, set_lift);
    lv_anim_set_values(&a, 0, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    if (back) lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void props_for_action(action_t kind)
{
    props_hide();
    switch (kind) {
    case ACT_FEED:
        shown_carrot = ICON_CARROT;
        prop_show(0, &icon_dsc[ICON_CARROT], 52, 52, COL_FOOD);
        break;
    case ACT_PLAY:
        // The ball falls as the bunny rises, so the two read as one bounce.
        prop_show(0, &icon_dsc[ICON_BALL], 126, 58, COL_FUN);
        prop_move(0, -44, HOP_UP_MS, HOP_DOWN_MS, true);
        break;
    case ACT_WASH: {
        static const int16_t x[PROP_MAX] = { -96, 10, 96 };
        for (int i = 0; i < PROP_MAX; i++) {
            prop_show(i, &drop_dsc[ICON_DROP], x[i], -84, COL_WATER);
            prop_move(i, 150, 420, (uint32_t) i * 140, false);
        }
        break;
    }
    }
}

// A gauge below its notch breathes instead of sitting still. One full breath
// takes 1.6 seconds: slow enough to read as an animal asking, and far below
// any rate that could trouble a sensitive child.
static void set_arc_opa(void *obj, int32_t v)
{
    lv_obj_set_style_arc_opa((lv_obj_t *) obj, (lv_opa_t) v, LV_PART_INDICATOR);
}

static void pulse_set(int i, bool on)
{
    if (on == meters[i].pulsing) return;
    meters[i].pulsing = on;

    if (!on) {
        lv_anim_delete(meters[i].arc, set_arc_opa);
        lv_obj_set_style_arc_opa(meters[i].arc, LV_OPA_COVER, LV_PART_INDICATOR);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, meters[i].arc);
    lv_anim_set_exec_cb(&a, set_arc_opa);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, PULSE_MS);
    lv_anim_set_reverse_duration(&a, PULSE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *) obj, (lv_opa_t) v, 0);
}

static void fade_done(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *) a->var, LV_OBJ_FLAG_HIDDEN);
}

// Hidden is not the same as transparent: a button at zero opacity still takes
// touches. The flag is what keeps the first tap from feeding the bunny.
static void buttons_set(bool show)
{
    if (show == buttons_shown) return;
    buttons_shown = show;

    for (int i = 0; i < METER_COUNT; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, buttons[i]);
        lv_anim_set_exec_cb(&a, set_opa);
        lv_anim_set_duration(&a, BTN_FADE_MS);
        if (show) {
            lv_obj_remove_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
            lv_anim_set_values(&a, 0, LV_OPA_COVER);
        } else {
            lv_anim_set_values(&a, LV_OPA_COVER, 0);
            lv_anim_set_completed_cb(&a, fade_done);
        }
        lv_anim_start(&a);
    }
}

// Flies. A dirty bunny smells, and the smell is what a child can see: the
// TIDY gauge on its own is an abstraction, three flies circling is not.
//
// The count follows the gauge directly rather than the mood, because mood is
// "the worst need wins". A bunny that is both hungry and filthy should still
// have flies, even while its face is asking for a carrot.

// Two drifts of different lengths, crossed, so no fly repeats the other's
// path and none of them looks like it is on a rail.
static void fly_wander(int i)
{
    static const int32_t  dx[FLY_MAX] = {  26, -22,  30 };
    static const int32_t  dy[FLY_MAX] = { -18,  24,  20 };
    static const uint32_t tx[FLY_MAX] = { 900, 1150, 780 };
    static const uint32_t ty[FLY_MAX] = { 1300, 700, 1020 };

    for (int axis = 0; axis < 2; axis++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, fly_img[i]);
        lv_anim_set_exec_cb(&a, axis ? set_lift : set_shift);
        lv_anim_set_values(&a, 0, axis ? dy[i] : dx[i]);
        lv_anim_set_duration(&a, axis ? ty[i] : tx[i]);
        lv_anim_set_reverse_duration(&a, axis ? ty[i] : tx[i]);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
}

static void fly_arrive(int i)
{
    lv_obj_set_style_translate_x(fly_img[i], 0, 0);
    lv_obj_set_style_translate_y(fly_img[i], 0, 0);
    lv_obj_set_style_opa(fly_img[i], LV_OPA_COVER, 0);
    lv_obj_remove_flag(fly_img[i], LV_OBJ_FLAG_HIDDEN);
    fly_wander(i);
}

// Washing sends them off upward. That flight is the reward for pressing WASH.
static void fly_scatter(int i)
{
    const int32_t y0 = lv_obj_get_style_translate_y(fly_img[i], LV_PART_MAIN);
    lv_anim_delete(fly_img[i], set_shift);
    lv_anim_delete(fly_img[i], set_lift);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, fly_img[i]);
    lv_anim_set_exec_cb(&a, set_lift);
    lv_anim_set_values(&a, y0, y0 - 180);
    lv_anim_set_duration(&a, FLY_SCATTER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, 0);
    lv_anim_set_completed_cb(&a, fade_done);
    lv_anim_start(&a);
}

static void flies_set(int n)
{
    if (n == shown_flies) return;
    const int had = shown_flies < 0 ? 0 : shown_flies;

    for (int i = 0; i < FLY_MAX; i++) {
        if (i < n && i >= had)      fly_arrive(i);
        else if (i >= n && i < had) fly_scatter(i);
    }
    shown_flies = n;
}

static void cheer(const char *text)
{
    lv_label_set_text(cheer_label, text);
    lv_obj_remove_flag(cheer_label, LV_OBJ_FLAG_HIDDEN);
    cheer_start = lv_tick_get();
    if (!cheer_start) cheer_start = 1;
}

// ---------------------------------------------------------------------------
static void screen_set_light(uint8_t level)
{
    if (level == backlight) return;
    if (level == OFF) state_save();             // the child has put it down: the
                                                // best moment to survive a power cut
    if (backlight == OFF && level != OFF) {
        // The first touch only wakes the screen. A child must not feed the
        // bunny by accident just by picking the device up.
        lv_indev_wait_release(lv_indev_get_next(NULL));
        start_bob();
    }
    if (level == OFF) lv_anim_delete(pet_img, set_lift);
    Lcd_SetBacklight(level);
    backlight = level;
}

static void hold_ring_hide(lv_event_t *e);   // defined with the other press handlers

// Starting over. The bunny never leaves on its own, so this is the only way
// back to a baby, and it is guarded twice: a long hold, then a question.
//
// This dialogue is the one place on this screen with words on it, and that is
// deliberate. Everywhere else the pictures are there so a child needs no
// reading; here the reading is the lock, so that the decision to throw away a
// grown bunny is taken by whoever can read the question. The gauge labels are
// the only other text, and they are for the adult too.
static void confirm_hide(void)
{
    confirm_start = 0;
    lv_obj_add_flag(confirm_box, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_no_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    confirm_hide();
}

static void confirm_yes_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    confirm_hide();

    pet_init(&pet);
    state_save();
    noted_stage = pet.stage;                    // a new baby is not a promotion
    shown_stage = STAGE_COUNT;
    shown_frame = SPR_COUNT;
    action_start = 0;
    props_hide();
    flies_set(0);
    motion_stop();
    start_bob();
    ESP_LOGI(TAG, "starting again with a new %s", pet_stage_name(pet.stage));
}

static void hold_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (confirm_start || !hold_armed) return;   // the rabbit, not the whole screen
    hold_ring_hide(NULL);
    buttons_set(false);
    lv_obj_remove_flag(confirm_box, LV_OBJ_FLAG_HIDDEN);
    confirm_start = lv_tick_get();
    if (!confirm_start) confirm_start = 1;
}

// Did this press land on the rabbit, or just somewhere on the glass? Asked
// once, when the finger arrives, so that sliding off the rabbit part way
// through a hold cannot change the answer either way.
static bool press_on_pet(void)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t a;
    lv_obj_get_coords(pet_img, &a);
    return p.x >= a.x1 + HOLD_INSET && p.x <= a.x2 - HOLD_INSET
        && p.y >= a.y1 + HOLD_INSET && p.y <= a.y2 - HOLD_INSET;
}

// A press anywhere that is not a button asks for the buttons.
static void screen_press_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    press_start = lv_tick_get();
    if (!press_start) press_start = 1;
    hold_armed = press_on_pet();
    buttons_set(true);
}

// Five seconds of nothing happening reads as a broken device, so the hold
// draws itself along the bottom rim once it is clear the press is deliberate.
static void hold_ring_hide(lv_event_t *e)
{
    LV_UNUSED(e);
    press_start = 0;
    hold_armed  = false;
    lv_obj_add_flag(hold_ring, LV_OBJ_FLAG_HIDDEN);
}

static void pressing_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!press_start || !hold_armed || confirm_start) return;

    const uint32_t held = lv_tick_elaps(press_start);
    if (held < HOLD_HINT_MS) return;

    const int32_t part = (int32_t) ((held - HOLD_HINT_MS) * 100 / (HOLD_MS - HOLD_HINT_MS));
    lv_arc_set_value(hold_ring, part > 100 ? 100 : part);
    lv_obj_remove_flag(hold_ring, LV_OBJ_FLAG_HIDDEN);
}

static void action_cb(lv_event_t *e)
{
    intptr_t which = (intptr_t) lv_event_get_user_data(e);
    bool     done  = false;

    if (backlight == OFF) return;               // asleep screens do not take orders

    action_kind = (action_t) which;
    switch (action_kind) {
    case ACT_FEED: done = pet_feed(&pet); action_ms = CHEW_MS * CHEW_TIMES;            break;
    case ACT_PLAY: done = pet_play(&pet); action_ms = (HOP_UP_MS + HOP_DOWN_MS) * HOP_TIMES; break;
    case ACT_WASH: done = pet_wash(&pet); action_ms = SHAKE_MS * 2 * SHAKE_TIMES;      break;
    }
    if (!done) return;

    action_start = lv_tick_get();
    if (!action_start) action_start = 1;        // 0 means "nothing showing"
    cost_at[COST_METER[action_kind]] = action_start;

    switch (action_kind) {
    case ACT_FEED: start_chew();  break;
    case ACT_PLAY: start_hop();   break;
    case ACT_WASH: start_shake(); break;
    }
    props_for_action(action_kind);
    buttons_set(false);                         // let the bunny have the stage
}

// The cell hangs on a 1:2 divider into ADC1 channel 3. The pin, the attenuation
// and the doubling are the ones the Waveshare ADC example uses for this board.
// ponytail: curve fitting is the vendor's own choice, so the numbers agree with
// their test sketch and there is nothing of ours to calibrate.
static adc_oneshot_unit_handle_t adc1;
static adc_cali_handle_t         adc_cali;

static void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc1));

    adc_oneshot_chan_cfg_t chan = {};
    chan.atten    = ADC_ATTEN_DB_12;            // the divided cell reaches 2.1 V
    chan.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, BAT_ADC_CH, &chan));

    adc_cali_curve_fitting_config_t cali = {};
    cali.unit_id  = ADC_UNIT_1;
    cali.chan     = BAT_ADC_CH;
    cali.atten    = ADC_ATTEN_DB_12;
    cali.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali, &adc_cali));
}

// Percent of a lithium cell, or -1 when the reading fails. The volts are
// smoothed first: one raw reading moves by a few percent between ticks, and a
// gauge that jitters looks broken.
static int battery_pct(void)
{
    int raw, mv;
    if (adc_oneshot_read(adc1, BAT_ADC_CH, &raw) != ESP_OK)         return -1;
    if (adc_cali_raw_to_voltage(adc_cali, raw, &mv) != ESP_OK)      return -1;

    mv    *= 2;                                 // back through the divider
    bat_mv = bat_mv ? (bat_mv * 3 + mv) / 4 : mv;

    const int pct = (bat_mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

// ---------------------------------------------------------------------------
static lv_obj_t *make_arc(int16_t start, int16_t end, bool reverse, uint32_t colour)
{
    lv_obj_t *a = lv_arc_create(lv_screen_active());
    lv_obj_set_size(a, ARC_BOX, ARC_BOX);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, start, end);
    lv_arc_set_mode(a, reverse ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_range(a, 0, 100);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, ARC_LEN, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, ARC_LEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(colour), LV_PART_INDICATOR);
    return a;
}

// A white notch across the rim at PET_NEED_LOW. Above the notch the bunny is
// content; below it the bar turns red and the bunny asks for help.
//
// This is a thin upright bar turned to lie along the radius, not an arc
// segment: lv_arc only takes whole degrees, and one degree at this radius is
// already four pixels wide.
static void make_notch(const Meter *m)
{
    const float span = (float) (m->end - m->start) * (PET_NEED_LOW / 100.0f);
    const float at   = m->reverse ? (float) m->end - span : (float) m->start + span;
    const float rad  = at * 3.14159265f / 180.0f;

    lv_obj_t *n = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(n);
    lv_obj_set_size(n, NOTCH_W, NOTCH_LEN);
    lv_obj_set_style_bg_color(n, lv_color_hex(COL_MARK), 0);
    lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
    // Turn the upright bar to point away from the middle, which crosses the
    // band at a right angle. Screen up is angle 270, hence the 90 degree turn.
    lv_obj_set_style_transform_pivot_x(n, NOTCH_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(n, NOTCH_LEN / 2, 0);
    lv_obj_set_style_transform_rotation(n, (int32_t) ((at + 90.0f) * 10.0f), 0);
    lv_obj_align(n, LV_ALIGN_CENTER,
                 (int32_t) (ARC_R * cosf(rad)), (int32_t) (ARC_R * sinf(rad)));
}

static void make_label(const char *text, int16_t x, int16_t y, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(lv_screen_active());
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, x, y);
}

// A button carries the picture of the need it answers, in that need's colour.
// The gauge breathes orange, the orange button has a carrot on it: a child who
// cannot read a word can still follow that. No text survives on this screen.
static lv_obj_t *make_button(const Meter *m, int16_t x, int16_t y, intptr_t which)
{
    lv_obj_t *b = lv_button_create(lv_screen_active());
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_radius(b, BTN_H / 2, 0);         // a full pill, so no sharp corner
                                                      // reaches the round edge
    lv_obj_set_style_bg_color(b, lv_color_hex(0x232327), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x4A4A52), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_hex(m->colour), 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_70, 0);
    lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *) which);

    lv_obj_t *pic = lv_image_create(b);
    lv_image_set_src(pic, &icon_dsc[m->icon]);
    lv_obj_set_style_image_recolor(pic, lv_color_hex(m->colour), 0);
    lv_obj_set_style_image_recolor_opa(pic, LV_OPA_COVER, 0);
    lv_obj_center(pic);
    lv_obj_remove_flag(pic, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_opa(b, LV_OPA_TRANSP, 0);     // a touch brings them in
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

// ---------------------------------------------------------------------------
static sprite_id_t frame_for(pet_mood_t mood)
{
    switch (mood) {
    case MOOD_ASLEEP: return SPR_SLEEP;
    case MOOD_HUNGRY: return SPR_HUNGRY;
    case MOOD_BORED:  return SPR_BORED;
    case MOOD_DIRTY:  return SPR_DIRTY;
    default:          return SPR_IDLE_A;
    }
}

// The face that goes with the motion. Feeding alternates an open and a shut
// mouth, which is the whole trick that makes chewing read at this size.
static sprite_id_t action_face(void)
{
    switch (action_kind) {
    case ACT_FEED: return (lv_tick_elaps(action_start) / (CHEW_MS / 2)) % 2 ? SPR_IDLE_A : SPR_EAT;
    case ACT_PLAY: return SPR_PLAY;
    case ACT_WASH: return SPR_IDLE_B;           // eyes shut against the water
    default:       return SPR_IDLE_A;
    }
}

// Where the ZzZz hangs, in sprite pixels from the top left of the art, so the
// numbers can be read straight off sprites.h. The head climbs and widens as
// the bunny grows: the baby's ears start at row 15, the adult's at row 3, so
// the ZzZz moves up for the young one and steps aside for the adult. The
// label is a child of the art, so it is clipped by it: keep x under 25, since
// four letters of Montserrat 24 are 56 px and the art is 288 px wide.
static const lv_point_t ZZZ_AT[STAGE_COUNT] = {
    { 23, 11 },     // BABY:  close over the ear, the art above it is empty
    { 23,  4 },     // YOUNG: higher, the ears are longer
    { 25,  5 },     // ADULT: beside the ear, because there is no room above it
};

// One timer runs everything. Elapsed time comes from the hardware clock, not
// from counting timer calls, so a busy redraw cannot slow the bunny down.
static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    int64_t now  = esp_timer_get_time();
    float   mins = (float) (now - last_us) / 60000000.0f;
    last_us = now;

    pet_tick(&pet, mins);

    since_save += mins;
    if (since_save >= SAVE_EVERY_MIN) {
        state_save();
        since_save = 0.0f;
    }

    // Compared against what was last celebrated, not against this tick's own
    // starting value: a deed done between ticks changes the stage too.
    if (pet.stage != noted_stage) {
        noted_stage = pet.stage;
        state_save();                           // growing is worth a write of its own
        pending_cheer = true;
        ESP_LOGI(TAG, "the bunny grew into %s", pet_stage_name(pet.stage));
    }

    uint32_t idle = lv_display_get_inactive_time(NULL);
    screen_set_light(idle > OFF_AFTER_MS ? OFF : (idle > DIM_AFTER_MS ? DIM : BRIGHT));
    if (confirm_start && lv_tick_elaps(confirm_start) >= CONFIRM_MS) confirm_hide();
    if (backlight == OFF) {
        // Nothing half-drawn survives the dark. Clearing action_start alone
        // left the prop behind, because the only code that hides a prop sits
        // behind that same flag: a carrot stranded on the belly until the
        // next meal, whatever the bunny actually needed.
        action_start = 0;
        cheer_start  = 0;
        props_hide();
        motion_stop();
        lv_obj_add_flag(cheer_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(zzz_label, LV_OBJ_FLAG_HIDDEN);
        buttons_set(false);
        return;
    }

    // The bunny usually grows while nobody is looking. Hold the celebration
    // until a child is there to see it.
    if (pending_cheer) {
        pending_cheer = false;
        cheer(pet_stage_name(pet.stage));
        start_hop();
    }

    pet_mood_t mood = pet_mood(&pet);

    bool wants_help = false;
    for (int i = 0; i < METER_COUNT; i++) {
        const float value = meters[i].read(&pet);
        const bool  low   = value < PET_NEED_LOW;
        if (value < BTN_NEED_BELOW) wants_help = true;
        const bool  paid  = cost_at[i] && lv_tick_elaps(cost_at[i]) < COST_FLASH_MS;
        lv_arc_set_value(meters[i].arc, (int32_t) value);
        lv_obj_set_style_arc_color(meters[i].arc,
                                   lv_color_hex(low ? COL_ALERT : paid ? COL_MARK
                                                                      : meters[i].colour),
                                   LV_PART_INDICATOR);
        pulse_set(i, low);
    }

    // A gauge under BTN_NEED_BELOW keeps the buttons out by itself, so the help
    // is one tap away rather than two. The three cases that hide them on
    // purpose still win: an action has the stage, the hold asks a question, and
    // a sleeping bunny takes no orders anyway.
    if (wants_help && !action_start && !confirm_start && !pet.asleep) buttons_set(true);
    else if (idle > BTN_SHOW_MS)                                      buttons_set(false);

    if (!bat_at || lv_tick_elaps(bat_at) >= BAT_EVERY_MS) {
        const bool first = !bat_at;
        bat_at = lv_tick_get();
        if (!bat_at) bat_at = 1;
        const int pct = battery_pct();
        if (first) ESP_LOGI(TAG, "cell at %d%%, %d mV", pct, bat_mv);
        if (pct >= 0) {
            lv_arc_set_value(bat_arc, pct);
            lv_obj_set_style_arc_color(bat_arc,
                                       lv_color_hex(pct < BAT_LOW ? COL_ALERT : COL_FUR),
                                       LV_PART_INDICATOR);
        }
    }

    flies_set(pet_flies(&pet));

    if (action_start && action_kind == ACT_FEED) {
        const uint32_t part = lv_tick_elaps(action_start) * 3 / action_ms;
        const icon_id_t bite = part == 0 ? ICON_CARROT
                             : part == 1 ? ICON_CARROT_BITTEN : ICON_CARROT_STUB;
        if (bite != shown_carrot) {
            lv_image_set_src(prop_img[0], &icon_dsc[bite]);
            shown_carrot = bite;
        }
    }
    if (action_start && lv_tick_elaps(action_start) >= action_ms) {
        action_start = 0;
        props_hide();
    }
    if (cheer_start && lv_tick_elaps(cheer_start) >= CHEER_SHOW_MS) {
        lv_obj_add_flag(cheer_label, LV_OBJ_FLAG_HIDDEN);
        cheer_start = 0;
    }

    if (pet.asleep) {
        lv_obj_align(zzz_label, LV_ALIGN_TOP_LEFT,
                     ZZZ_AT[pet.stage].x * SCALE, ZZZ_AT[pet.stage].y * SCALE);
        lv_obj_remove_flag(zzz_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(zzz_label, LV_OBJ_FLAG_HIDDEN);
    }

    sprite_id_t want_frame;
    if (pet.asleep)                    want_frame = SPR_SLEEP;
    else if (action_start)             want_frame = action_face();
    else if (mood != MOOD_OK)          want_frame = frame_for(mood);
    else want_frame = ((lv_tick_get() / BLINK_SLOT_MS) % 8 == 0) ? SPR_IDLE_B : SPR_IDLE_A;

    if (want_frame != shown_frame || pet.stage != shown_stage) {
        lv_image_set_src(pet_img, &sprite_dsc[pet.stage][want_frame]);
        shown_frame = want_frame;
        shown_stage = pet.stage;
    }
}

// ---------------------------------------------------------------------------
void user_app_init(void)
{
    state_load();
}

void user_ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, hold_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(scr, pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, hold_ring_hide, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, hold_ring_hide, LV_EVENT_PRESS_LOST, NULL);
    lv_indev_set_long_press_time(lv_indev_get_next(NULL), HOLD_MS);

    if (!sprites_load()) return;                            // no PSRAM, no bunny

    // Three meters around the top of the round screen. LVGL angle 270 is 12 o'clock.
    for (int i = 0; i < METER_COUNT; i++) {
        meters[i].arc = make_arc(meters[i].start, meters[i].end, meters[i].reverse,
                                 meters[i].colour);
        make_notch(&meters[i]);
        make_label(meters[i].label, meters[i].label_x, meters[i].label_y, meters[i].colour);
    }

    // The board's own gauge, on the rim the three needs leave empty. It wears
    // the fur colour rather than one of the three, and it carries no notch:
    // nothing here asks the child to act, it tells the adult when to charge.
    bat_arc = make_arc(BAT_ARC_START, BAT_ARC_END, true, COL_FUR);
    lv_arc_set_value(bat_arc, 0);
    make_label("BAT", BAT_LABEL_X, BAT_LABEL_Y, COL_FUR);
    battery_init();

    pet_img = lv_image_create(scr);
    lv_image_set_src(pet_img, &sprite_dsc[pet.stage][SPR_IDLE_A]);
    lv_obj_align(pet_img, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_image_recolor(pet_img, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_image_recolor_opa(pet_img, LV_OPA_COVER, 0);

    // A child of the bunny, so it bobs, hops and shivers with it.
    zzz_label = lv_label_create(pet_img);
    lv_label_set_text(zzz_label, "ZzZz");
    lv_obj_set_style_text_font(zzz_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(zzz_label, lv_color_hex(COL_FUR), 0);
    lv_obj_add_flag(zzz_label, LV_OBJ_FLAG_HIDDEN);

    // The breath runs forever, awake or not: a hidden label draws nothing, so
    // there is nothing to start and stop when the bunny wakes.
    // ponytail: no start/stop pair, add one if the anim ever costs power
    lv_anim_t zzz_breath;
    lv_anim_init(&zzz_breath);
    lv_anim_set_var(&zzz_breath, zzz_label);
    lv_anim_set_exec_cb(&zzz_breath, set_opa);
    lv_anim_set_values(&zzz_breath, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_duration(&zzz_breath, ZZZ_PULSE_MS);
    lv_anim_set_reverse_duration(&zzz_breath, ZZZ_PULSE_MS);
    lv_anim_set_repeat_count(&zzz_breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&zzz_breath, lv_anim_path_ease_in_out);
    lv_anim_start(&zzz_breath);

    // Props are made after the bunny, so the carrot sits on top of the fur.
    for (int i = 0; i < PROP_MAX; i++) {
        prop_img[i] = lv_image_create(scr);
        lv_image_set_src(prop_img[i], &icon_dsc[ICON_CARROT]);
        lv_obj_set_style_image_recolor_opa(prop_img[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(prop_img[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Flies sit outside the body, where they read against the black.
    static const lv_point_t fly_at[FLY_MAX] = { { -140, -20 }, { 140, -55 }, { -150, 60 } };
    for (int i = 0; i < FLY_MAX; i++) {
        fly_img[i] = lv_image_create(scr);
        lv_image_set_src(fly_img[i], &drop_dsc[ICON_FLY]);
        lv_obj_align(fly_img[i], LV_ALIGN_CENTER, fly_at[i].x, fly_at[i].y);
        lv_obj_set_style_image_recolor(fly_img[i], lv_color_hex(COL_FLY), 0);
        lv_obj_set_style_image_recolor_opa(fly_img[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(fly_img[i], LV_OBJ_FLAG_HIDDEN);
    }

    cheer_label = lv_label_create(scr);
    lv_label_set_text(cheer_label, "");
    lv_obj_set_style_text_color(cheer_label, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_text_font(cheer_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(cheer_label, lv_color_hex(0x101014), 0);
    lv_obj_set_style_bg_opa(cheer_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cheer_label, 10, 0);
    lv_obj_set_style_radius(cheer_label, 16, 0);
    lv_obj_align(cheer_label, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(cheer_label, LV_OBJ_FLAG_HIDDEN);

    // Hidden until a child touches the screen. The far end of the outer pills
    // now reaches 202 of the 233 pixel radius, where the straight row reached
    // 236 and was cut by the bezel. The meters are in the same order as the
    // buttons, so each button takes its picture and its colour straight from
    // the gauge it answers.
    static const lv_point_t btn_at[METER_COUNT] = {
        { -BTN_OUT_X, BTN_OUT_Y }, { 0, BTN_MID_Y }, { BTN_OUT_X, BTN_OUT_Y },
    };
    for (int i = 0; i < METER_COUNT; i++)
        buttons[i] = make_button(&meters[i], btn_at[i].x, btn_at[i].y, i);

    hold_ring = lv_arc_create(scr);
    lv_obj_set_size(hold_ring, ARC_BOX, ARC_BOX);
    lv_obj_center(hold_ring);
    lv_arc_set_bg_angles(hold_ring, 25, 155);   // the bottom rim, free of gauges
    lv_arc_set_range(hold_ring, 0, 100);
    lv_arc_set_value(hold_ring, 0);
    lv_obj_remove_style(hold_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(hold_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(hold_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hold_ring, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hold_ring, lv_color_hex(COL_FUR), LV_PART_INDICATOR);
    lv_obj_add_flag(hold_ring, LV_OBJ_FLAG_HIDDEN);

    // The start again question. Built last, so it sits above everything.
    confirm_box = lv_obj_create(scr);
    lv_obj_set_size(confirm_box, 320, 170);
    lv_obj_center(confirm_box);
    lv_obj_set_style_radius(confirm_box, 24, 0);
    lv_obj_set_style_bg_color(confirm_box, lv_color_hex(0x101014), 0);
    lv_obj_set_style_bg_opa(confirm_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(confirm_box, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_border_width(confirm_box, 2, 0);
    lv_obj_remove_flag(confirm_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(confirm_box, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *ask = lv_label_create(confirm_box);
    lv_label_set_text(ask, "NEW BABY?");
    lv_obj_set_style_text_color(ask, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_text_font(ask, &lv_font_montserrat_16, 0);
    lv_obj_align(ask, LV_ALIGN_TOP_MID, 0, 6);

    struct { const char *text; uint32_t colour; int16_t x; lv_event_cb_t cb; } choice[] = {
        { "NO",  0x4A4A52, -70, confirm_no_cb  },
        { "YES", COL_ALERT,  70, confirm_yes_cb },
    };
    for (unsigned i = 0; i < sizeof(choice) / sizeof(choice[0]); i++) {
        lv_obj_t *b = lv_button_create(confirm_box);
        lv_obj_set_size(b, 116, 56);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, choice[i].x, -4);
        lv_obj_set_style_radius(b, 28, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(choice[i].colour), 0);
        lv_obj_add_event_cb(b, choice[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, choice[i].text);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_center(l);
    }

    noted_stage = pet.stage;                    // do not cheer a stage on every boot
    start_bob();
    last_us = esp_timer_get_time();
    lv_timer_create(tick_cb, TICK_MS, NULL);
    ESP_LOGI(TAG, "bunny is awake: %s", pet_stage_name(pet.stage));
}
