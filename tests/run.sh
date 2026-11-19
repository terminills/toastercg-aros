#!/bin/sh
# Host regression tests: builds the tools with ASan/UBSan, runs the
# scripts, checks book round trips and that reloads render identically.
# usage: tests/run.sh   (from the repository, with testdata/ in place: Video
# Toaster content, see README.md; the panel is drawn from testdata/iso and
# kept in build/host/CG.art)
set -e
CF="-std=c99 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude $(pkg-config --cflags freetype2 libpng)"
LIBS="$(pkg-config --libs freetype2 libpng) -lm"
CORE="src/cgbook.c src/cgbook_save.c src/cgrender.c src/cgilbm.c src/cgtfont.c src/cgedit.c src/cgcmd.c src/cgpage.c src/cgplay.c"
UI="src/ui/cgui.c src/ui/canvas.c src/ui/cgartfile.c src/ui/cgtrace.c src/ui/trace.c src/ui/vecart.c"
mkdir -p build/host build/test
cc $CF -o build/host/cgscript tools/cgscript.c $CORE $LIBS
cc $CF -o build/host/bookcopy tools/bookcopy.c src/cgbook.c src/cgbook_save.c
cc $CF -Isrc/ui -o build/host/uitest tools/uitest.c $CORE $UI $LIBS
cc $CF -o build/host/scalecmp tools/scalecmp.c $LIBS
cc $CF -o build/host/playcheck tools/playcheck.c $CORE $LIBS
cc $CF -o build/host/ilbmcheck tools/ilbmcheck.c $CORE $LIBS
cc $CF -o build/host/pagerender tools/pagerender.c $CORE $LIBS
cc $CF -o build/host/pagecheck tools/pagecheck.c $CORE $LIBS
cc $CF -Isrc/ui -o build/host/fmtcheck tools/fmtcheck.c src/ui/trace.c -lm
export ASAN_OPTIONS=detect_leaks=1
export LSAN_OPTIONS=suppressions=$PWD/tests/lsan.supp
# the numbers of the panel's outlines are written as the host's C library writes them
build/host/fmtcheck > build/test/fmtcheck.log
for b in testdata/CG/Books/*.Book; do
    build/host/bookcopy "$b" build/test/copy.book
    cmp "$b" build/test/copy.book
done
build/host/cgscript testdata tests/newpage.cgs > build/test/newpage.log
build/host/cgscript testdata tests/reload.cgs build/test/newpage.book > build/test/reload.log
cmp build/test/newpage.png build/test/reload-book.png
cmp build/test/newpage.png build/test/reload-frame.png
build/host/cgscript testdata tests/crawl-type.cgs testdata/CG/Books/System4.1.Book > build/test/crawl-type.log
build/host/cgscript testdata tests/arexx.cgs testdata/CG/Books/System4.1.Book > build/test/arexx.log
build/host/cgscript testdata tests/crawltrim.cgs testdata/CG/Books/System4.1.Book > build/test/crawltrim.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/crawltrim.log
# the interface scripts: any sanitizer report fails the run
for t in ui-tour ui-scroll ui-crawl ui-edit; do
    build/host/uitest testdata tests/$t.cgu testdata/CG/Books/System4.1.Book > build/test/$t.log 2>&1
    ! grep -q 'ERROR\|runtime error' build/test/$t.log
done
# F7 renders the page to disk: the ILBM holds the page as rendered
build/host/uitest testdata tests/ui-renderdisk.cgu testdata/CG/Books/System4.1.Book > build/test/ui-renderdisk.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-renderdisk.log
build/host/ilbmcheck build/test/renderdisk.ilbm testdata/CG/Books/System4.1.Book 3 testdata > /dev/null
# a book's missing font is offered for replacement: a font root with all
# of testdata but Colorfonts/Symbola.80; page 3 must change once replaced
rm -rf build/test/missingroot && mkdir -p build/test/missingroot/PSFonts/Colorfonts
for f in testdata/*; do [ "$f" = testdata/PSFonts ] || ln -s "$PWD/$f" build/test/missingroot/; done
for f in testdata/PSFonts/*; do [ "$f" = testdata/PSFonts/Colorfonts ] || ln -s "$PWD/$f" build/test/missingroot/PSFonts/; done
for f in testdata/PSFonts/Colorfonts/*; do [ "$f" = testdata/PSFonts/Colorfonts/Symbola.80 ] || ln -s "$PWD/$f" build/test/missingroot/PSFonts/Colorfonts/; done
sed "s#@PWD@#$PWD#" tests/ui-missing.cgu > build/test/ui-missing.cgu
build/host/uitest build/test/missingroot build/test/ui-missing.cgu testdata/CG/Books/System4.1.Book > build/test/ui-missing.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-missing.log
grep -q 'Unable to load Toaster:PSFonts/Colorfonts/Symbola.80' build/test/ui-missing.log
grep -q 'replaced 1' build/test/ui-missing.log
! build/host/scalecmp build/test/missing-before.png build/test/missing-after.png 100 0 0 751 479 > /dev/null
# the crawl slider: its right end puts the cursor where End does
build/host/uitest testdata tests/ui-crawlslider.cgu testdata/CG/Books/System4.1.Book > build/test/ui-crawlslider.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-crawlslider.log
build/host/scalecmp build/test/crawlslider-end.png build/test/crawlslider-endkey.png 100 0 0 751 479 > /dev/null
! build/host/scalecmp build/test/crawlslider-end.png build/test/crawlslider-start.png 100 0 0 751 479 > /dev/null
# the Info panel and Font Clear
build/host/uitest testdata tests/ui-info.cgu testdata/CG/Books/System4.1.Book > build/test/ui-info.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-info.log
grep -q 'Balloon-ExtraBold Copyright 1990 Brendel.*PostScript font No kerning pairs available Height: 35 Disk size: 69764 bytes Current path: Toaster:PSFonts/SoftMaker/Balloon-ExtraBold' build/test/ui-info.log
grep -q 'We dare not remove this font.' build/test/ui-info.log
# Load Draw makes a PostScript graphic; it saves with its rotation and
# shear, copies byte for byte, and draws from the saved book
build/host/uitest testdata tests/ui-draw.cgu > build/test/ui-draw.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-draw.log
grep -q 'saved build/test/draw.book: 1' build/test/ui-draw.log
LC_ALL=C grep -q -a 'Toaster.CGrs' build/test/draw.book
build/host/bookcopy build/test/draw.book build/test/draw-copy.book
cmp build/test/draw.book build/test/draw-copy.book
build/host/pagerender build/test/draw.book 0 testdata build/test/draw-saved.png > /dev/null
build/host/pagerender build/test/draw-copy.book 0 testdata build/test/draw-copy.png > /dev/null
build/host/scalecmp build/test/draw-saved.png build/test/draw-copy.png 100 0 0 751 479 > /dev/null
! build/host/scalecmp build/test/draw-saved.png build/test/missing-before.png 100 150 100 450 250 > /dev/null
# dragging stops at the frame's edges
build/host/uitest testdata tests/ui-dragclamp.cgu testdata/CG/Books/System4.1.Book > build/test/ui-dragclamp.log 2>&1
! grep -q 'ERROR\|runtime error' build/test/ui-dragclamp.log
# (rows above the bar: the bar shows the font at the cursor, which each
# drag's start puts elsewhere)
build/host/scalecmp build/test/drag-left.png build/test/drag-left2.png 100 0 0 751 440 > /dev/null
build/host/scalecmp build/test/drag-right.png build/test/drag-right2.png 100 0 0 751 440 > /dev/null
! build/host/scalecmp build/test/drag-left.png build/test/drag-right.png 100 0 0 751 440 > /dev/null
# a scroll's fields are the page at their view offset, at any scale
for p in 100 275; do
    build/host/playcheck testdata testdata/CG/Books/System4.1.Book 18 $p > build/test/playcheck-$p.log
done
# a still page's Take is the whole page, its lines too
for page in 3 5; do
    build/host/playcheck testdata testdata/CG/Books/System4.1.Book $page 100 >> build/test/playcheck-100.log
done
# Save Page: a key, a frame, a crawl and a scroll as the Switcher gets them
build/host/cgscript testdata tests/savepage.cgs testdata/CG/Books/System4.1.Book > build/test/savepage.log
! grep -q FAIL build/test/savepage.log
for p in 3 5 2 16 18; do
    build/host/pagecheck build/test/page-$p testdata/CG/Books/System4.1.Book $p testdata >> build/test/savepage.log
done
# scaled interfaces: the bar's pens land Toaster pixel for Toaster pixel
# as at 100 percent, in every bar mode the tour clicks its way into
mkdir -p build/test/scale
for p in 100 150 275 400; do
    { echo "VECTOR 0"; echo "SCALE $p"; sed "s#build/test/ui-#build/test/scale/$p-#" tests/ui-tour.cgu; } > build/test/scale/tour.cgu
    build/host/uitest testdata build/test/scale/tour.cgu testdata/CG/Books/System4.1.Book > build/test/scale/$p.log 2>&1
    ! grep -q 'ERROR\|runtime error' build/test/scale/$p.log
    [ $p = 100 ] && continue
    for s in normal cmds pagetype; do
        build/host/scalecmp build/test/scale/100-$s.png build/test/scale/$p-$s.png $p 53 426 697 479 > /dev/null
    done
    for s in color color-bkg; do
        build/host/scalecmp build/test/scale/100-$s.png build/test/scale/$p-$s.png $p 53 370 697 479 > /dev/null
    done
done
# the SVG redraws: each Toaster pixel's span of the bar at p percent,
# averaged, against the original pixels at 100 (correct: about 2 levels a
# channel; every redraw a Toaster pixel out: about 7)
for p in 150 275 400; do
    { echo "VECTOR 1"; echo "SCALE $p"; sed "s#build/test/ui-#build/test/scale/v$p-#" tests/ui-tour.cgu; } > build/test/scale/vtour.cgu
    build/host/uitest testdata build/test/scale/vtour.cgu testdata/CG/Books/System4.1.Book > build/test/scale/v$p.log 2>&1
    ! grep -q 'ERROR\|runtime error' build/test/scale/v$p.log
    for s in normal cmds pagetype; do
        build/host/scalecmp build/test/scale/100-$s.png build/test/scale/v$p-$s.png $p 53 426 697 479 4 > /dev/null
    done
    for s in color color-bkg; do
        build/host/scalecmp build/test/scale/100-$s.png build/test/scale/v$p-$s.png $p 53 370 697 479 4 > /dev/null
    done
done
echo "all tests passed"
