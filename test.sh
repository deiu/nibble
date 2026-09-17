#!/bin/sh
# Host self-check for the bunny state machine. No hardware, no framework.
set -e
CC=${CC:-cc}
SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
[ -d "$SYSROOT" ] && SYSROOT_FLAG="-isysroot $SYSROOT" || SYSROOT_FLAG=""
OUT=$(mktemp -d)/pet_test
$CC $SYSROOT_FLAG -std=c99 -Wall -Wextra -DPET_TEST components/user_app/pet.c -o "$OUT"
"$OUT"
