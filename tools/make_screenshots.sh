#!/bin/sh
# make_screenshots.sh — renders the README images (docs/images) headlessly from the program itself.
# Needs: ImageMagick (magick) and ffmpeg. Run from the repository root after `make`.
#   sh tools/make_screenshots.sh            # everything
#   sh tools/make_screenshots.sh biomes     # just the biome stills (also: hero creatures sheets)
set -e
NF=./nightfall
OUT=docs/images
TMP=${TMPDIR:-/tmp}/nightfall-shots.$$
mkdir -p "$OUT" "$TMP"
trap 'rm -rf "$TMP"' EXIT
WHAT=${1:-all}
want() { [ "$WHAT" = all ] || [ "$WHAT" = "$1" ]; }

# still: name biome seed warm-up [extra args...]
still() {
    name=$1; biome=$2; seed=$3; t=$4; shift 4
    $NF --snap "$TMP/$name.ppm" --seed "$seed" --time "$t" --width 200 --shot-scale 5 --biome "$biome" "$@" >/dev/null
    magick "$TMP/$name.ppm" "$OUT/$name.png"
}

# gif: name biome seed warm-up seconds fps cropW cropH follow pixel-scale [extra args...]
# (the camera follows the first creature called $follow; empty = fixed camera)
gif() {
    name=$1; biome=$2; seed=$3; t=$4; secs=$5; fps=$6; cw=$7; ch=$8; follow=$9; ss=${10}; shift 10
    d="$TMP/$name"; rm -rf "$d"; mkdir -p "$d"
    $NF --snap /dev/null --seed "$seed" --time "$t" --width 200 --biome "$biome" \
        --record "$d" --record-time "$secs" --record-fps "$fps" --crop "$cw" "$ch" \
        ${follow:+--follow "$follow"} --shot-scale "$ss" "$@" >/dev/null
    ffmpeg -loglevel error -y -framerate "$fps" -i "$d/f%04d.ppm" \
        -vf "split[a][b];[a]palettegen=max_colors=112:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
        -loop 0 "$OUT/$name.gif"
}

if want biomes; then
    still biome-oak    oak_wood    5 40
    still biome-taiga  taiga       2 40
    still biome-birch  birch_grove 2 40
    still biome-swamp  swamp       4 40 --spawn frog --spawn salamander
    still biome-pine   pine_rocks  2 40
    still biome-beech  beech_hall  3 40
fi

if want hero; then
    gif hero taiga 2 20 10 8 200 84 "" 4 --spawn rabbit --spawn squirrel
fi

if want creatures; then
    gif dhole    taiga       1 1  12 10 110 64 dhole    4 --spawn-rate dhole=0 --spawn dhole --spawn rabbit --spawn mouse
    gif owl      beech_hall  8 0  14 10 110 64 owl      4 --spawn-rate owl=0 --spawn owl
    gif squirrel oak_wood    5 0  14 10 110 64 squirrel 4 --spawn-rate squirrel=0 --spawn squirrel
    gif mole     pine_rocks  5 1  12 10 84  56 mole     5 --spawn-rate mole=0 --spawn mole --grass-density 0.25
    gif bat      swamp       4 0  12 10 80  50 bat      5 --spawn-rate bat=0 --spawn bat
    gif deer     beech_hall  1 8  12 10 140 84 deer     3 --spawn-rate deer=0 --spawn deer
fi

if want sheets; then
    $NF --sheet "$TMP/sheet.ppm" --filter creature --scale 3 --width 1100 >/dev/null
    magick "$TMP/sheet.ppm" -fuzz 2% -trim +repage -bordercolor '#06090a' -border 12 "$OUT/creatures-sheet.png"
    $NF --sheet "$TMP/g1.ppm" --filter dhole --scale 4 --width 700 >/dev/null
    $NF --sheet "$TMP/g2.ppm" --filter dhole --scale 4 --width 700 --depth >/dev/null
    magick "$TMP/g1.ppm" "$TMP/g2.ppm" -background '#0a0e10' -append "$OUT/gallery.png"
    NF_DEBUG=1 $NF --snap "$TMP/pg.ppm" --seed 3 --time 30 --width 200 --shot-scale 5 --biome oak_wood \
        --spawn squirrel --spawn owl --spawn rabbit >/dev/null
    magick "$TMP/pg.ppm" "$OUT/playground.png"
fi
echo "done: $OUT"
