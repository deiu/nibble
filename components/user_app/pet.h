// The bunny's state machine. Plain C, no LVGL and no ESP headers, so it
// compiles and self-checks on the host. See test.sh.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A need below this line makes the bunny ask for help: sad face, red gauge,
// breathing. The gauges draw their notch here too, so the mark on screen is
// the same rule the bunny obeys. One number, one rule a child can learn.
#define PET_NEED_LOW 30.0f

// A need below this line is simply one the bunny would welcome having
// answered. Answering it is a good deed, and good deeds are what the bunny
// grows on. Well above PET_NEED_LOW on purpose: a child who tops the bunny up
// early should be rewarded, not made to let it suffer first.
#define PET_WANTS    60.0f

// One fly for every quarter of the PROPRE gauge that has been lost, so a full
// gauge draws none and an empty one draws three. Flies start gathering well
// before the notch: they are the early warning, the sad face is the alarm.
#define PET_FLY_MAX  3

typedef enum {
    MOOD_OK,
    MOOD_HUNGRY,
    MOOD_BORED,
    MOOD_DIRTY,
    MOOD_ASLEEP,
} pet_mood_t;

// A rabbit is a mammal, so it is born, not hatched. No egg.
typedef enum {
    STAGE_BABY,      // a kit: small, with ears folded flat
    STAGE_YOUNG,
    STAGE_ADULT,
    STAGE_COUNT,
} pet_stage_t;

typedef struct {
    float       hunger;    // 100 is a full belly, 0 is starving
    float       happy;
    float       clean;
    float       energy;
    bool        asleep;
    uint32_t    age_min;
    float       age_part;  // minutes not yet whole; ticks are far shorter than a minute
    pet_stage_t stage;
    uint16_t    deeds;     // needs answered; the bunny grows on these
} pet_t;

void        pet_init(pet_t *p);
void        pet_tick(pet_t *p, float minutes);  // negative or huge values are safe
bool        pet_feed(pet_t *p);                 // false when the bunny is asleep
bool        pet_play(pet_t *p);
bool        pet_wash(pet_t *p);
pet_mood_t  pet_mood(const pet_t *p);
const char *pet_stage_name(pet_stage_t stage);  // French, for the screen
int         pet_flies(const pet_t *p);          // 0 to PET_FLY_MAX

#ifdef __cplusplus
}
#endif
