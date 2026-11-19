/*
    The interface's imagery and text drawn from their SVG redraws
    (cgicons.h) at the interface's scale, in place of the original
    pixels.
*/

#ifndef VECART_H
#define VECART_H

#include <stdint.h>

#include "cgart.h"

/* whether an art image has a redraw */
int vec_has_image(const struct cg_art_image *img);

/* the image's redraw over w x h RGBA, its top left at Toaster (x,y),
   scaled by percent; 0 when it has none */
int vec_image(uint8_t *rgba, int w, int h, const struct cg_art_image *img, int x, int y, int percent);

/* character ch of the CGInterface font, its cell's top left at Toaster
   (x,y), in colour rgb */
void vec_char(uint8_t *rgba, int w, int h, unsigned char ch, int x, int y, int percent, uint32_t rgb);

/* drop the rasterised cache (on a change of scale) */
void vec_flush(void);

#endif
