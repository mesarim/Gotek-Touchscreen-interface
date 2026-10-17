#!/bin/sh
# Build a GTi sketch from THIS worktree. Usage: sh tools/cbuild.sh Gotek_JC3248
# (~/gotek-tools/pbuild.sh hardcodes a different worktree and would build the wrong tree.)
set -e
cd "$(dirname "$0")/.."
SKETCH="${1:-Gotek_JC3248}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app,USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default"
# The Webby dongles (Super Mini, S3-Zero: ESP32-S3FH4R2) have 4 MB flash and 2 MB quad PSRAM. A 16M/opi
# image flashes or OTAs onto them without complaint and then never boots.
case "$SKETCH" in *Webby*) FQBN="esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=default,PSRAM=enabled,FlashMode=qio,CPUFreq=240,USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default";; esac
arduino-cli compile --fqbn "$FQBN" "firmware/$SKETCH"
