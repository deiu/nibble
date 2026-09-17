#include "pet.h"

// ---------------------------------------------------------------------------
// Tuning. Change these numbers first. Everything about how the bunny feels is
// here, and the right values are the ones that feel right on the real device.
// A full bar falls to zero in (100 / rate) minutes.
//
// These are set for a child: something always needs doing when the device is
// picked up. A calmer adult pace is about a tenth of these rates.
// ---------------------------------------------------------------------------
#define HUNGER_PER_MIN   4.00f   // empty in 25 min
#define HAPPY_PER_MIN    3.30f   // empty in 30 min
#define CLEAN_PER_MIN    2.50f   // empty in 40 min
#define ENERGY_PER_MIN   1.10f   // awake for about 90 min
#define ENERGY_SLEEP     8.00f   // asleep for about 11 min
#define SLEEP_RATE       0.40f   // needs decay this much slower while asleep

#define SLEEP_BELOW     12.0f    // the bunny falls asleep under this energy
#define WAKE_ABOVE      96.0f

#define FEED_HUNGER     25.0f
#define FEED_CLEAN      -5.0f
#define FEED_HAPPY       3.0f
#define PLAY_HAPPY      22.0f
#define PLAY_ENERGY     -8.0f
#define PLAY_HUNGER     -6.0f
#define WASH_CLEAN     100.0f
#define WASH_HAPPY      -3.0f    // no bunny enjoys a bath

// Growth. The clock only runs while every need is above PET_NEED_LOW, so
// neglect does not punish the bunny, it just stops it growing. These are
// minutes of cared-for time, counted from birth.
#define GROW_YOUNG_MIN   60.0f
#define GROW_ADULT_MIN  360.0f

// A baby needs more of everything, an adult less. This is what growth buys a
// child: the bunny becomes easier to look after.
static const float STAGE_NEED_RATE[STAGE_COUNT] = {
    [STAGE_BABY]  = 1.40f,
    [STAGE_YOUNG] = 1.00f,
    [STAGE_ADULT] = 0.80f,
};

// A child who comes back the next morning should find a bunny that missed
// them, not a wreck. The device has no clock while it is off, so this cap is
// also the whole catch-up story.
// ponytail: real elapsed time needs NTP over Wi-Fi.
#define MAX_CATCHUP_MIN 90.0f

static float clamp(float v)
{
    if (v < 0.0f)   return 0.0f;
    if (v > 100.0f) return 100.0f;
    return v;
}

void pet_init(pet_t *p)
{
    p->hunger   = 80.0f;
    p->happy    = 80.0f;
    p->clean    = 100.0f;
    p->energy   = 90.0f;
    p->asleep   = false;
    p->age_min  = 0;
    p->age_part = 0.0f;
    p->stage    = STAGE_BABY;
    p->care_min = 0.0f;
}

static bool well_cared_for(const pet_t *p)
{
    return p->hunger > PET_NEED_LOW && p->happy > PET_NEED_LOW && p->clean > PET_NEED_LOW;
}

void pet_tick(pet_t *p, float minutes)
{
    float scale;

    if (minutes <= 0.0f)           return;
    if (minutes > MAX_CATCHUP_MIN) minutes = MAX_CATCHUP_MIN;

    scale = (p->asleep ? SLEEP_RATE : 1.0f) * STAGE_NEED_RATE[p->stage];

    p->hunger = clamp(p->hunger - HUNGER_PER_MIN * minutes * scale);
    p->happy  = clamp(p->happy  - HAPPY_PER_MIN  * minutes * scale);
    p->clean  = clamp(p->clean  - CLEAN_PER_MIN  * minutes * scale);

    if (p->asleep) {
        p->energy = clamp(p->energy + ENERGY_SLEEP * minutes);
        if (p->energy >= WAKE_ABOVE) p->asleep = false;
    } else {
        p->energy = clamp(p->energy - ENERGY_PER_MIN * minutes);
        if (p->energy <= SLEEP_BELOW) p->asleep = true;
    }

    // Growth is earned. The clock runs only while the bunny is looked after.
    if (well_cared_for(p)) p->care_min += minutes;
    if (p->stage == STAGE_BABY  && p->care_min >= GROW_YOUNG_MIN) p->stage = STAGE_YOUNG;
    if (p->stage == STAGE_YOUNG && p->care_min >= GROW_ADULT_MIN) p->stage = STAGE_ADULT;

    // Ticks are half a second, so whole minutes must be carried between them.
    p->age_part += minutes;
    if (p->age_part >= 1.0f) {
        uint32_t whole = (uint32_t) p->age_part;
        p->age_min  += whole;
        p->age_part -= (float) whole;
    }
}

bool pet_feed(pet_t *p)
{
    if (p->asleep) return false;
    p->hunger = clamp(p->hunger + FEED_HUNGER);
    p->clean  = clamp(p->clean  + FEED_CLEAN);
    p->happy  = clamp(p->happy  + FEED_HAPPY);
    return true;
}

bool pet_play(pet_t *p)
{
    if (p->asleep) return false;
    p->happy  = clamp(p->happy  + PLAY_HAPPY);
    p->energy = clamp(p->energy + PLAY_ENERGY);
    p->hunger = clamp(p->hunger + PLAY_HUNGER);
    return true;
}

bool pet_wash(pet_t *p)
{
    if (p->asleep) return false;
    p->clean = clamp(p->clean + WASH_CLEAN);
    p->happy = clamp(p->happy + WASH_HAPPY);
    return true;
}

pet_mood_t pet_mood(const pet_t *p)
{
    if (p->asleep) return MOOD_ASLEEP;

    // The worst need wins, so the bunny asks for one thing at a time.
    if (p->hunger <= p->happy && p->hunger <= p->clean)
        return p->hunger < PET_NEED_LOW ? MOOD_HUNGRY : MOOD_OK;
    if (p->happy <= p->clean)
        return p->happy  < PET_NEED_LOW ? MOOD_BORED  : MOOD_OK;
    return p->clean < PET_NEED_LOW ? MOOD_DIRTY : MOOD_OK;
}

int pet_flies(const pet_t *p)
{
    const int n = (int) ((100.0f - p->clean) / (100.0f / (PET_FLY_MAX + 1)));
    if (n < 0)            return 0;
    if (n > PET_FLY_MAX)  return PET_FLY_MAX;
    return n;
}

const char *pet_stage_name(pet_stage_t stage)
{
    switch (stage) {
    case STAGE_BABY:  return "BEBE";
    case STAGE_YOUNG: return "JEUNE";
    case STAGE_ADULT: return "ADULTE";
    default:          return "";
    }
}

// ---------------------------------------------------------------------------
#ifdef PET_TEST
#include <assert.h>
#include <stdio.h>

#define NEAR(a, b) (((a) - (b) < 0.01f) && ((b) - (a) < 0.01f))

// Keep every need full, the way an attentive child would.
static void care_for(pet_t *p, float minutes, float step)
{
    for (float t = 0.0f; t < minutes; t += step) {
        pet_tick(p, step);
        if (p->asleep) continue;
        pet_feed(p);
        pet_play(p);
        pet_wash(p);
    }
}

int main(void)
{
    pet_t p;

    // A new bunny is a content, awake baby.
    pet_init(&p);
    assert(!p.asleep);
    assert(p.stage == STAGE_BABY);
    assert(pet_mood(&p) == MOOD_OK);

    // Needs fall at the stated rate, scaled for the stage.
    pet_tick(&p, 1.0f);
    assert(NEAR(p.hunger, 80.0f - HUNGER_PER_MIN * STAGE_NEED_RATE[STAGE_BABY]));

    // Age advances even when every tick is a fraction of a minute.
    pet_init(&p);
    for (int i = 0; i < 300; i++) pet_tick(&p, 1.0f / 120.0f);   // 300 ticks of 0.5 s
    assert(p.age_min == 2);

    // A hungry bunny asks for food, and food answers it.
    pet_init(&p);
    while (p.hunger > 5.0f && !p.asleep) pet_tick(&p, 0.5f);
    if (!p.asleep) {
        assert(pet_mood(&p) == MOOD_HUNGRY);
        assert(pet_feed(&p));
        assert(p.hunger > 5.0f);
    }

    // Each need has its own answer, so the face and the icon can differ.
    pet_init(&p);
    p.hunger = 90.0f; p.clean = 90.0f; p.happy = 10.0f;
    assert(pet_mood(&p) == MOOD_BORED);
    p.happy = 90.0f; p.clean = 10.0f;
    assert(pet_mood(&p) == MOOD_DIRTY);

    // Bars never leave 0..100, whatever we do to them.
    pet_init(&p);
    pet_tick(&p, 100000.0f);
    assert(p.hunger >= 0.0f && p.hunger <= 100.0f);
    assert(p.energy >= 0.0f && p.energy <= 100.0f);
    p.asleep = false;                       // a sleeping bunny refuses the bath
    for (int i = 0; i < 50; i++) pet_wash(&p);
    assert(NEAR(p.clean, 100.0f));

    // Time away is capped, so a night off is the same as ninety minutes off.
    pet_t a, b;
    pet_init(&a); pet_init(&b);
    pet_tick(&a, MAX_CATCHUP_MIN);
    pet_tick(&b, MAX_CATCHUP_MIN * 100.0f);
    assert(NEAR(a.hunger, b.hunger));
    assert(NEAR(a.energy, b.energy));

    // Negative or zero time changes nothing.
    pet_init(&a); b = a;
    pet_tick(&a, -5.0f);
    pet_tick(&a, 0.0f);
    assert(NEAR(a.hunger, b.hunger) && a.age_min == b.age_min);

    // The bunny falls asleep when tired, recovers, and wakes up again.
    pet_init(&p);
    p.energy = SLEEP_BELOW + 1.0f;
    pet_tick(&p, 5.0f);
    assert(p.asleep);
    assert(pet_mood(&p) == MOOD_ASLEEP);
    assert(!pet_feed(&p) && !pet_play(&p) && !pet_wash(&p));  // no poking a sleeping bunny
    float hunger_before = p.hunger;
    pet_tick(&p, 60.0f);
    assert(!p.asleep);
    assert(p.energy > 90.0f);
    // Needs still fall while asleep, but slower than when awake.
    assert(p.hunger < hunger_before);
    assert(p.hunger > hunger_before - 60.0f * HUNGER_PER_MIN);

    // Playing costs energy and food.
    pet_init(&p);
    float e = p.energy, h = p.hunger;
    assert(pet_play(&p));
    assert(p.energy < e && p.hunger < h && p.happy > 80.0f);

    // A neglected bunny does not grow, however long it is left.
    pet_init(&p);
    for (int i = 0; i < 2000; i++) pet_tick(&p, 1.0f);
    assert(p.age_min >= 1000);
    // It starts fed, so a few minutes of care are banked before the bars fall.
    // What matters is that the clock then stops, far short of a growth step.
    assert(p.care_min < GROW_YOUNG_MIN / 2.0f);
    assert(p.stage == STAGE_BABY);

    // A cared-for bunny grows, in order, at the stated times.
    pet_init(&p);
    care_for(&p, GROW_YOUNG_MIN - 5.0f, 0.5f);
    assert(p.stage == STAGE_BABY);
    care_for(&p, 10.0f, 0.5f);
    assert(p.stage == STAGE_YOUNG);
    care_for(&p, GROW_ADULT_MIN - GROW_YOUNG_MIN + 10.0f, 0.5f);
    assert(p.stage == STAGE_ADULT);

    // Growth buys an easier bunny: the adult empties slower than the baby.
    pet_t baby, adult;
    pet_init(&baby);
    pet_init(&adult); adult.stage = STAGE_ADULT;
    pet_tick(&baby, 1.0f);
    pet_tick(&adult, 1.0f);
    assert(adult.hunger > baby.hunger);

    // A fly arrives at each quarter lost: none at full, three at empty.
    pet_init(&p);
    p.clean = 100.0f; assert(pet_flies(&p) == 0);
    p.clean =  76.0f; assert(pet_flies(&p) == 0);
    p.clean =  74.0f; assert(pet_flies(&p) == 1);
    p.clean =  51.0f; assert(pet_flies(&p) == 1);
    p.clean =  49.0f; assert(pet_flies(&p) == 2);
    p.clean =  26.0f; assert(pet_flies(&p) == 2);
    p.clean =  24.0f; assert(pet_flies(&p) == 3);
    p.clean =   0.0f; assert(pet_flies(&p) == 3);
    // The count never leaves the range the screen has objects for.
    for (float v = -10.0f; v <= 110.0f; v += 0.5f) {
        p.clean = v;
        assert(pet_flies(&p) >= 0 && pet_flies(&p) <= PET_FLY_MAX);
    }

    // Every stage has a name for the screen.
    for (int s = 0; s < STAGE_COUNT; s++) assert(pet_stage_name((pet_stage_t) s)[0] != '\0');

    printf("pet self-check passed\n");
    return 0;
}
#endif
