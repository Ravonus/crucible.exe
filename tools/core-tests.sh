#!/bin/sh
# Checks for the rules core (core/):
#   sh tools/core-tests.sh test       host tests (clang or gcc, ASan + UBSan): sessions, saves, grants, story runs,
#                                     lost pieces, encounters, the saga, and byte agreement with core/test/reference in both
#                                     SRAM layouts
#   sh tools/core-tests.sh sm83       compile every core file for SM83 (banked), report code size and stack per file,
#                                     fail on multiply/divide helper calls or anything outside its bank
#   sh tools/core-tests.sh sm83-test  run the scenarios (core/test/sm83) on the host and on a real SM83 core
#                                     (PyBoy, headless) and compare the results byte for byte
#   sh tools/core-tests.sh            all three
# Environment: CC (default clang), GBDK_HOME (default ~/.cache/gbdk-4.5.0, see tools/install-gbdk.sh), PYTHON
# (default python3).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# Repo-relative paths only: SDCC aborts on any argument containing ".exe", and clones are named crucible.exe.
CORE=core
OUT=build/core-tests
CC="${CC:-clang}"
LCC="${GBDK_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/gbdk-4.5.0}/bin/lcc"
PYTHON="${PYTHON:-python3}"
REF="$CORE/test/reference"
SRC="$CORE/src/cru_tables.c $CORE/src/cru_play.c $CORE/src/cru_feats.c $CORE/src/cru_save.c $CORE/src/cru_records.c
  $CORE/src/cru_bench.c $CORE/src/cru_text.c $CORE/src/cru_grant.c $CORE/src/cru_reset.c $CORE/src/cru_story.c
  $CORE/src/cru_saga.c $CORE/src/cru_lost.c $CORE/src/cru_encounter.c"
SANITIZE="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
mkdir -p "$OUT"

host_test() {
  flags="-std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion $SANITIZE -I$CORE/include -I$CORE/src"
  for f in $SRC; do
    # shellcheck disable=SC2086
    $CC $flags -Werror -c "$f" -o "$OUT/$(basename "$f" .c).host.o"
  done
  tflags="-std=c99 -O1 -g -Wall -Wextra $SANITIZE -I$CORE/include -I$CORE/src -I$CORE/test"
  for t in core:test_core.c,test_save.c,test_main.c grant:test_grant.c story:test_story.c lost:test_lost.c \
    encounter:test_encounter.c saga:test_saga.c; do
    name="${t%%:*}"
    files=""
    for f in $(echo "${t#*:}" | tr ',' ' '); do files="$files $CORE/test/$f"; done
    # shellcheck disable=SC2086
    $CC $tflags -o "$OUT/cru_${name}_tests" $SRC "$CORE/test/harness.c" $files
    "$OUT/cru_${name}_tests"
  done
  # The reference play module is old, warning-heavy C kept verbatim as the oracle, hence -w.
  for layout in 32 128; do
    define=""
    [ "$layout" = 128 ] && define="-DPLAY_SRAM_128K"
    ref="-std=c99 -O1 -g -w -DPLAY_HOST -DPLAY_TEST $define -I$REF -I$REF/harness"
    # shellcheck disable=SC2086
    $CC $ref -c "$REF/crucible_play.c" -o "$OUT/ref_play_$layout.o"
    # shellcheck disable=SC2086
    $CC $ref -c "$REF/harness/play_harness.c" -o "$OUT/ref_harness_$layout.o"
    # shellcheck disable=SC2086
    $CC $tflags -DPLAY_HOST -DPLAY_TEST $define -I"$REF" -I"$REF/harness" -o "$OUT/cru_diff_$layout" $SRC "$CORE/test/harness.c" \
      "$CORE/test/test_diff.c" "$OUT/ref_play_$layout.o" "$OUT/ref_harness_$layout.o"
    "$OUT/cru_diff_$layout"
  done
}

sm83() {
  o="$OUT/sm83"
  mkdir -p "$o"
  rm -f "$o"/*.o
  status=0
  total=0
  defs="-DCORE_BANKED=BANKED -DCORE_LOCAL=BANKED"
  printf "  %-12s %7s %6s %9s  %s\n" file "ROM B" "RAM B" "max frame" "runtime helpers called"
  for f in $SRC "$CORE/test/sm83/rom_tables.c"; do
    b="$(basename "$f" .c)"
    [ "$b" = cru_tables ] && continue # compiled inside rom_tables.c, in the same bank as its tables
    # shellcheck disable=SC2086
    "$LCC" -c -Wa-l -Wf--max-allocs-per-node50000 $defs -Wf-bo255 -I"$CORE/include" -o "$o/$b.o" "$f" 2>&1 |
      grep -v 'warning 110\|^$' || true
    code=$(awk '$1=="A" && $2 ~ /^_CODE_[0-9]+$/ {print $4}' "$o/$b.o")
    code=$(printf "%d" "0x${code:-0}")
    ram=0
    for a in _DATA _BSS _INITIALIZED _HRAM _CODE _HOME; do
      s=$(awk -v a="$a" '$1=="A" && $2==a {print $4}' "$o/$b.o")
      [ -n "$s" ] && ram=$((ram + $(printf "%d" "0x$s")))
    done
    frame=$(grep -oE 'add[[:space:]]+sp, #-[0-9]+' "$o/$b.asm" | grep -oE '[0-9]+$' | sort -n | tail -1 || true)
    calls=$(grep -oE 'call[[:space:]]+___[_a-z0-9]+' "$o/$b.asm" | awk '{print $2}' | sort -u | tr '\n' ' ' || true)
    printf "  %-12s %7d %6d %9s  %s\n" "$b" "$code" "$ram" "${frame:-0}" "${calls:-none}"
    total=$((total + code))
    if grep -qiE 'call[[:space:]]+_*(mul|div|mod)[a-z]*' "$o/$b.asm"; then
      echo "  $b: multiply/divide helper called"
      status=1
    fi
    [ "$ram" -eq 0 ] || { echo "  $b: code or RAM outside its bank"; status=1; }
    [ "$code" -le 16384 ] || { echo "  $b: larger than a 16 KB bank"; status=1; }
  done
  printf "  %-12s %7d  (rom_tables = cru_tables.c + the world tables)\n" total "$total"
  # A linked cartridge (MBC5 + RAM + battery, CGB) proves the banked calls resolve.
  # shellcheck disable=SC2086
  "$LCC" -c -Wf--max-allocs-per-node50000 $defs -I"$CORE/include" -o "$o/rom_main.o" "$CORE/test/sm83/rom_main.c"
  "$LCC" -Wm-yC -Wl-yt0x1B -Wl-ya4 -autobank -Wl-m -o "$o/core_link.gbc" "$o"/*.o
  echo "  linked $o/core_link.gbc ($(wc -c <"$o/core_link.gbc" | tr -d ' ') bytes)"
  return $status
}

sm83_test() {
  t="$CORE/test/sm83"
  o="$OUT/sm83t"
  mkdir -p "$o"
  rm -f "$o"/*.o
  scen="$t/scen_util.c $t/scen_a.c $t/scen_c.c $t/scen_b.c"
  # The register allocator's budget changes the code SDCC emits; SDCC_OPT="" runs the scenarios at its default.
  opt="${SDCC_OPT--Wf--max-allocs-per-node50000}"
  # shellcheck disable=SC2086
  $CC -std=c99 -O1 -g -Wall -Wextra $SANITIZE -I"$CORE/include" -o "$OUT/scen_host" $SRC $scen "$t/plat_host.c"
  "$OUT/scen_host" >"$OUT/scen_host.txt"
  defs="-DCORE_BANKED=BANKED -DCORE_LOCAL=BANKED -DSCEN_FN=BANKED -DPLAT_FN=NONBANKED"
  for f in $SRC "$t/rom_tables.c" $scen; do
    b="$(basename "$f" .c)"
    [ "$b" = cru_tables ] && continue
    # shellcheck disable=SC2086
    "$LCC" -c $opt $defs -Wf-bo255 -I"$CORE/include" -o "$o/$b.o" "$f" 2>&1 | grep -v 'warning 110\|warning 283\|^$' || true
  done
  # shellcheck disable=SC2086
  "$LCC" -c $defs -I"$CORE/include" -o "$o/scen_rom.o" "$t/scen_rom.c"
  # MBC5 + 128 KB RAM + battery, CGB: the scenarios use RAM banks 0-11 and keep a backup in bank 12.
  "$LCC" -Wm-yC -Wl-yt0x1B -Wl-ya16 -autobank -Wl-m -o "$o/scen.gbc" "$o"/*.o
  "$PYTHON" -W ignore "$t/scen_check.py" "$o/scen.gbc" "$o/scen.map" "$OUT/scen_host.txt"
}

case "${1:-all}" in
  test) host_test ;;
  sm83) sm83 ;;
  sm83-test) sm83_test ;;
  all) host_test && sm83 && sm83_test ;;
  *)
    echo "usage: $0 [test|sm83|sm83-test]" >&2
    exit 2
    ;;
esac
