/*
    Putting a page out: a key or buffer page as it is, a scroll moving
    up through the frame, a crawl moving left along its line, at the
    page's speed and playback mode.
*/

#ifndef CGPLAY_H
#define CGPLAY_H

#include "cgedit.h"

struct cg_play;

/* percent: the output's size against the Toaster's frame (cg_edge()) */
struct cg_play *cg_play_new(struct cg_renderer *r, const struct cg_book *book, int page, int percent);
void cg_play_free(struct cg_play *p);

/* the next field (60 per second) into w*h RGBA, over black (opaque,
   as cg_render_flatten() leaves a page); 0 once it is over (a
   still page or a frozen scroll stays on its last field until the
   program takes it down) */
int cg_play_field(struct cg_play *p, uint8_t *rgba, int w, int h);

/* on by some fields without drawing them, for a program that has fallen
   behind the field rate; 0 once the page is over */
int cg_play_skip(struct cg_play *p, int fields);

/* half pixels per field for a speed: the Toaster's steps, a scroll half
   a line and then 1 to 8 lines a field, a crawl 2, 4, 8 or 16 pixels */
int cg_play_scroll_speed(int speed);
int cg_play_crawl_speed(int speed);

/* a scroll's or crawl's lines alone, the strip its fields are windows
   of: w*h RGBA with straight alpha, NULL for a still page or an empty
   one; top is where a crawl's line sits in the frame */
const uint8_t *cg_play_strip(const struct cg_play *p, int *w, int *h, int *top);

/* fields from the first to the one that ends the page played once
   (a frozen scroll: to where it stops); 0 for a still page */
long cg_play_fields(const struct cg_play *p);

#endif
