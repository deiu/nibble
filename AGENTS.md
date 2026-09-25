# Working on nibble

README.md says what the device is. This file says how to work on it.

Hardware in the loop: the board is usually plugged in. Build, flash and read
the log yourself instead of asking someone to try it.

## The shell

Every command that touches the toolchain needs the environment first, and it
does not survive between calls:

    . ~/esp/esp-idf/export.sh

## Build, flash, read

    . ~/esp/esp-idf/export.sh && idf.py build
    . ~/esp/esp-idf/export.sh && idf.py -p /dev/cu.usbmodem11301 flash
    ./serial.sh 8                           # 8 seconds of log, then it exits

**Always flash after a build that succeeds.** A binary left in `build/` proves
nothing. `idf.py flash` ends with `Hash of data verified` and a hard reset.

Find the port with `ls /dev/cu.usb*` if the one above is not there.

The firmware logs at boot and at events, so a log read after the board has
been running for a while is empty. That is normal, not a fault. To see the
boot lines, reset the board first:

    . ~/esp/esp-idf/export.sh && esptool.py -p /dev/cu.usbmodem11301 --after hard_reset flash_id >/dev/null; ./serial.sh 8

`bunny is back: ...` proves the new build runs and the saved rabbit survived.

## Test on the Mac, not on the board

    ./test.sh

It compiles `pet.c` with `-DPET_TEST` and `art.c` with `-DART_TEST` and runs
the asserts in them: the state machine, then the rasteriser. It takes a second,
so run it after every change to either. Neither file includes LVGL or an ESP
header, and both have to stay that way for this to work. Any change to the
rules gets one more assert in the same block.

## Things that bite

- **`sprites.h` is generated, and it holds shapes rather than pixels.** Edit
  the geometry in `tools/gen_sprites.py`, run `python3 tools/gen_sprites.py`,
  then check `git diff sprites.h` is what you meant. There is nothing to edit
  by hand in it: a coordinate there is one number of a float six-tuple.
- **Two rasterisers draw the same shapes**, `art.c` on the board and
  `coverage()` in `gen_sprites.py` for the mock screen and the terminal
  preview. They agree to within one step of alpha. Change the arithmetic in one
  and change it in the other, or the picture in README.md stops matching the
  panel. `./test.sh` covers the C one.
- **`sdkconfig` is not in git.** Options that have to last go in
  `sdkconfig.defaults`, on their own line: a comment after a value is read as
  part of the value.
- **The saved rabbit is a raw `pet_t` blob in NVS**, accepted only when its
  size matches. Add a field without changing the size and an old blob loads
  into the new field as garbage, so guard new fields in `pet_tick`, where a
  bad value costs nothing. Change the size and every saved rabbit is dropped.
- **The screen is round, 466 px.** The centre is (233, 233) and the gauge band
  starts at radius 210, so nothing of your own may reach that far. The art's
  32x32 unit square is drawn at `SCALE` 9 px per unit, centred at y -24: art
  unit (col, row) is screen (89 + 9*col, 65 + 9*row). A child of `pet_img` is clipped to that
  288 px box, and it moves with the bob, the hop and the shiver.
- **`docs/screens.png` is generated too**, by `tools/mock_screen.py`, which
  copies the layout numbers out of `user_app.cpp`. Move something on the
  screen and move it there as well, or the picture in README.md starts to lie.
  It runs on the system python3 with Pillow, not on the ESP-IDF python.
- **LVGL 9 names.** `lv_obj_remove_flag`, not `lv_obj_clear_flag`.
- **The board never sleeps and never switches off with a USB host attached.**
  `nap()` returns at once when `usb_serial_jtag_is_connected()`, because light
  sleep drops the USB device and takes the flash path with it. So the whole
  power path can only be tested on the cell, and the cell log is how you read
  the result afterwards. The panel sleep at 60 seconds is not guarded and can
  be watched on the desk: the screen goes truly black and a tap brings it back.

## Measuring the cell

README.md says how to run a discharge test. Three things about the log itself:

- **It prints only with a USB host attached.** On the cell there is nobody to
  read it, and a console with no host costs milliseconds a line.
- **It reaches NVS every 5 minutes**, and every minute once the cell is under
  3500 mV, so the last few minutes of a run that ended in a brown-out can be
  missing. The shape survives; the very last point may not.
- **It is never cleared.** A restart shows up as the minute column starting
  again, and the dump marks it. The summary at the end covers the last stretch
  only.
- **A run's first point comes one interval after boot, not at boot.** A dead
  cell takes the board through a reset every few seconds, and a point on each
  of those would fill the ring with nothing and decimate the night away. Only
  an empty log starts at minute 0.

## Commits

One subject per change, in the imperative, with a body that says why rather
than what. No AI attribution of any kind, in commits or in pull requests.
