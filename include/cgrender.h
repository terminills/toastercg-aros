/*
    Draws a ToasterCG page into a 32-bit RGBA buffer with FreeType.
*/

#ifndef CGRENDER_H
#define CGRENDER_H

#include "cgbook.h"

/* the Toaster's NTSC frame */
#define CG_FRAME_WIDTH  752
#define CG_FRAME_HEIGHT 480

/* Interface scales are percentages of the Toaster's size. A Toaster
   coordinate n is at output pixel cg_edge(n, percent), rounded as the
   native LightWave interface rounds, so two areas that meet in Toaster
   pixels meet on screen; cg_logical() is the Toaster pixel an output
   pixel lies in. */
static inline int cg_edge(int n, int percent)
{
    long v = (long)n * percent + 50;

    return (int)(v >= 0 ? v / 100 : -((-v + 99) / 100));
}

/* An outline's size in Toaster pixels, by type (none, thin, medium,
   thick as the CG's bar icons draw them), measured on the boxes in the
   Toaster's own pictures: across LeaderBoard's type 1 2, SpecialChars'
   type 2 3, Panic's type 3 7; down 2, 2 and 4, as the books' lines are
   fitted (type 1: all 12 boxes and 62 of 78 text lines give back their
   stored height with 2, none with 1). Down is an even number of lines,
   the same in both fields of the interlaced frame. A box or brush stands
   this far below its line's top (the line's height includes it), and its
   outline reaches out to the left of its line's x. */
static inline int cg_outline_x(int type)
{
    static const int w[4] = { 0, 2, 3, 7 };

    return w[type > 3 ? 3 : type];
}

static inline int cg_outline_y(int type)
{
    static const int h[4] = { 0, 2, 2, 4 };

    return h[type > 3 ? 3 : type];
}

static inline int cg_logical(int x, int percent)
{
    long v = (long)x * 100 + 49;

    return (int)(v >= 0 ? v / percent : -((-v + percent - 1) / percent));
}

struct cg_renderer;

/* fontroot: the drawer that "Toaster:" in font names refers to */
struct cg_renderer *cg_render_new(const char *fontroot);
void cg_render_free(struct cg_renderer *r);

/* rgba: width*height pixels, R,G,B,A bytes; scale 1 = Toaster size */
int cg_render_page(struct cg_renderer *r, const struct cg_book *book, int page,
                   uint8_t *rgba, int width, int height, double scale);

/* the same with the page shifted: (ox,oy) in output pixels is at the
   top left, for scroll pages taller and crawls wider than the frame */
int cg_render_page_view(struct cg_renderer *r, const struct cg_book *book, int page,
                        uint8_t *rgba, int width, int height, double scale, int ox, int oy);

/* the picture behind buffer pages (a 24-bit ILBM, or the first ILBM of a
   framestore or page file); NULL removes it */
int cg_render_set_picture(struct cg_renderer *r, const char *path);

/* key pages render see-through; this puts them over black */
void cg_render_flatten(uint8_t *rgba, int w, int h);

/* a font's height and baseline in Toaster pixels */
int cg_render_font_metrics(struct cg_renderer *r, const struct cg_data *d, int *height, int *baseline);

/* a font's kind as GET_FONT TYPE gives it: 4 PostScript, a ToasterFont 1
   (mono) or 2 (colour), 0 when it cannot be loaded. The ToasterFont
   header is not in NewTek's release: only "4 and up is PostScript" is
   known (SizeUp.Rexx), the rest follows the font's own HEAD version. */
int cg_render_font_type(struct cg_renderer *r, const struct cg_data *d);

/* what the Info panel shows of a font or brush: where it was found, its
   size on disk, a PostScript font's notice and the pairs in the .AFM
   beside it (-1 when there is none). 0 when it cannot be found. */
int cg_render_data_info(struct cg_renderer *r, const struct cg_data *d, char *path, size_t pathlen, long *disksize,
                        char *notice, size_t noticelen, int *kernpairs);

/* a brush's size in pixels */
int cg_render_brush_size(struct cg_renderer *r, const struct cg_data *d, int *w, int *h);

/* a PostScript graphic's size at the Toaster's scale */
int cg_render_drawing_size(struct cg_renderer *r, const struct cg_data *d, int *w, int *h);

/* xs[i]: where character i starts, xs[nchars]: where the line ends, in
   Toaster pixels from the line's xoffset */
int cg_render_line_layout(struct cg_renderer *r, const struct cg_book *book, const struct cg_line *l,
                          double *xs);

/* the same, allocated (free() it); any length, crawls included */
double *cg_render_line_xs(struct cg_renderer *r, const struct cg_book *book, const struct cg_line *l);

#endif
