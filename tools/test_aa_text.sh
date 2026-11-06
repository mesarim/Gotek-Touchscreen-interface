#!/bin/sh
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Werror -O0 -g \
    -I firmware/Gotek_JC3248 \
    tools/test_aa_text.cpp \
    -o tools/test_aa_text.exe
./tools/test_aa_text.exe
