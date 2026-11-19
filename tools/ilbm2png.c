/*
    ilbm2png: a brush, framestore or page file's picture as a PNG, keys
    over black.

    usage: ilbm2png file out.png
*/

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgilbm.h"
int main(int argc, char **argv)
{
    const char *err; struct cg_image *img = argc > 1 ? cg_ilbm_load(argv[1], &err) : NULL; FILE *f; png_structp p; png_infop in; int y;
    if (argc < 3)
    {
        fprintf(stderr, "usage: ilbm2png file out.png\n");
        return 20;
    }
    if (!img) { printf("%s\n", err); return 1; }
    for (y = 0; y < img->w * img->h; y++) { unsigned a = img->rgba[y * 4 + 3]; img->rgba[y*4] = img->rgba[y*4] * a / 255; img->rgba[y*4+1] = img->rgba[y*4+1] * a / 255; img->rgba[y*4+2] = img->rgba[y*4+2] * a / 255; img->rgba[y*4+3] = 255; }
    f = fopen(argv[2], "wb"); p = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0); in = png_create_info_struct(p);
    png_init_io(p, f); png_set_IHDR(p, in, img->w, img->h, 8, PNG_COLOR_TYPE_RGBA, 0, 0, 0); png_write_info(p, in);
    for (y = 0; y < img->h; y++) png_write_row(p, img->rgba + (size_t)y * img->w * 4);
    png_write_end(p, 0); png_destroy_write_struct(&p, &in); fclose(f); cg_image_free(img); return 0;
}
