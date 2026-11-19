/*
    An 8 colour pen canvas for the CG's interface, drawn at the original
    size and scaled to the screen by a percentage. Imagery and text that
    have SVG redraws (vecart.c) are recorded as well and drawn from them
    at the screen's scale when the canvas is blitted.
*/

#ifndef CANVAS_H
#define CANVAS_H

#include <stdint.h>

#include "cgart.h"

/* the interface pens (_AAIntPalette) */
#define PEN_BG      0
#define PEN_BLACK   1
#define PEN_DGREY   2
#define PEN_BROWN   3
#define PEN_GREY    4
#define PEN_BLUE    5
#define PEN_WHITE   6
#define PEN_CREAM   7
#define PEN_CLEAR   255

#define BOX_STANDARD 0
#define BOX_REV      1

struct pc_vec
{
    const struct cg_art_image *img;     /* an image, or NULL for a character */
    int                        ch, x, y;
};

struct pcanvas
{
    uint8_t       *px;
    int            w, h;
    /* the redraws, in drawing order; where one covers, pen pixels drawn
       later are flagged in over[] and go back on top when blitted */
    struct pc_vec *vec;
    int            nvec, capvec;
    uint8_t       *covered, *over;
    int            vector;      /* use the redraws */
};

int pc_init(struct pcanvas *c, int w, int h);
void pc_free(struct pcanvas *c);
/* every pixel to pen, the redraws forgotten */
void pc_clear(struct pcanvas *c, int pen);
/* whether canvases draw from the redraws (default) or the original pixels */
void pc_set_vector(int on);
void pc_fill(struct pcanvas *c, int x0, int y0, int x1, int y1, int pen);
void pc_image(struct pcanvas *c, const struct cg_art_image *img, int x, int y);
void pc_box(struct pcanvas *c, int x0, int y0, int x1, int y1, int style);
/* text with the CGInterface font, baseline at y; returns the width */
int pc_text(struct pcanvas *c, const char *s, int x, int y, int maxw);
int pc_text_width(const char *s);

/* pens to RGBA at Toaster position (x,y), scaled by percent, in a w-wide
   buffer; each pen pixel fills the output pixels from its edge to the
   next (cg_edge()); PEN_CLEAR is skipped */
void pc_blit(const struct pcanvas *c, uint8_t *rgba, int w, int h, int x, int y, int percent);

#endif
