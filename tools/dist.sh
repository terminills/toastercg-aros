#!/bin/sh
# The AROS drawers and archives: build/dist/ToasterCG-<version>-<arch>.lha
# holding ToasterCG/ (CG, cgrx, icons, README, the licences) and its drawer
# icon. Nothing of the Video Toaster is in them: the CG takes its panel,
# its cg-config and the ARexx scripts from the user's own.
# LHA is written by tools/mklha.py (stored, level-2 headers): no LHA
# creator is needed on the host. ICONFONT names the font of the icons'
# labels (default: fontconfig's bold DejaVu Sans).
# usage: tools/dist.sh   (from the repository, after make aros-x86_64 aros-i386)
set -e
VER=$(sed -n 's/^#define CG_VERSTRING "\(.*\)"/\1/p' include/version.h | tr -d ' ')
FONT=${ICONFONT:-$(fc-match -f '%{file}' 'DejaVu Sans:bold')}
mkdir -p build/host build/dist
cc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -Isrc/ui $(pkg-config --cflags libpng freetype2) \
    -o build/host/mkicon tools/mkicon.c $(pkg-config --libs libpng freetype2) -lm
for arch in x86_64 i386; do
    top=build/dist/ToasterCG-$VER-$arch
    d=$top/ToasterCG
    rm -rf "$top" "$top.lha"
    mkdir -p "$d"
    cp build/aros-$arch/CG build/aros-$arch/cgrx "$d/"
    cp docs/README "$d/README"
    cp LICENSE "$d/LICENSE"
    cp src/ui/nanosvg/LICENSE.txt "$d/nanosvg-LICENSE.txt"
    build/host/mkicon -f "$FONT" "$d/CG.info" "(TOASTER=Toaster:)" "(SCALE=200)" "(CONFIG=<cg-config file>)"
    build/host/mkicon -f "$FONT" -p SYS:Utilities/MultiView -l "Read Me" "$d/README.info"
    build/host/mkicon -f "$FONT" -d -l "Toaster CG" "$top/ToasterCG.info"
    (cd "$top" && python3 -I ../../../tools/mklha.py "../ToasterCG-$VER-$arch.lha" ToasterCG ToasterCG.info)
    echo "build/dist/ToasterCG-$VER-$arch.lha"
done
