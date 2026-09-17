// Bunny Tamagotchi for the Waveshare ESP32-S3 1.32 inch round AMOLED.
// The display, touch and LVGL port come from the Waveshare example. This file
// is the whole game: sprites, screen layout and the one timer that drives it.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
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
#define SCALE           8                      // 32x32 art becomes 256x256
#define IMG_W           (SPRITE_W * SCALE)
#define IMG_H           (SPRITE_H * SCALE)
#define ICON_SCALE      4                      // 16x16 art becomes 64x64
#define DROP_SCALE      2                      // water drops are smaller than that
#define PROP_MAX        3                      // most props any one action needs
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
#define CHEW_TIMES      4
#define HOP_UP_MS       170
#define HOP_DOWN_MS     210
#define HOP_TIMES       2
#define SHAKE_MS        85                     // one half of a shiver
#define SHAKE_TIMES     6
#define BLINK_SLOT_MS   700
#define CHEER_SHOW_MS   2600
#define SAVE_EVERY_MIN  5.0f                   // ponytail: keeps NVS writes rare

#define ARC_LEN         12                     // radial thickness of a gauge band
#define ARC_BOX         444                    // the gauge's bounding square
#define ARC_R           ((ARC_BOX - ARC_LEN) / 2)   // radius of the middle of the band
#define NOTCH_W         2                      // a hairline across the band
#define NOTCH_LEN       22                     // crosses the band, 5 px proud each side

#define BTN_W           104                    // as large as the round screen allows
#define BTN_H           70
#define BTN_STEP        110                    // gap between button centres
#define BTN_Y           134

#define COL_FUR         0xF2E9D8
#define COL_FOOD        0xFF9E3D
#define COL_FUN         0x5AC8FA
#define COL_TIDY        0x7ED957
#define COL_WATER       0x8FD8FF
#define COL_ALERT       0xFF3B30               // a bar below its notch turns red
#define COL_TRACK       0x1E1E1E
#define COL_MARK        0xFFFFFF

// Each meter: where it sits on the rim, its colour, and the icon the bunny
// shows when this is the need it wants answered.
// LVGL measures arc angles clockwise from 3 o'clock, so on the right half of
// the screen the smaller angle is the higher point. PROPRE is therefore drawn
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
    icon_id_t   icon;
    lv_obj_t   *arc;
};

static Meter meters[] = {
    { 190, 240, false, read_hunger, COL_FOOD, "FAIM",    -150, -104, MOOD_HUNGRY, ICON_CARROT, NULL },
    { 246, 296, false, read_happy,  COL_FUN,  "BONHEUR",    0, -182, MOOD_BORED,  ICON_BALL,   NULL },
    { 302, 352, true,  read_clean,  COL_TIDY, "PROPRE",   152,  -99, MOOD_DIRTY,  ICON_DROP,   NULL },
};
#define METER_COUNT ((int) (sizeof(meters) / sizeof(meters[0])))

// Each button has its own motion and its own face. A child should be able to
// tell what happened with the sound off and the labels unread.
typedef enum { ACT_FEED, ACT_PLAY, ACT_WASH } action_t;

static pet_t          pet;
static lv_image_dsc_t sprite_dsc[STAGE_COUNT][SPR_COUNT];
static lv_image_dsc_t icon_dsc[ICON_COUNT];
static lv_image_dsc_t drop_dsc[ICON_COUNT];             // the same art, drawn smaller
static lv_obj_t      *pet_img, *want_img, *cheer_label;
static lv_obj_t      *prop_img[PROP_MAX];               // carrot, ball, water
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
static int            shown_icon  = -1;
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
    const float v[] = { p->hunger, p->happy, p->clean, p->energy, p->care_min };
    for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++)
        if (!(v[i] >= 0.0f) || !(v[i] <= (i == 4 ? 1.0e9f : 100.0f))) return false;
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
        ESP_LOGI(TAG, "bunny is back: %s, age %u min, care %d min",
                 pet_stage_name(pet.stage), (unsigned) pet.age_min, (int) pet.care_min);
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

// JOUER: two bounces off the floor.
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

// NOURRIR: short nods down to the bowl, in time with the chewing face.
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

// LAVER: the shiver a rabbit does to throw the water off, side to side.
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
        prop_show(0, &icon_dsc[ICON_CARROT], 46, 46, COL_FOOD);
        break;
    case ACT_PLAY:
        // The ball falls as the bunny rises, so the two read as one bounce.
        prop_show(0, &icon_dsc[ICON_BALL], 104, 66, COL_FUN);
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

    switch (action_kind) {
    case ACT_FEED: start_chew();  break;
    case ACT_PLAY: start_hop();   break;
    case ACT_WASH: start_shake(); break;
    }
    props_for_action(action_kind);
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

static void make_button(const char *text, int16_t x, int16_t y, intptr_t which)
{
    lv_obj_t *b = lv_button_create(lv_screen_active());
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_radius(b, BTN_H / 2, 0);         // a full pill, so no sharp corner
                                                      // reaches the round edge
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2E), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x4A4A52), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *) which);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
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

// One timer runs everything. Elapsed time comes from the hardware clock, not
// from counting timer calls, so a busy redraw cannot slow the bunny down.
static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    int64_t now  = esp_timer_get_time();
    float   mins = (float) (now - last_us) / 60000000.0f;
    last_us = now;

    pet_stage_t was_stage = pet.stage;
    pet_tick(&pet, mins);

    since_save += mins;
    if (since_save >= SAVE_EVERY_MIN) {
        state_save();
        since_save = 0.0f;
    }

    if (pet.stage != was_stage) {
        state_save();                           // growing is worth a write of its own
        pending_cheer = true;
        ESP_LOGI(TAG, "the bunny grew into %s", pet_stage_name(pet.stage));
    }

    uint32_t idle = lv_display_get_inactive_time(NULL);
    screen_set_light(idle > OFF_AFTER_MS ? OFF : (idle > DIM_AFTER_MS ? DIM : BRIGHT));
    if (backlight == OFF) {
        action_start = 0;                       // nothing half-drawn survives the dark
        cheer_start  = 0;
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

    for (int i = 0; i < METER_COUNT; i++) {
        const float value = meters[i].read(&pet);
        lv_arc_set_value(meters[i].arc, (int32_t) value);
        lv_obj_set_style_arc_color(meters[i].arc,
                                   lv_color_hex(value < PET_NEED_LOW ? COL_ALERT : meters[i].colour),
                                   LV_PART_INDICATOR);
    }

    // The bunny holds up a picture of the one thing it wants.
    int want = -1;
    for (int i = 0; i < METER_COUNT; i++)
        if (meters[i].mood == mood) want = meters[i].icon;
    if (want != shown_icon) {
        if (want < 0) {
            lv_obj_add_flag(want_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_image_set_src(want_img, &icon_dsc[want]);
            lv_obj_remove_flag(want_img, LV_OBJ_FLAG_HIDDEN);
        }
        shown_icon = want;
    }

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

    if (!sprites_load()) return;                            // no PSRAM, no bunny

    // Three meters around the top of the round screen. LVGL angle 270 is 12 o'clock.
    for (int i = 0; i < METER_COUNT; i++) {
        meters[i].arc = make_arc(meters[i].start, meters[i].end, meters[i].reverse,
                                 meters[i].colour);
        make_notch(&meters[i]);
        make_label(meters[i].label, meters[i].label_x, meters[i].label_y, meters[i].colour);
    }

    pet_img = lv_image_create(scr);
    lv_image_set_src(pet_img, &sprite_dsc[pet.stage][SPR_IDLE_A]);
    lv_obj_align(pet_img, LV_ALIGN_CENTER, 0, -34);
    lv_obj_set_style_image_recolor(pet_img, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_image_recolor_opa(pet_img, LV_OPA_COVER, 0);

    want_img = lv_image_create(scr);
    lv_image_set_src(want_img, &icon_dsc[ICON_CARROT]);
    lv_obj_align(want_img, LV_ALIGN_CENTER, 118, -78);
    lv_obj_set_style_image_recolor(want_img, lv_color_hex(COL_FOOD), 0);
    lv_obj_set_style_image_recolor_opa(want_img, LV_OPA_COVER, 0);
    lv_obj_add_flag(want_img, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PROP_MAX; i++) {
        prop_img[i] = lv_image_create(scr);
        lv_image_set_src(prop_img[i], &icon_dsc[ICON_CARROT]);
        lv_obj_set_style_image_recolor_opa(prop_img[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(prop_img[i], LV_OBJ_FLAG_HIDDEN);
    }

    cheer_label = lv_label_create(scr);
    lv_label_set_text(cheer_label, "");
    lv_obj_set_style_text_color(cheer_label, lv_color_hex(COL_FUR), 0);
    lv_obj_set_style_text_font(cheer_label, &lv_font_montserrat_16, 0);
    lv_obj_align(cheer_label, LV_ALIGN_CENTER, 0, 66);
    lv_obj_add_flag(cheer_label, LV_OBJ_FLAG_HIDDEN);

    // Three buttons in a row across the bottom. The screen is a 233 pixel
    // circle, so the far end of each outer pill sits 219 from the middle and
    // clears the edge. Anything wider than this has to leave the row.
    make_button("NOURRIR", -BTN_STEP, BTN_Y, 0);
    make_button("JOUER",            0, BTN_Y, 1);
    make_button("LAVER",     BTN_STEP, BTN_Y, 2);

    start_bob();
    last_us = esp_timer_get_time();
    lv_timer_create(tick_cb, TICK_MS, NULL);
    ESP_LOGI(TAG, "bunny is awake: %s", pet_stage_name(pet.stage));
}
