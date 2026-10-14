#!/usr/bin/env bash
set -euo pipefail

mode="${1:-all}"
case "$mode" in
  core|display|all) ;;
  *) echo "Usage: bash scripts/run_host_tests.sh [core|display|all]" >&2; exit 2 ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${TEST_BUILD_DIR:-$repo_root/.pio/host-tests}"
compiler="${CXX:-c++}"
python="${PYTHON:-python3}"
flags=(-std=c++11 -Wall -Wextra -g -fno-omit-frame-pointer
       -fsanitize=address,undefined -fno-sanitize-recover=all)
mkdir -p "$build_dir"

if [[ "$mode" == core || "$mode" == all ]]; then
  "$compiler" "${flags[@]}" -I tests/host -I firmware/lib/mb-rl7023-11 \
    tests/host/test_meter.cpp firmware/lib/mb-rl7023-11/mb_rl7023_11.cpp \
    -o "$build_dir/test-meter"
  "$build_dir/test-meter"
  # Tests supply a fake serial module: no pyserial or hardware is required.
  "$python" -B -m unittest discover -s tests -p 'test_*.py' -v
fi

if [[ "$mode" == display || "$mode" == all ]]; then
  fonts_dir="$repo_root/firmware/.pio/libdeps/m5stick-c/M5StickCPlus/src"
  if [[ ! -f "$fonts_dir/Fonts/Font7srle.c" ]]; then
    echo "M5 fonts are missing. Run: pio pkg install -d firmware -e m5stick-c" >&2
    exit 1
  fi
  mkdir -p "$build_dir/display"
  "$compiler" "${flags[@]}" -I tests/display -I firmware/src -I "$fonts_dir" \
    tests/display/test_display.cpp firmware/src/meter_display.cpp \
    -o "$build_dir/test-display"
  "$build_dir/test-display" "$build_dir/display"
fi
