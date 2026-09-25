# nibble

A Tamagotchi rabbit for the Waveshare ESP32-S3-Touch-AMOLED-1.32
(466 x 466 round AMOLED, SH8601 panel over QSPI, CST820 touch).

A nibble is half a byte, and what a rabbit does all day.

![The screen in three states: content, playing, asleep](docs/screens.png)

Everything on screen is a picture, because it is meant for a child who cannot
read yet: the rabbit's face says what it needs, an icon on the button says
what that button gives, flies gather when it smells, and a gauge below its
notch turns red and breathes. It grows on care given rather than on time spent
holding it.

## Build and flash

    . ~/esp/esp-idf/export.sh
    idf.py build flash monitor

## Test the bunny logic and the art on the Mac

    ./test.sh

`pet.c` and `art.c` have no LVGL and no ESP headers, so they compile on the
host. The self-check runs in a second and fails if the decay, the actions, the
nap, the clamps or the drawing of a shape break.

## Where things are

| File | Holds |
|---|---|
| `components/user_app/pet.c` | the state machine and all tuning numbers |
| `components/user_app/sprites.h` | the art, as shapes: ellipses and tapered strokes |
| `components/user_app/art.c` | draws those shapes into smooth masks, at any size |
| `tools/gen_sprites.py` | builds `sprites.h`; edit a stage here, not by hand |
| `components/user_app/user_app.cpp` | screen layout and the one timer |
| `main/user_config.h` | the board pin map, from the Waveshare example |
| `docs/screens.png` | the picture at the top, drawn by `tools/mock_screen.py` |

Everything under that (display driver, touch, LVGL port) is the Waveshare
example, unchanged.

## The art

    python3 tools/gen_sprites.py            # rewrites sprites.h
    python3 tools/gen_sprites.py BABY YOUNG # also prints those stages to the terminal

The rabbit has no resolution of its own. A frame is a list of two primitives in
a 32 by 32 unit square, an ellipse (sheared sideways, which is how an ear
leans) and a capsule with a radius at each end (a carrot, a whisker, a stroke
of the mouth), each one added to the fur or cut out of it, in order.

`art.c` turns a list into an 8 bit coverage mask at whatever size is asked for:
a pixel half inside the edge is half lit, so the curve of an ear lands where it
really falls instead of on the nearest of 32 columns. The panel is 466 px and
the bunny is 288 of them, so the difference between that and one character per
pixel is the whole point of the screen. It costs half a second at boot, once,
and nothing afterwards.

Every feature of the rabbit is placed from its body ellipse, so a growth stage
is only a body size and an ear size. The `STAGES` table at the top holds all
three.

## The screen, without a board

    /usr/bin/python3 tools/mock_screen.py   # rewrites docs/screens.png

The picture at the top is drawn, not photographed. The rabbit comes from
`gen_sprites.py`, rasterised at the size the board rasterises it, and every
number comes from `user_app.cpp`: the gauge angles, the
band width, the notch at 30, the scale of the art and where each thing sits.
Move something on the screen and move it here, then look at the result before
reaching for the USB cable. Needs Pillow, and the system python3 rather than
the ESP-IDF one.

## What the bunny shows you

Nothing on screen asks a child to read. A low need puts its own face on the
bunny and a picture of what it wants beside its head: a carrot, a ball, a drop
of water. The same picture is on the button, so the game is a match. Flies
gather as TIDY falls, one for every quarter of the gauge lost, so the first
arrives long before the notch and they all leave when you wash. The buttons
stay out of the way until the screen is touched, unless a gauge has fallen
below 75, and then they stay out so that the help is one tap away.

Every answer costs a little on the next gauge: a meal makes a mess, a game
makes an appetite, a bath is no fun. The gauge that pays turns white for a
moment, so a child sees where the cost went. After thirty taps the bunny naps
for a minute, shuts its eyes, shows a ZzZz beside its head and takes no orders
until it wakes.

A fourth gauge sits on the rim below the bunny, in the fur colour rather than
one of the three, with BAT written at its left end. That one is the board's own
battery, and it is the only thing on the screen the child is not asked to
answer: it turns red below 20, which means an adult must charge it.

Apart from the ZzZz over a sleeping bunny, the four gauge words and the
restart question are the only text on the screen, and all of them are aimed at
the adult rather than the child.

## Growth

The pet grows on good deeds, not on time. A deed is answering a need the bunny
actually had, meaning the gauge was below `PET_WANTS` when you pressed the
button. Tapping a contented bunny earns nothing, and a need has to fall again
before it can be answered again, so the rate has a ceiling no amount of
tapping can beat. 16 deeds reach YOUNG, 65 reach ADULT.

This is deliberately not a clock. Paying for powered minutes rewards a child
for staying glued to the device and flattens the battery for nothing. Paying
for deeds rewards them for coming back. Against three short visits a day, the
simulation in the commit history gives YOUNG inside the first day and ADULT
on day two or three.

The catch is the missing clock: the bunny only gets hungry while the board is
powered. Left on with the screen asleep it works as described. Switched off,
it is frozen, and a child returning finds it exactly as they left it.

A baby gets hungry faster than an adult, so growing up makes the bunny easier
to keep.

## Tuning

All of the feel is in the block at the top of `pet.c`. A bar falls to zero in
(100 / rate) minutes. The defaults are set for a child: FOOD empties in 25
minutes, FUN in 30, TIDY in 40, and time away is capped at 90 minutes so
that a night off does not ruin the bunny. For a calmer, adult Tamagotchi pace,
divide the three rates by ten. `SIDE_COST` sets what every answer costs on the
next gauge, and `SLEEP_AFTER` how many taps buy a nap.

## Sleep and power

Untouched, the screen dims at 30 seconds and goes at 60. Going means the panel
itself, not only its emitters: brightness 0 leaves the controller scanning a
black frame, so the firmware sends the display to sleep as well. From there the
chip light sleeps between touches, in three second slices, and the touch chip
keeps scanning on its own: a finger pulls its interrupt line low and the board
is back in about a quarter of a second, with the screen as it was.

Five dark minutes later the board opens the latch that holds its own power and
switches off. The way back is then the PWR button, which closes the latch while
it is held, long enough for the firmware to take over the pin. The bunny is
saved before the power goes, and it is frozen until somebody comes back, which
is what this Tamagotchi does anyway when it is not powered.

None of the sleeping and none of the switching off happens with a USB host
attached: light sleep drops the USB device, a board that cannot be reached
cannot be flashed, and the latch does nothing while the cable holds the rail.
On a cell there is no host, which is exactly when it matters.

Idle with the screen dark, before this, the board drew about 27 mA, which is
about 35 hours on a 1000 mAh cell. Lit, it draws four to six times that.

## Measuring the cell

Nothing on this board measures current, and a USB cable measures the charger
rather than the cell, so the instrument is the cell's own voltage against time.
The firmware writes a point into NVS as it runs and prints the whole run as CSV
at the next boot, when a host is on the USB port.

    (unplug the USB and leave the bunny alone, as long as you can spare)
    (plug the USB back in, then:)
    . ~/esp/esp-idf/export.sh
    esptool.py -p /dev/cu.usbmodem11301 --after hard_reset flash_id >/dev/null; ./serial.sh 8

Every line is `minute,mv,lit`. `lit` is 1 when the screen was on for that
point, so a run somebody picked up halfway is easy to tell from one left alone,
and the last line of the dump gives the fall in mV an hour. The ring holds 256
points: when it fills, every second point goes and the interval doubles, so a
run of any length fits in the same kilobyte and only detail is ever lost.

## Not built yet

- Wi-Fi and NTP. The board has no RTC, so time only passes while it is
  powered. Away time is capped by `MAX_CATCHUP_MIN` in `pet.c`.
- Sound. The board has an ES8311 codec and a speaker header.
- More animals. `gen_sprites.py` draws one rabbit; a second animal is a second
  set of drawing functions and a selector.
- Death.
- A tap game behind PLAY.
- Battery life while the bunny is actually in use. The board sleeps and
  switches itself off now, so what is left is the lit screen, at four to six
  times the idle draw: `BRIGHT` is 255, the maximum, and the CPU runs at
  160 MHz with the power management of ESP-IDF switched off.
