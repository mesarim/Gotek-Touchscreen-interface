#!/bin/sh
# Prove the default panel build does not contain the club layer - in the built
# image, not in the source. Strings survive in flash even when nothing reaches
# them, so grepping the .ino proves nothing about what a user receives.
#
# Two-sided on purpose: the OFF build must be clean AND the ON build must be
# dirty. A checker that cannot fail proves nothing.
set -e
SKETCH="${1:-Gotek_JC3248}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app,USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.fleetcheck"

# Every phrase the club layer can put in front of a user, or write to their card.
# One gap here disables BOTH halves of this check: the symbol half below never fires,
# because these are static functions in a single translation unit and the compiler
# folds them, so nm has nothing to report. This list is effectively the whole test.
# It grew after a review found "locked to another screen" shipping in a default image
# while the checker reported it clean.
STRINGS='locked to another screen|[(]mine[)]|[(]locked[)]|RELEASE LOCKS|UNCLAIM|Claiming|Claimed|Not claimed|Releasing|Released|Not ours|Nothing to release|no locked dongles|claimed elsewhere|claimed by another screen|PANEL_TOKEN|/api/fleet/enroll|/api/fleet/unenroll'
# Kept for the day one of these stops being static; contributes nothing today.
SYMS='pfSendEnroll|pfSendUnenroll|pfWriteAuth|pfElect|pfOrphanCount|doReleaseOrphans'

# The esp32 3.x post-build hook rewrites the tracked artefacts under
# firmware/<sketch>/build/ even with --output-dir. Put them back, so running this
# check never shows up as a source change.
restore_tracked() {
  git -C "$ROOT" checkout -- "firmware/$SKETCH/build" 2>/dev/null || true
}
trap restore_tracked EXIT

build() {   # $1 = on|off, $2 = outdir
  rm -rf "$2"; mkdir -p "$2"
  if [ "$1" = on ]; then extra="--build-property compiler.cpp.extra_flags=-DGTI_FLEET=1"; else extra=""; fi
  rc=0
  # shellcheck disable=SC2086
  arduino-cli compile --fqbn "$FQBN" $extra --output-dir "$2" "$ROOT/firmware/$SKETCH" >"$2/build.log" 2>&1 || rc=$?
  # Judge the artefact, not the exit code. That same post-build hook returns 1 when
  # another process is holding a file in the committed build directory, which reports
  # a failure on a compile and link that actually succeeded.
  if [ ! -s "$2/$SKETCH.ino.bin" ]; then
    echo "=== BUILD FAILED ($1, rc=$rc) - no image produced, see $2/build.log ==="
    grep -m5 "error:" "$2/build.log" || tail -20 "$2/build.log"
    exit 1
  fi
  if [ "$rc" -ne 0 ]; then
    echo "  (arduino-cli returned $rc but the image is there - post-build hook, not the compile)"
  fi
  ls -l "$2/$SKETCH.ino.bin" | awk '{print "  " $5 " bytes"}'
}

scan() {    # $1 = outdir ; echoes every hit, one per line
  strings "$1/$SKETCH.ino.bin" 2>/dev/null | grep -Eo "$STRINGS" | sort -u
  nm -C "$1/$SKETCH.ino.elf" 2>/dev/null | grep -Eo "$SYMS" | sort -u
}

echo "=== building WITHOUT the flag ==="; build off "$OUT/off"
echo "=== building WITH the flag ===";    build on  "$OUT/on"

off_hits="$(scan "$OUT/off" || true)"
on_hits="$(scan "$OUT/on"  || true)"

rc=0
if [ -n "$off_hits" ]; then
  echo "FAIL: the default build still contains the club layer:"
  echo "$off_hits" | sed 's/^/  /'
  rc=1
else
  echo "OK: default build is clean"
fi
if [ -z "$on_hits" ]; then
  echo "FAIL: the fleet build contains none of it either - the check is blind"
  rc=1
else
  echo "OK: fleet build still has it ($(echo "$on_hits" | wc -l | tr -d ' ') markers), so the check can see"
fi

off_size=$(wc -c < "$OUT/off/$SKETCH.ino.bin")
on_size=$(wc -c < "$OUT/on/$SKETCH.ino.bin")
echo "cost of the club layer to a normal user: $((on_size - off_size)) bytes"
exit $rc
