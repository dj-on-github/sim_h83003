#!/bin/sh
# Runs a list of mutations against the cases in /tmp/mut_spec.json.
#
#   tool/subspec.py 'module_colours_show '        # pick the cases first
#   tool/mutate.sh [list] [first] [last]
#
# The list defaults to /tmp/muts.txt, one mutation a line, four fields
# separated by @:
#
#   app_module.c@void module_colours_show@count > 0x0F@count >= 0x0F
#
# The from and to fields go through printf %b, so \n stands for a newline
# when an anchor has to span lines.
#
# A pristine copy of the application sources is taken *at the start of this
# run* and every mutation is applied to the tree from that copy. Nothing
# outlives the run, so a snapshot cannot go stale behind an edit -- which is
# what made the old /tmp/good dangerous. The copy is thrown away and the
# tree rebuilt from it on the way out, including on an interrupt.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

LIST=${1:-/tmp/muts.txt}
FROM=${2:-1}
TO=${3:-9999}

MUT_SNAPSHOT=$(mktemp -d "${TMPDIR:-/tmp}/mut-snapshot.XXXXXX")
export MUT_SNAPSHOT
cp bernina_artista180/application/*.c bernina_artista180/application/*.h \
   "$MUT_SNAPSHOT/"

cleanup() {
  cp "$MUT_SNAPSHOT"/*.c "$MUT_SNAPSHOT"/*.h bernina_artista180/application/
  ( cd bernina_artista180/application && make -s ) >/dev/null 2>&1 || true
  rm -rf "$MUT_SNAPSHOT"
}
trap cleanup EXIT INT TERM

n=0
while IFS='@' read -r f sig a b; do
  n=$((n + 1))
  [ -z "$f" ] && continue
  [ "$n" -lt "$FROM" ] && continue
  [ "$n" -gt "$TO" ] && break
  printf '%3d  %-28s %s\n' "$n" "$(echo "$a" | cut -c1-28)" \
    "$(tool/mutate_one.sh "$f" "$sig" "$(printf '%b' "$a")" \
         "$(printf '%b' "$b")" 2>&1 | tr '\n' ' ')"
done < "$LIST"
