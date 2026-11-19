/*
    The SVG redraws of the CG's imagery rasterised with nanosvg (zlib
    licence, src/ui/nanosvg) at the interface's scale. An icon or a
    character is parsed the first time it is drawn and rasterised once
    per scale; it lands where its original pixels would (cg_edge()).
*/

#include <stdlib.h>
#include <string.h>

#include "cgicons.h"
#include "cgrender.h"
#include "vecart.h"

/* nanosvg is vendored as it comes; its warnings are not ours */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#pragma GCC diagnostic pop

struct raster
{
    const void *key;
    int         percent;
    int         w, h;
    uint8_t    *rgba;       /* straight alpha */
};

#define MAX_RASTERS 512

static struct raster rasters[MAX_RASTERS];
static int nrasters;
static NSVGrasterizer *rast;

static const struct cg_icon *find_icon(const struct cg_art_image *img)
{
    int i;

    for (i = 0; i < cg_nicons; i++)
    {
        const struct cg_art_gadget *g = &cg_art_gadgets[cg_icons[i].gadget];

        if (img == (cg_icons[i].select ? &g->select : &g->render))
            return &cg_icons[i];
    }
    return NULL;
}

int vec_has_image(const struct cg_art_image *img)
{
    return find_icon(img) != NULL;
}

/* rasterise an SVG document at percent; NULL on failure */
static struct raster *raster_of(const void *key, const char *svg, int percent)
{
    struct raster *r;
    NSVGimage *im;
    char *copy;
    int i;

    for (i = 0; i < nrasters; i++)
        if (rasters[i].key == key && rasters[i].percent == percent)
            return &rasters[i];
    if (nrasters == MAX_RASTERS)
        vec_flush();
    if (!rast && !(rast = nsvgCreateRasterizer()))
        return NULL;
    if (!(copy = malloc(strlen(svg) + 1)))
        return NULL;
    strcpy(copy, svg);
    im = nsvgParse(copy, "px", 96);
    free(copy);
    if (!im)
        return NULL;
    r = &rasters[nrasters];
    r->key = key;
    r->percent = percent;
    r->w = (int)(im->width * percent / 100.0f + 0.999f);
    r->h = (int)(im->height * percent / 100.0f + 0.999f);
    r->rgba = calloc((size_t)(r->w ? r->w : 1) * (r->h ? r->h : 1), 4);
    if (!r->rgba)
    {
        nsvgDelete(im);
        return NULL;
    }
    nsvgRasterize(rast, im, 0, 0, percent / 100.0f, r->rgba, r->w, r->h, r->w * 4);
    nsvgDelete(im);
    nrasters++;
    return r;
}

/* r over the w x h RGBA at output (x0,y0), its colour replaced by rgb
   when tint, clipped to the w0 x h0 the original pixels covered */
static void composite(uint8_t *rgba, int w, int h, const struct raster *r, int x0, int y0, int w0, int h0, int tint,
                      uint32_t rgb)
{
    int x, y;

    for (y = 0; y < r->h && y < h0; y++)
        for (x = 0; x < r->w && x < w0; x++)
        {
            const uint8_t *s = r->rgba + ((size_t)y * r->w + x) * 4;
            unsigned a = s[3], cr = tint ? rgb >> 16 & 255 : s[0], cg = tint ? rgb >> 8 & 255 : s[1],
                     cb = tint ? rgb & 255 : s[2];
            uint8_t *d;

            if (!a || x0 + x < 0 || y0 + y < 0 || x0 + x >= w || y0 + y >= h)
                continue;
            d = rgba + ((size_t)(y0 + y) * w + x0 + x) * 4;
            d[0] = (uint8_t)((cr * a + d[0] * (255 - a) + 127) / 255);
            d[1] = (uint8_t)((cg * a + d[1] * (255 - a) + 127) / 255);
            d[2] = (uint8_t)((cb * a + d[2] * (255 - a) + 127) / 255);
            d[3] = 255;
        }
}

int vec_image(uint8_t *rgba, int w, int h, const struct cg_art_image *img, int x, int y, int percent)
{
    const struct cg_icon *ic = find_icon(img);
    struct raster *r;

    if (!ic || !(r = raster_of(ic, ic->svg, percent)))
        return 0;
    x += img->x;
    y += img->y;
    composite(rgba, w, h, r, cg_edge(x, percent), cg_edge(y, percent),
              cg_edge(x + img->w, percent) - cg_edge(x, percent), cg_edge(y + img->h, percent) - cg_edge(y, percent), 0, 0);
    return 1;
}

void vec_char(uint8_t *rgba, int w, int h, unsigned char ch, int x, int y, int percent, uint32_t rgb)
{
    const struct cg_art_font *f = &cg_art_font;
    int i = ch < f->lo || ch > f->hi ? f->hi - f->lo + 1 : ch - f->lo;
    const struct cg_art_char *g = &f->chars[i];
    static char doc[65536];
    struct raster *r;

    if (!g->width || !cg_vfont[i][0])
        return;
    /* an SVG document around the character's outline, made once */
    {
        int k;

        for (k = 0; k < nrasters; k++)
            if (rasters[k].key == &cg_vfont[i] && rasters[k].percent == percent)
                break;
        if (k == nrasters)
        {
            if (strlen(cg_vfont[i]) + 200 > sizeof(doc))
                return;
            sprintf(doc,
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">"
                    "<path fill=\"#000000\" fill-rule=\"evenodd\" d=\"%s\"/></svg>",
                    g->width, f->height, g->width, f->height, cg_vfont[i]);
        }
    }
    if (!(r = raster_of(&cg_vfont[i], doc, percent)))
        return;
    composite(rgba, w, h, r, cg_edge(x, percent), cg_edge(y, percent),
              cg_edge(x + g->width, percent) - cg_edge(x, percent), cg_edge(y + f->height, percent) - cg_edge(y, percent), 1,
              rgb);
}

void vec_flush(void)
{
    int i;

    for (i = 0; i < nrasters; i++)
        free(rasters[i].rgba);
    nrasters = 0;
}
