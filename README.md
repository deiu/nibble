# Bunny

A Tamagotchi bunny for the Waveshare ESP32-S3-Touch-AMOLED-1.32
(466 x 466 round AMOLED, SH8601 panel over QSPI, CST820 touch).

## Build and flash

    . ~/esp/esp-idf/export.sh
    idf.py build flash monitor

## Test the bunny logic on the Mac

    ./test.sh

`pet.c` has no LVGL and no ESP headers, so it compiles on the host. The
self-check runs in a second and fails if the decay, the actions, the sleep
cycle or the clamps break.

## Where things are

| File | Holds |
|---|---|
| `components/user_app/pet.c` | the state machine and all tuning numbers |
| `components/user_app/sprites.h` | the art, as ASCII, one character per pixel |
| `tools/gen_sprites.py` | builds `sprites.h`; edit a stage here, not by hand |
| `components/user_app/user_app.cpp` | screen layout and the one timer |
| `main/user_config.h` | the board pin map, from the Waveshare example |

## The art

    python3 tools/gen_sprites.py            # rewrites sprites.h
    python3 tools/gen_sprites.py BABY YOUNG # also prints those stages to the terminal

Every feature of the rabbit is placed from its body ellipse, so a growth stage
is only a body size and an ear size. The `STAGES` table at the top holds all
three. Single pixels in `sprites.h` can still be edited by hand, but the
generator overwrites them.

## Growth

The bunny is a mammal, so it is born, not hatched: BEBE, JEUNE, ADULTE. It
grows only while every need is above the notch drawn on the gauges. Neglect
does not hurt it, it just stops the clock. One hour of cared-for time reaches
JEUNE, six reach ADULTE. A baby gets hungry faster than an adult, so growing
up makes the bunny easier to keep.

Everything below that (display driver, touch, LVGL port) is the Waveshare
example, unchanged.

## Tuning

All of the feel is in the block at the top of `pet.c`. A bar falls to zero in
(100 / rate) minutes. The defaults are set for a child: FAIM empties in 25
minutes, BONHEUR in 30, PROPRE in 40, and time away is capped at 90 minutes so
that a night off does not ruin the bunny. For a calmer, adult Tamagotchi pace,
divide the four rates by ten.

## Not built yet

- Wi-Fi and NTP. The board has no RTC, so time only passes while it is
  powered. Away time is capped by `MAX_CATCHUP_MIN` in `pet.c`.
- Sound. The board has an ES8311 codec and a speaker header.
- More animals. `gen_sprites.py` draws one rabbit; a second animal is a second
  set of drawing functions and a selector.
- Death.
- A tap game behind JOUER.
