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

# user-visible words that must never reach a normal user
STRINGS='[(]mine[)]|[(]locked[)]|RELEASE LOCKS|UNCLAIM|Claiming|Not claimed|claimed elsewhere|claimed by another screen|/api/fleet/enroll|/api/fleet/unenroll'
# symbols that must not be linked in
SYMS='pfSendEnroll|pfSendUnenroll|pfWriteAuth|pfElect|pfOrphanCount|doReleaseOrphans'

build() {   # $1 = on|off, $2 = outdir
  rm -rf "$2"; mkdir -p "$2"
  if [ "$1" = on ]; then extra="--build-property compiler.cpp.extra_flags=-DGTI_FLEET=1"; else extra=""; fi
  # shellcheck disable=SC2086
  arduino-cli compile --fqbn "$FQBN" $extra \
    --output-dir "$2" "$ROOT/firmware/$SKETCH" >"$2/build.log" 2>&1 || {
      echo "=== BUILD FAILED ($1) - see $2/build.log ==="; tail -20 "$2/build.log"; exit 1; }
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
exit $rc
