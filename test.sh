#!/bin/sh
# Host self-check for the bunny state machine and the art rasteriser.
# No hardware, no framework.
set -e
CC=${CC:-cc}
SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
[ -d "$SYSROOT" ] && SYSROOT_FLAG="-isysroot $SYSROOT" || SYSROOT_FLAG=""
DIR=$(mktemp -d)
$CC $SYSROOT_FLAG -std=c99 -Wall -Wextra -DPET_TEST components/user_app/pet.c -o "$DIR/pet_test"
"$DIR/pet_test"
$CC $SYSROOT_FLAG -std=c99 -Wall -Wextra -DART_TEST components/user_app/art.c -lm -o "$DIR/art_test"
"$DIR/art_test"
