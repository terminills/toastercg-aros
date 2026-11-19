/*
    frametest: render the page inside a page file and compare it with the
    picture the Video Toaster rendered of it, stored in the same file.

    usage: frametest fontroot out-prefix file...

    Prints the mean absolute difference per channel (keys: over black, as
    keyed) and writes out-prefix<name>.png: the Toaster's picture above,
    this renderer's below.

    The Toaster's frame shows the CG's page TOASTER_TOP lines down: CG y
    is frame row y + 44. Measured: 34 of the 36 pictures match the page
    best at +44 (Graphics.1: mean difference 15.7 unshifted, 2.2 at +44),
    and key pictures are stored from row 43.
*/

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgilbm.h"
#include "cgrender.h"

#define TOASTER_TOP 44

static void write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    png_structp p;
    png_infop in;
    int y;

    if (!f)
        return;
    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    in = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, in, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, in);
    for (y = 0; y < h; y++)
        png_write_row(p, rgba + (size_t)y * w * 4);
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &in);
    fclose(f);
}

int main(int argc, char **argv)
{
    struct cg_renderer *r;
    int i, n = 0;
    double total = 0;

    if (argc < 4 || !(r = cg_render_new(argv[1])))
    {
        fprintf(stderr, "usage: frametest fontroot out-prefix file...\n");
        return 20;
    }
    for (i = 3; i < argc; i++)
    {
        const char *err, *name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        struct cg_book *book = cg_book_load(argv[i], &err);
        struct cg_image *ref = cg_ilbm_load(argv[i], &err);
        uint8_t *both;
        int page, p, k;
        double sum = 0;
        char out[512];

        if (!book || !ref || ref->w != CG_FRAME_WIDTH || ref->h != CG_FRAME_HEIGHT)
        {
            printf("%-24s skipped\n", name);
            cg_book_free(book);
            cg_image_free(ref);
            continue;
        }
        for (page = 0; page < CG_PAGES_PER_BOOK && !book->pages[page].used; page++)
            ;
        /* a buffer page is drawn over the switcher's picture of the
           moment, which the file does not hold (BackDrop3: a chess scene) */
        if (page < CG_PAGES_PER_BOOK && book->pages[page].background == CG_BACKGROUND_BUFFER)
        {
            printf("%-24s page %3d  buffer background, not in the file: skipped\n", name, page);
            cg_book_free(book);
            cg_image_free(ref);
            continue;
        }
        both = calloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 2, 4);
        cg_render_page_view(r, book, page, both + (size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 4, CG_FRAME_WIDTH,
                            CG_FRAME_HEIGHT, 1.0, 0, -TOASTER_TOP);
        cg_render_flatten(both + (size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 4, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);
        for (p = 0; p < CG_FRAME_WIDTH * CG_FRAME_HEIGHT; p++)
        {
            uint8_t *d = both + (size_t)p * 4, *s = ref->rgba + (size_t)p * 4, *m = d + (size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 4;

            for (k = 0; k < 3; k++)
            {
                d[k] = (uint8_t)(s[k] * s[3] / 255);
                sum += abs(d[k] - m[k]);
            }
            d[3] = 255;
        }
        sum /= (double)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 3;
        printf("%-24s page %3d type %d  mean |diff| %5.1f\n", name, page, book->pages[page].type, sum);
        snprintf(out, sizeof(out), "%s%s.png", argv[2], name);
        write_png(out, both, CG_FRAME_WIDTH, CG_FRAME_HEIGHT * 2);
        total += sum;
        n++;
        free(both);
        cg_book_free(book);
        cg_image_free(ref);
    }
    if (n)
        printf("%d pages, mean |diff| %.1f\n", n, total / n);
    cg_render_free(r);
    return 0;
}
