#!/bin/sh
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Werror -O0 -g \
    -I firmware/shared \
    tools/test_cracktro_gti.cpp \
    -o tools/test_cracktro_gti.exe
./tools/test_cracktro_gti.exe
