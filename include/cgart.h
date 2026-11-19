/*
    The CG's interface imagery, palette and font. They are NewTek's and
    are not part of this program: cg_art_load() reads them from the
    Toaster CG program and the CGInterface font of the user's own Video
    Toaster (src/ui/cgartfile.c).
*/

#ifndef CGART_H
#define CGART_H

#include <stddef.h>
#include <stdint.h>

/* pixels are pens 0..7 into cg_art_palette */
struct cg_art_image
{
    int16_t        x, y, w, h;
    const uint8_t *px;
};

struct cg_art_gadget
{
    uint16_t            id;
    int16_t             w, h;
    uint16_t            flags, activation, type;
    struct cg_art_image render, select;
};

struct cg_art_char
{
    uint16_t loc, width;
    int16_t  space, kern;
};

struct cg_art_font
{
    int                       height, baseline, lo, hi, stripw;
    const uint8_t            *strip;    /* stripw*height pens */
    const struct cg_art_char *chars;    /* lo..hi, then the default char */
};

extern const struct cg_art_gadget *cg_art_gadgets;
extern int                         cg_art_ngadgets;
extern uint32_t                    cg_art_palette[8];
extern struct cg_art_font          cg_art_font;

/*
    Sets the art up. `toaster` is the Video Toaster's drawer: the art is
    read from its Programs/CG and its CGInterface/14.4C font (in
    Programs/System_Support/Fonts or Fonts) and redrawn as outlines, which
    takes a while (note() is told how far it is). The result is kept in a
    file: `keep` lists the names it may have, ended by NULL, and it is
    written under the first of them that can be written. The next time the
    kept file is read instead, as long as it was made from those same two
    files, or they are not there.

    Returns 0 with the reason in err, CG_ART_KEPT when a kept file was
    read, CG_ART_MADE when it was made and written, CG_ART_UNKEPT when it
    was made but could not be written.
*/
enum { CG_ART_KEPT = 1, CG_ART_MADE, CG_ART_UNKEPT };

int  cg_art_load(const char *toaster, const char *const *keep, void (*note)(void *ud, int done, int of), void *ud,
                 char *err, size_t errlen);
void cg_art_free(void);

#endif
