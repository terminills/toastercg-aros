/*
    NewTek ToasterFont bitmap fonts (Toaster:PSFonts/Colorfonts, OldFonts).
*/

#ifndef CGTFONT_H
#define CGTFONT_H

#include "cgilbm.h"

struct cg_tglyph
{
    struct cg_image *img;   /* height rows; alpha is the glyph mask */
    int              advance;
    int              kern;
};

struct cg_tfont
{
    int              height;
    int              baseline;
    int              colour;    /* glyph pixels carry their own colour */
    struct cg_tglyph glyphs[256];
};

/* NULL with *err set; *err is NULL when the file is not a ToasterFont */
struct cg_tfont *cg_tfont_load(const char *path, const char **err);
void cg_tfont_free(struct cg_tfont *tf);

#endif
