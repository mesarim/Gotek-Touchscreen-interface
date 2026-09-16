#!/bin/sh
# Build a GTi sketch from THIS worktree. Usage: sh tools/cbuild.sh Gotek_JC3248
# (~/gotek-tools/pbuild.sh hardcodes a different worktree and would build the wrong tree.)
set -e
cd "$(dirname "$0")/.."
SKETCH="${1:-Gotek_JC3248}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app,USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default"
arduino-cli compile --fqbn "$FQBN" "firmware/$SKETCH"
