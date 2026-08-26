#!/bin/sh
# One mutation, end to end: apply it, rebuild, run the cases in
# /tmp/mut_spec.json, and put the source back.
#
#   MUT_SNAPSHOT=<dir> tool/mutate_one.sh <file.c> <signature> <from> <to>
#
# Prints the number of cases that failed and the pass/fail summary. A
# mutation that fails no cases is one the suite cannot see -- either a case
# is missing or the mutation is genuinely equivalent.
#
# The restore runs from a trap, so an interrupt or a failed build leaves the
# tree as it found it rather than leaving a mutated source behind.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
export PATH="$HOME/opt/h8300-elf/bin:$PATH"

[ -n "$MUT_SNAPSHOT" ] || { echo "MUT_SNAPSHOT is not set" >&2; exit 2; }
[ -f "$MUT_SNAPSHOT/$1" ] || { echo "no $1 in $MUT_SNAPSHOT" >&2; exit 2; }

build() {
  ( cd bernina_artista180/application &&
    rm -f "${1%.c}.o" app.elf app.bin app.sym && make -s ) >/dev/null 2>&1
  ./bernina_artista180/application/mergeapp \
      -a bernina_artista180/application/app.bin \
      -m bernina_artista180/Bernina180_20260816.bin \
      -o /tmp/app_merged.bin >/dev/null
}

restore() {
  cp "$MUT_SNAPSHOT/$1" "bernina_artista180/application/$1"
  build "$1"
}
trap 'restore "$1"' EXIT INT TERM

python3 tool/mutate_apply.py "$MUT_SNAPSHOT" "$1" "$2" "$3" "$4"
build "$1"

# || true: compare_routines exits non-zero when a case fails, which is
# the whole point of a mutation run and must not stop the script.
out=$(dart run tool/compare_routines.dart /tmp/mut_spec.json 2>&1 || true)
echo "$out" | grep -cE "FAIL" || true
echo "$out" | grep -E "passed,"
