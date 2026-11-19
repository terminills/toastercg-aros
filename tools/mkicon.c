/*
    mkicon: the CG's Workbench icon, a PNG icon (AROS icon.library reads
    its tool types, stack and default tool from an "icOn" chunk; see
    workbench/libs/icon/diskobjPNGio.c). The picture is a button in a
    2 pixel bevel with rounded corners, "CG" on it in the font given.

    usage: mkicon -f FONT [-d | -p TOOL] [-l LABEL] out.info [TOOLTYPE ...]

    -f FONT  the label's font, a file FreeType reads
    -d       a drawer's icon (its window's place and size)
    -p TOOL  a project's icon, opened with TOOL
    -l       the button's text (default CG)
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#pragma GCC diagnostic pop

#define ATTR_DRAWERX      0x80001003
#define ATTR_DRAWERY      0x80001004
#define ATTR_DRAWERWIDTH  0x80001005
#define ATTR_DRAWERHEIGHT 0x80001006
#define ATTR_STACKSIZE 0x80001009
#define ATTR_DEFAULTTOOL  0x8000100a
#define ATTR_TOOLTYPE  0x8000100b

#define SCALE  2        /* icon pixels per Toaster pixel */
#define W      30       /* the button, Toaster pixels */
#define H      22
#define STACK  262144

static char svg[65536];
static size_t len;

static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void put(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    len += (size_t)vsnprintf(svg + len, sizeof(svg) - len, fmt, ap);
    va_end(ap);
}

static void attr_u32(uint8_t *b, size_t *n, uint32_t v)
{
    b[(*n)++] = (uint8_t)(v >> 24);
    b[(*n)++] = (uint8_t)(v >> 16);
    b[(*n)++] = (uint8_t)(v >> 8);
    b[(*n)++] = (uint8_t)v;
}

int main(int argc, char **argv)
{
    const char *text = "CG", *tool = NULL, *font = NULL;
    double r = 2.5, t = 2, k = r - r / 1.41421356;
    uint32_t light = 0x664400, dark = 0x000000, face = 0x555555;
    FT_Library ft;
    FT_Face fc;
    int size = 15 * SCALE, tw, pen, base;
    NSVGimage *im;
    NSVGrasterizer *ras;
    uint8_t *rgba, chunk[4096];
    size_t n = 0;
    const char *s;
    int i, pw = W * SCALE, ph = H * SCALE, drawer = 0;
    FILE *out;
    png_structp png;
    png_infop info;

    while (argc > 2 && argv[1][0] == '-')
    {
        if (!strcmp(argv[1], "-d"))
            drawer = 1;
        else if (!strcmp(argv[1], "-p") && argc > 3)
            tool = (argv++)[2], argc--;
        else if (!strcmp(argv[1], "-l") && argc > 3)
            text = (argv++)[2], argc--;
        else if (!strcmp(argv[1], "-f") && argc > 3)
            font = (argv++)[2], argc--;
        else
            break;
        argv++;
        argc--;
    }
    if (argc < 2 || !font)
    {
        fprintf(stderr, "usage: mkicon -f FONT [-d | -p TOOL] [-l LABEL] out.info [TOOLTYPE ...]\n");
        return 20;
    }
    if (FT_Init_FreeType(&ft) || FT_New_Face(ft, font, 0, &fc))
    {
        fprintf(stderr, "mkicon: %s is not a font\n", font);
        return 20;
    }
    /* the bevel and face */
    put("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">", W, H, W, H);
    put("<path fill=\"#%06x\" d=\"M%.3f %.3f L0 %g A%g %g 0 0 1 %g 0 L%g 0 A%g %g 0 0 1 %.3f %.3f L%g %g L%g %g L%g %g Z\"/>",
        light & 0xffffff, k, H - k, r, r, r, r, W - r, r, r, W - k, k, W - t, t, t, t, t, H - t);
    put("<path fill=\"#%06x\" d=\"M%.3f %.3f A%g %g 0 0 1 %d %g L%d %g A%g %g 0 0 1 %g %d L%g %d A%g %g 0 0 1 %.3f %.3f L%g %g "
        "L%g %g L%g %g Z\"/>",
        dark & 0xffffff, W - k, k, r, r, W, r, W, H - r, r, r, W - r, H, r, H, r, r, k, H - k, t, H - t, W - t, H - t, W - t, t);
    put("<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"1\" fill=\"#%06x\"/>", t, t, W - 2 * t, H - 2 * t,
        face & 0xffffff);
    put("</svg>");

    if (!(im = nsvgParse(svg, "px", 96)) || !(ras = nsvgCreateRasterizer()) ||
        !(rgba = calloc((size_t)pw * ph, 4)))
        return 20;
    nsvgRasterize(ras, im, 0, 0, SCALE, rgba, pw, ph, pw * 4);

    /* the label, centred (smaller when it is wider than the button) */
    for (;; size--)
    {
        FT_Set_Pixel_Sizes(fc, 0, (FT_UInt)size);
        for (tw = 0, s = text; *s; s++)
            if (!FT_Load_Char(fc, (unsigned char)*s, FT_LOAD_DEFAULT))
                tw += (int)(fc->glyph->advance.x >> 6);
        if (tw <= pw - 6 * SCALE || size <= 6)
            break;
    }
    pen = (pw - tw) / 2;
    /* capitals stand in the middle */
    base = ph / 2 + (FT_Load_Char(fc, 'H', FT_LOAD_DEFAULT) ? size / 3 : (int)(fc->glyph->metrics.height >> 7));
    for (s = text; *s; s++)
    {
        FT_GlyphSlot g = fc->glyph;
        unsigned gx, gy;

        if (FT_Load_Char(fc, (unsigned char)*s, FT_LOAD_RENDER))
            continue;
        for (gy = 0; gy < g->bitmap.rows; gy++)
            for (gx = 0; gx < g->bitmap.width; gx++)
            {
                int px = pen + g->bitmap_left + (int)gx, py = base - g->bitmap_top + (int)gy, c;
                unsigned a = g->bitmap.buffer[gy * (unsigned)g->bitmap.pitch + gx];

                if (px < 0 || py < 0 || px >= pw || py >= ph)
                    continue;
                for (c = 0; c < 3; c++)
                    rgba[((size_t)py * pw + px) * 4 + c] =
                        (uint8_t)(rgba[((size_t)py * pw + px) * 4 + c] * (255 - a) / 255);
            }
        pen += (int)(g->advance.x >> 6);
    }
    FT_Done_Face(fc);
    FT_Done_FreeType(ft);

    /* the icOn chunk: a drawer's window, or a tool's stack and tool types */
    if (drawer)
    {
        attr_u32(chunk, &n, ATTR_DRAWERX);
        attr_u32(chunk, &n, 60);
        attr_u32(chunk, &n, ATTR_DRAWERY);
        attr_u32(chunk, &n, 60);
        attr_u32(chunk, &n, ATTR_DRAWERWIDTH);
        attr_u32(chunk, &n, 420);
        attr_u32(chunk, &n, ATTR_DRAWERHEIGHT);
        attr_u32(chunk, &n, 200);
    }
    else if (tool)
    {
        attr_u32(chunk, &n, ATTR_DEFAULTTOOL);
        memcpy(chunk + n, tool, strlen(tool) + 1);
        n += strlen(tool) + 1;
    }
    else
    {
        attr_u32(chunk, &n, ATTR_STACKSIZE);
        attr_u32(chunk, &n, STACK);
    }
    for (i = 2; i < argc && !drawer; i++)
    {
        size_t l = strlen(argv[i]) + 1;

        if (n + 4 + l > sizeof(chunk))
            return 20;
        attr_u32(chunk, &n, ATTR_TOOLTYPE);
        memcpy(chunk + n, argv[i], l);
        n += l;
    }

    if (!(out = fopen(argv[1], "wb")))
        return 20;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png)))
        return 20;
    png_init_io(png, out);
    png_set_IHDR(png, info, pw, ph, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_chunk(png, (png_const_bytep) "icOn", chunk, n);
    for (i = 0; i < ph; i++)
        png_write_row(png, rgba + (size_t)i * pw * 4);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(out);
    free(rgba);
    nsvgDeleteRasterizer(ras);
    nsvgDelete(im);
    return 0;
}
