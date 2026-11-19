#!/bin/sh
# Damaged-file robustness: tools/fuzzload with the sanitizers over each
# loader, from real files. Any sanitizer report or crash fails.
# usage: tests/fuzz.sh [rounds]   (from the repository, testdata/ in place)
set -e
N=${1:-2000}
CF="-std=c99 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude $(pkg-config --cflags freetype2 libpng)"
CORE="src/cgbook.c src/cgbook_save.c src/cgrender.c src/cgilbm.c src/cgtfont.c src/cgedit.c src/cgcmd.c src/cgpage.c src/cgplay.c"
mkdir -p build/host build/fuzz
cc $CF -o build/host/fuzzload tools/fuzzload.c $CORE $(pkg-config --libs freetype2 libpng) -lm
export ASAN_OPTIONS=detect_leaks=1
export LSAN_OPTIONS=suppressions=$PWD/tests/lsan.supp
run() {
    build/host/fuzzload "$@" > build/fuzz/last.log 2>&1 || { cat build/fuzz/last.log; echo "FAIL: fuzzload $*"; exit 1; }
    tail -1 build/fuzz/last.log
}
run ilbm testdata/Brushes/TopHat $N 1 testdata
run ilbm testdata/Brushes/Sword $N 2 testdata
run tfont testdata/PSFonts/Colorfonts/OlGold.80 $N 3 testdata
for f in testdata/PSFonts/OldFonts/*; do run tfont "$f" $((N / 4)) 4 testdata; break; done
run picture "testdata/CG/Frames/$(ls testdata/CG/Frames | grep -v '\.i$' | head -1)" $((N / 4)) 5 testdata
run page "testdata/CG/Frames/$(ls testdata/CG/Frames | grep -v '\.i$' | head -1)" $((N / 2)) 6 testdata testdata/CG/Books/System4.1.Book
run page testdata/CG/Crawls/EBS.Crawl $((N / 2)) 7 testdata testdata/CG/Books/System4.1.Book
run tfontdraw testdata/PSFonts/Colorfonts/Symbola.80 $((N / 4)) 10 testdata testdata/CG/Books/System4.1.Book Toaster:PSFonts/Colorfonts/Symbola.80
run tfontdraw testdata/PSFonts/OldFonts/SimpleFuture.36 $((N / 4)) 11 testdata testdata/CG/Books/System4.1.Book toaster:psfonts/OldFonts/SimpleFuture.36
run book testdata/CG/Books/System4.1.Book $((N / 2)) 8 testdata
run book testdata/CG/Books/CGTips.Book $((N / 2)) 9 testdata
echo "fuzz passed"
