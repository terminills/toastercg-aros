/*
    24-bit IFF ILBM pictures, the format of Toaster:Brushes, and the
    32-bit ones (alpha in planes 24 to 31) of this CG's page files.
*/

#ifndef CGILBM_H
#define CGILBM_H

#include <stdint.h>
#include <stdio.h>

struct cg_image
{
    int      w, h;
    uint8_t *rgba;      /* w*h pixels; alpha from planes 24 to 31 or the mask plane, else 255 */
};

struct cg_image *cg_ilbm_load(const char *path, const char **err);

/* w x h RGBA as a 24-bit ILBM, ByteRun1, alpha as its mask plane (set
   where alpha is at least half); 0 with *err set on failure */
int cg_ilbm_save(const char *path, const uint8_t *rgba, int w, int h, const char **err);

/* the same as one FORM into an open file: 24 planes (alpha as the mask
   plane) or 32 (alpha, straight, as planes 24 to 31), with top as
   BMHD's y; 0 when it could not be written */
int cg_ilbm_write(FILE *f, const uint8_t *rgba, int w, int h, int planes, int top);
void cg_image_free(struct cg_image *img);

#endif
