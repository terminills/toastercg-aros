/*
    pagerender: render one page of a ToasterCG book to a PNG.
*/

#include <stdio.h>
#include <stdlib.h>
#include <png.h>

#include "cgrender.h"

static int write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    png_structp png;
    png_infop info;
    int y;

    if (!f)
        return 0;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!info || setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        return 0;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (y = 0; y < h; y++)
        png_write_row(png, rgba + (size_t)y * w * 4);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    struct cg_book *book;
    struct cg_renderer *r;
    const char *err = NULL;
    double scale = 1.0;
    uint8_t *rgba;
    int w, h, page;

    if (argc < 5)
    {
        fprintf(stderr, "usage: pagerender file.Book page fontroot out.png [scale]\n");
        return 20;
    }
    page = atoi(argv[2]);
    if (argc > 5)
        scale = atof(argv[5]);
    if (!(book = cg_book_load(argv[1], &err)))
    {
        fprintf(stderr, "pagerender: %s: %s\n", argv[1], err);
        return 20;
    }
    w = (int)(CG_FRAME_WIDTH * scale);
    h = (int)(CG_FRAME_HEIGHT * scale);
    if (!(r = cg_render_new(argv[3])) || !(rgba = calloc((size_t)w * h, 4)))
        return 20;
    if (!cg_render_page(r, book, page, rgba, w, h, scale))
    {
        fprintf(stderr, "pagerender: no page %d\n", page);
        return 10;
    }
    cg_render_flatten(rgba, w, h);
    if (!write_png(argv[4], rgba, w, h))
        return 20;
    free(rgba);
    cg_render_free(r);
    cg_book_free(book);
    return 0;
}
