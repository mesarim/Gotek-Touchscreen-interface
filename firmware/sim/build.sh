#!/bin/bash
# GTi browser simulator build: the unmodified firmware sketch + the sim shim -> wasm32 (zig/clang) -> asyncify (binaryen)
# needs: python3 -m pip install ziglang==0.16.0 ; npm i -g binaryen (wasm-opt) ; arduino-cli with esp32:esp32 3.3.12 + JPEGDEC + PNGdec
#   ./build.sh            preprocess the sketch with arduino-cli, compile, link, asyncify -> build/gti.wasm (and web/gti.wasm)
#   ./build.sh nopre      reuse build/sketch_pre.cpp
#   PUBLISH=../../docs/demo ./build.sh   also copy the page files into the site
set -e
cd "$(dirname "$0")"
FW=${FW:-$(cd ../Gotek_JC3248 2>/dev/null && pwd)}        # the firmware sketch folder (default: firmware/Gotek_JC3248 next to this one)
ZIG="python3 -m ziglang"
OUT=build/obj; mkdir -p $OUT
L=$HOME/Arduino/libraries
INC="-Ishim/include -Isrc -I$L/JPEGDEC/src -I$L/PNGdec/src"
DEF="-DARDUINO=10819 -DNO_SIMD -DSIM_GTI=1 -D_WASI_EMULATED_SIGNAL"
CF="-target wasm32-wasi -O2 -g0 -fno-exceptions -fno-rtti -Wno-everything $INC $DEF"
# 1. the firmware, exactly as arduino-cli hands it to the compiler (auto-prototypes included)
if [ "$1" != "nopre" ]; then
  FQ="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=opi,FlashSize=16M,PartitionScheme=custom"
  arduino-cli compile --fqbn $FQ --preprocess $FW > build/sketch_pre.cpp
fi
python3 tools/sim_sketch.py build/sketch_pre.cpp build/sketch_sim.cpp
mkdir -p src; cp -f $FW/{gti_bufio.h,gti_fatwalk.h,gti_gamestore.h,gti_pathlist.h,gti_sdguard.h,omega_logo.h,retro_assets.h,diag_adf.h,espnow_server.h} src/
objs=""
cc() { local src=$1; local o=$OUT/$(basename $src).o; shift;
  if [ ! -f $o ] || [ $src -nt $o ] || [ "$FORCE" = 1 ]; then echo "  cc $src"; $ZIG c++ $CF "$@" -c $src -o $o; fi; objs="$objs $o"; }
ccc() { local src=$1; local o=$OUT/$(basename $src).o; shift;
  if [ ! -f $o ] || [ $src -nt $o ] || [ "$FORCE" = 1 ]; then echo "  cc $src"; $ZIG cc -target wasm32-wasi -O2 -g0 -Wno-everything $INC $DEF "$@" -c $src -o $o; fi; objs="$objs $o"; }
FORCE=1 cc build/sketch_sim.cpp -std=gnu++17 -include Arduino.h -include sim_sketch_pre.h
for f in WString Print Stream StreamString FS vfs_api sim_posix sim_runtime sim_sdmmc sim_espnow sim_main; do cc shim/src/$f.cpp -std=gnu++17; done
for f in stdlib_noniso ff ffunicode sim_disk; do ccc shim/src/$f.c; done
cc $L/JPEGDEC/src/JPEGDEC.cpp -std=gnu++17
cc $L/PNGdec/src/PNGdec.cpp -std=gnu++17
for f in adler32 crc32 infback inffast inflate inftrees zutil; do ccc $L/PNGdec/src/$f.c; done
EXP="g_sdaccess_magic,g_cap_magic,g_cap_images,g_cap_games,g_cap_fit,g_cap_pct,g_cap_atleast,g_bootMagic,g_bootCount,g_bc_magic,g_bc_stage,g_bc_n,g_bc_psram,g_bc_int,g_bc_failsz,g_bc_failcaps"
echo "  link"
LINKARGS="-target wasm32-wasi -O2 -mexec-model=reactor $objs -o build/gti_raw.wasm -Wl,-z,stack-size=4194304 -Wl,--initial-memory=67108864 -Wl,--max-memory=2147483648 $(for e in ${EXP//,/ }; do printf -- "-Wl,--export=%s " $e; done) -Wl,--export-dynamic -lwasi-emulated-signal"
LDCMD=$($ZIG c++ $LINKARGS -v 2>&1 | grep "^wasm-ld --" | tail -1)
# zig does not pass --wrap through: re-run its wasm-ld command with the allocator wraps (heap accounting)
$ZIG $LDCMD --wrap=malloc --wrap=free --wrap=calloc --wrap=realloc --wrap=aligned_alloc --wrap=posix_memalign
echo "  asyncify"
${WASM_OPT:-wasm-opt} build/gti_raw.wasm -O2 --asyncify \
  --pass-arg=asyncify-imports@env.js_sleep,env.js_restart -o build/gti.wasm
ls -la build/gti.wasm
[ -n "$PUBLISH" ] && cp build/gti.wasm web/gti_core.js web/gti_worker.js web/index.html "$PUBLISH"/ && echo "  published to $PUBLISH"
cp build/gti.wasm web/
