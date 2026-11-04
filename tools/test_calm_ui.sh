#!/bin/sh
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Werror -O0 -g \
    -I firmware/Gotek_JC3248 \
    tools/test_calm_ui.cpp \
    -o tools/test_calm_ui.exe
./tools/test_calm_ui.exe
