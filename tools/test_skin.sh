#!/bin/sh
set -e
cd "$(dirname "$0")/.."
for t in tools/test_skin_*.cpp; do
  b="${t%.cpp}"
  g++ -std=c++17 -Wall -Wextra -Werror -O0 -g \
      -I firmware/shared \
      "$t" -o "$b.exe"
  "./$b.exe"
done
