# ToasterCG for AROS

Native AROS port (x86_64 and i386) of the Video Toaster CG 4.1, NewTek's character generator: the interface as it was,
drawn clean at any screen size, with the books, fonts, brushes and ARexx scripts of the Video Toaster 4.

Sources: the CG source NewTek released as the Open Video Toaster, and the CG program of the Video Toaster 4.2 as the
reference for what the release leaves out.

Nothing of NewTek's is in this repository or in a release. The CG draws its panel from the user's own Video Toaster:
at its first start it reads the gadget pictures, the palette and the interface font from `Toaster:Programs/CG`, redraws
them as outlines and keeps the result in `CG.art` beside itself (`src/ui/cgartfile.c`, `src/ui/cgtrace.c`). Its
`cg-config` and the ARexx scripts are the Toaster's too. `docs/README` is the user's manual and goes into a release.

Building (on Linux, with an AROS cross compiler and SDK):
- Say where they are in `local.mk` (not in the repository) or on the command line:
  `SDK_x86_64 = <AROS build>/bin/pc-x86_64/AROS/Developer`, `CROSS_x86_64 = <crosstools>/x86_64-aros-`, and the same
  with `i386`. The SDK needs freetype2 and reqtools.
- `make` builds `build/aros-x86_64/CG` and `cgrx`, `make aros-i386` the same for i386, `make dist` the drawers and
  archives in `build/dist/` (icons are drawn by `tools/mkicon.c`, lettered in a font of the host).
- `make host` builds the host tools (FreeType and libpng): `uitest` drives the interface from a script and writes
  PNGs, `cgscript` the ARexx functions, `pagerender` a page.
- `make test` runs the regression suite under ASan/UBSan. It needs Video Toaster content in `testdata/` (not in the
  repository): `PSFonts`, `Brushes` and `CG` of the Toaster's drawer, and `iso/` with `Programs/CG`,
  `Programs/CG_Support`, `Fonts/CGInterface` and `Arexx/CG` as on the disc.

Layout:
- `src/` the CG: books (`cgbook*.c`), rendering (`cgrender.c`, `cgtfont.c`), editing and the ARexx functions
  (`cgedit.c`, `cgcmd.c`), pages for the Switcher (`cgpage.c`), Take (`cgplay.c`); `src/ui/` the interface;
  `src/aros/cg.c` what is AROS only: the window, requesters, the `CG_AREXX` port.
- `include/`, `tools/` (host tools, `tools/aros/cgrx.c` the shell's way to the port), `tests/`, `docs/`.

Pages go to the [Video Toaster Switcher for AROS](https://github.com/terminills/toaster-switcher) as croutons: Save
Page writes the file its "Add croutons..." takes.

Licence: the code of this repository is under the AROS Public License 1.1 (`LICENSE`); the Initial Developer is terminills,
copyright (C) 2026. The CG's logic was converted from NewTek's CG (the Open Video Toaster source); what is NewTek's in
it stays NewTek's, on the terms of that release. `src/ui/nanosvg/` is nanosvg by Mikko Mononen, under the zlib licence
(`src/ui/nanosvg/LICENSE.txt`).
