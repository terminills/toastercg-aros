/*
    ilbmcheck: an ILBM written by F7 (Render page to disk) against the page
    rendered directly. Opaque pixels must keep their colour exactly and
    clear ones be masked out; edges between (antialiasing) have no one-bit
    mask to match, so are not counted.

    usage: ilbmcheck file.ilbm book page fontroot
*/

#include <stdio.h>
#include <stdlib.h>

#include "cgilbm.h"
#include "cgrender.h"

int main(int argc, char **argv)
{
    struct cg_image *img;
    struct cg_book *book;
    struct cg_renderer *r;
    const char *err = NULL;
    uint8_t *rgba;
    long i, bad = 0, opaque = 0, clear = 0;

    if (argc != 5)
    {
        fprintf(stderr, "usage: ilbmcheck file.ilbm book page fontroot\n");
        return 20;
    }
    if (!(img = cg_ilbm_load(argv[1], &err)) || !(book = cg_book_load(argv[2], &err)) || !(r = cg_render_new(argv[4])) ||
        !(rgba = calloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT, 4)))
    {
        fprintf(stderr, "ilbmcheck: %s\n", err ? err : "cannot start");
        return 20;
    }
    if (img->w != CG_FRAME_WIDTH || img->h != CG_FRAME_HEIGHT ||
        !cg_render_page(r, book, atoi(argv[3]), rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, 1.0))
    {
        fprintf(stderr, "ilbmcheck: %dx%d, or no page\n", img->w, img->h);
        return 10;
    }
    for (i = 0; i < (long)CG_FRAME_WIDTH * CG_FRAME_HEIGHT; i++)
    {
        const uint8_t *a = rgba + i * 4, *b = img->rgba + i * 4;

        if (a[3] == 255)
        {
            opaque++;
            bad += b[3] != 255 || a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
        }
        else if (a[3] == 0)
        {
            clear++;
            bad += b[3] != 0;
        }
    }
    printf("%s: %ld opaque, %ld clear, %ld differ\n", argv[1], opaque, clear, bad);
    cg_image_free(img);
    cg_book_free(book);
    cg_render_free(r);
    free(rgba);
    return bad || !opaque ? 10 : 0;
}
