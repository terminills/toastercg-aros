/*
    ToasterCG page renderer: background, then per character the shadow,
    outline and face, in the order of NewTek's renderer (newrender.a),
    with glyphs from FreeType instead of the Toaster's font cache, and
    boxes and brushes behind the text.
*/

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "cgrender.h"
#include "cgilbm.h"
#include "cgtfont.h"

#define MAX_FACES   64
#define MAX_BRUSHES 64

/* a font is a FreeType face or a NewTek ToasterFont */
struct face_cache
{
    char            *name;
    FT_Face          face;
    struct cg_tfont *tf;
};

struct brush_cache
{
    char            *name;
    struct cg_image *img;
};

struct cg_renderer
{
    FT_Library         lib;
    char              *fontroot;
    struct face_cache  faces[MAX_FACES];
    int                nfaces;
    struct brush_cache brushes[MAX_BRUSHES];
    int                nbrushes;
    struct cg_image   *picture;     /* CommonRGB: the background of buffer pages */
};

struct canvas
{
    uint8_t *rgba;
    int      w, h;
    int      ox, oy;    /* the page position at the canvas' top left */
};

/* light position 0 north .. 7 northwest; the shadow falls the other way */
static const int shadow_dx[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };
static const int shadow_dy[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };

struct cg_renderer *cg_render_new(const char *fontroot)
{
    struct cg_renderer *r = calloc(1, sizeof(*r));

    if (!r)
        return NULL;
    if (FT_Init_FreeType(&r->lib))
    {
        free(r);
        return NULL;
    }
    r->fontroot = strdup(fontroot);
    return r;
}

void cg_render_free(struct cg_renderer *r)
{
    int i;

    if (!r)
        return;
    for (i = 0; i < r->nfaces; i++)
    {
        if (r->faces[i].face)
            FT_Done_Face(r->faces[i].face);
        cg_tfont_free(r->faces[i].tf);
        free(r->faces[i].name);
    }
    for (i = 0; i < r->nbrushes; i++)
    {
        cg_image_free(r->brushes[i].img);
        free(r->brushes[i].name);
    }
    cg_image_free(r->picture);
    FT_Done_FreeType(r->lib);
    free(r->fontroot);
    free(r);
}

/* Amiga names are case insensitive: find each component of rel under root */
static int find_path(const char *root, const char *rel, char *out, size_t outlen)
{
    char cur[1024], comp[256];
    const char *p = rel;
    size_t rl = strlen(root);
    FILE *f;

    /* the name as it is first: on AROS that finds it in any case and
       through every drawer of a multi-assign (Toaster: on several drives),
       where listing a drawer sees only the first */
    snprintf(cur, sizeof(cur), "%s%s%s", root, rl && root[rl - 1] != ':' && root[rl - 1] != '/' ? "/" : "", rel);
    if ((f = fopen(cur, "rb")))
    {
        fclose(f);
        snprintf(out, outlen, "%s", cur);
        return 1;
    }
    snprintf(cur, sizeof(cur), "%s", root);
    while (*p)
    {
        size_t n = strcspn(p, "/");
        DIR *dir;
        struct dirent *e;
        int found = 0;

        if (n >= sizeof(comp))
            return 0;
        memcpy(comp, p, n);
        comp[n] = 0;
        p += n;
        if (*p == '/')
            p++;
        if (!(dir = opendir(cur)))
            return 0;
        while ((e = readdir(dir)))
            if (!strcasecmp(e->d_name, comp))
            {
                size_t l = strlen(cur), el = strlen(e->d_name);
                /* no separator after a volume or drawer name ending in : or / */
                int sep = l && cur[l - 1] != ':' && cur[l - 1] != '/';

                if (l + sep + el < sizeof(cur))
                {
                    if (sep)
                        cur[l++] = '/';
                    memcpy(cur + l, e->d_name, el + 1);
                    found = 1;
                }
                break;
            }
        closedir(dir);
        if (!found)
            return 0;
    }
    snprintf(out, outlen, "%s", cur);
    return 1;
}

/* "Toaster:" names resolve under fontroot */
/* A name in a book, under the Toaster drawer. A name without a volume
   (a drawing's font: "FontBankN-R/Reinhard") is also looked for in the
   font drawers, PSFonts and ToasterFonts. */
static int resolve(struct cg_renderer *r, const char *name, char *out, size_t outlen)
{
    /* names without a drawer are also looked for in OldFonts, as
       NewBook.c's CheckOldFonts does */
    static const char *const drawers[] = { "PSFonts/", "ToasterFonts/", "PSFonts/OldFonts/" };
    char alt[1024];
    size_t i;
    int volume = strchr(name, ':') != NULL;

    if (!strncasecmp(name, "Toaster:", 8))
    {
        name += 8;
        volume = 0;
    }
    /* a font or brush anywhere else (DH0:..., or a host's /...) is used
       where it is */
    if (volume || name[0] == '/')
    {
        FILE *f = fopen(name, "rb");

        if (f)
        {
            fclose(f);
            snprintf(out, outlen, "%s", name);
            return 1;
        }
    }
    if (find_path(r->fontroot, name, out, outlen))
        return 1;
    for (i = 0; !volume && i < sizeof(drawers) / sizeof(drawers[0]); i++)
    {
        snprintf(alt, sizeof(alt), "%s%s", drawers[i], name);
        if (find_path(r->fontroot, alt, out, outlen))
            return 1;
    }
    return 0;
}

/* a font that fails to load is cached empty so it is tried once */
static const struct face_cache *get_font(struct cg_renderer *r, const char *name)
{
    struct face_cache *f;
    const char *err;
    char path[1024];
    int i;

    for (i = 0; i < r->nfaces; i++)
        if (!strcmp(r->faces[i].name, name))
            return &r->faces[i];
    if (r->nfaces == MAX_FACES || !(name = strdup(name)))
        return NULL;
    f = &r->faces[r->nfaces++];
    f->name = (char *)name;
    f->face = NULL;
    f->tf = NULL;
    if (resolve(r, name, path, sizeof(path)) && !(f->tf = cg_tfont_load(path, &err)) && !err &&
        FT_New_Face(r->lib, path, 0, &f->face))
        f->face = NULL;
    return f;
}

/* a brush that fails to load is cached as NULL so it is tried once */
static struct cg_image *get_brush(struct cg_renderer *r, const char *name)
{
    char path[1024];
    int i;

    for (i = 0; i < r->nbrushes; i++)
        if (!strcmp(r->brushes[i].name, name))
            return r->brushes[i].img;
    if (r->nbrushes == MAX_BRUSHES)
        return NULL;
    r->brushes[r->nbrushes].name = strdup(name);
    r->brushes[r->nbrushes].img = resolve(r, name, path, sizeof(path)) ? cg_ilbm_load(path, NULL) : NULL;
    return r->brushes[r->nbrushes++].img;
}

static void blend(struct canvas *c, int x, int y, struct cg_rgba col, unsigned cover)
{
    uint8_t *d;
    unsigned a;

    x -= c->ox;
    y -= c->oy;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || !cover)
        return;
    a = cover * col.a / 255;
    d = c->rgba + ((size_t)y * c->w + x) * 4;
    if (d[3] == 255)
    {
        d[0] = (uint8_t)((col.r * a + d[0] * (255 - a)) / 255);
        d[1] = (uint8_t)((col.g * a + d[1] * (255 - a)) / 255);
        d[2] = (uint8_t)((col.b * a + d[2] * (255 - a)) / 255);
    }
    else
    {
        /* over a see-through background (a key page) */
        unsigned da = d[3] * (255 - a) / 255, oa = a + da;

        if (!oa)
            return;
        d[0] = (uint8_t)((col.r * a + d[0] * da) / oa);
        d[1] = (uint8_t)((col.g * a + d[1] * da) / oa);
        d[2] = (uint8_t)((col.b * a + d[2] * da) / oa);
        d[3] = (uint8_t)oa;
    }
}

/* a colour, or a top to bottom gradation over a line's height (RGB only) */
struct paint
{
    struct cg_rgba top, bottom;
    int            y0, h, grad;
};

static struct paint solid(struct cg_rgba col)
{
    struct paint p = { col, col, 0, 1, 0 };
    return p;
}

static struct paint gradation(struct cg_rgba top, struct cg_rgba bottom, int on, int y0, int h)
{
    struct paint p = { top, bottom, y0, h > 1 ? h : 1, on };
    return p;
}

static struct cg_rgba paint_at(const struct paint *p, int y)
{
    struct cg_rgba c = p->top;
    int t = y - p->y0;

    if (!p->grad)
        return c;
    t = t < 0 ? 0 : t > p->h - 1 ? p->h - 1 : t;
    c.r = (uint8_t)(p->top.r + (p->bottom.r - p->top.r) * t / (p->h - 1 ? p->h - 1 : 1));
    c.g = (uint8_t)(p->top.g + (p->bottom.g - p->top.g) * t / (p->h - 1 ? p->h - 1 : 1));
    c.b = (uint8_t)(p->top.b + (p->bottom.b - p->top.b) * t / (p->h - 1 ? p->h - 1 : 1));
    /* alpha as well: the CG's renderer steps all four channels
       (newrender.a AlphaIntoByteMap, GRAD_CHANNELS 4) */
    c.a = (uint8_t)(p->top.a + (p->bottom.a - p->top.a) * t / (p->h - 1 ? p->h - 1 : 1));
    return c;
}

static void draw_bitmap(struct canvas *c, const FT_Bitmap *bm, int left, int top, const struct paint *p)
{
    unsigned row, x;

    for (row = 0; row < bm->rows; row++)
    {
        struct cg_rgba col = paint_at(p, top + (int)row);

        for (x = 0; x < bm->width; x++)
            blend(c, left + (int)x, top + (int)row, col, bm->buffer[row * bm->pitch + x]);
    }
}

/* glyph image, optionally widened by a stroke of radius px */
/* the glyph loaded in the face's slot as a bitmap */
static FT_Glyph glyph_image(FT_Face face)
{
    FT_Glyph g;

    if (FT_Get_Glyph(face->glyph, &g))
        return NULL;
    if (FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, NULL, 1))
    {
        FT_Done_Glyph(g);
        return NULL;
    }
    return g;
}

static void draw_glyph(struct canvas *c, FT_Glyph g, int x, int y, struct paint p)
{
    FT_BitmapGlyph bg = (FT_BitmapGlyph)g;
    draw_bitmap(c, &bg->bitmap, x + bg->left, y - bg->top, &p);
}

static void draw_background(struct cg_renderer *r, struct canvas *c, const struct cg_page *p)
{
    int x, y;

    if (p->background == CG_BACKGROUND_BUFFER && r->picture)
    {
        const struct cg_image *img = r->picture;

        for (y = 0; y < c->h; y++)
            for (x = 0; x < c->w; x++)
            {
                const uint8_t *s = img->rgba + ((size_t)(y * img->h / c->h) * img->w + x * img->w / c->w) * 4;
                uint8_t *d = c->rgba + ((size_t)y * c->w + x) * 4;

                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            }
        return;
    }

    for (y = 0; y < c->h; y++)
    {
        struct cg_rgba col = p->top;

        if (p->background == CG_BACKGROUND_GRADATION && c->h > 1)
        {
            col.r = (uint8_t)(p->top.r + (p->bottom.r - p->top.r) * y / (c->h - 1));
            col.g = (uint8_t)(p->top.g + (p->bottom.g - p->top.g) * y / (c->h - 1));
            col.b = (uint8_t)(p->top.b + (p->bottom.b - p->top.b) * y / (c->h - 1));
            col.a = (uint8_t)(p->top.a + (p->bottom.a - p->top.a) * y / (c->h - 1));
        }
        for (x = 0; x < c->w; x++)
        {
            uint8_t *d = c->rgba + ((size_t)y * c->w + x) * 4;
            d[0] = col.r;
            d[1] = col.g;
            d[2] = col.b;
            d[3] = col.a;
        }
    }
}

static int image_mask(const struct cg_image *img, int w, int h, int x, int y)
{
    if (x < 0 || y < 0 || x >= w || y >= h)
        return 0;
    return img->rgba[((size_t)(y * img->h / h) * img->w + x * img->w / w) * 4 + 3] != 0;
}

/* inside the mask, or within the outline radius o of it */
/* The CG's outline masks, its _OutlineTable read from the binary: the
   shape is put down at every point marked, around the mask's centre
   (column width/2, row (rows-1)/2), and the outline is where any copy
   falls. Types 1 to 3 reach 2, 3 and 7 across and 2, 2 and 4 down
   (cg_outline_x()/cg_outline_y()). */
static const char *const outline_rows[4][9] = {
    { NULL },
    { "....#...", "...#.#..", "..#...#.", "...#.#..", "....#..." },
    { "..#####.", ".##...##", ".##...##", ".##...##", "..#####." },
    { "......#####.....", "...###.....###..", "..#...........#.", ".#.............#", ".#.............#",
      ".#.............#", "..#...........#.", "...###.....###..", "......#####....." },
};

struct opoint
{
    int dx, dy;
};

/* the mask's points at this scale, and (face) the shape's own place;
   returns how many, at most 34 */
static int outline_points(int type, double scale, int face, struct opoint *pt)
{
    const char *const *r = outline_rows[type > 3 ? 3 : type < 0 ? 0 : type];
    int n = 0, rows = 0, i, j;

    while (rows < 9 && r[rows])
        rows++;
    for (i = 0; i < rows; i++)
    {
        int cols = (int)strlen(r[i]);

        for (j = 0; j < cols; j++)
            if (r[i][j] == '#')
            {
                pt[n].dx = (int)lround((j - cols / 2) * scale);
                pt[n].dy = (int)lround((i - (rows - 1) / 2) * scale);
                n++;
            }
    }
    if (face)
    {
        pt[n].dx = pt[n].dy = 0;
        n++;
    }
    return n;
}

/* The image at w x h with its outline: (w + 2ox) x (h + 2o) with (0,0)
   at the image's (-ox,-o), set where the image or any copy of it the
   outline's mask puts down falls. NULL when out of memory. */
static uint8_t *cover_mask(const struct cg_image *img, int w, int h, int ox, int o, int type, double scale)
{
    struct opoint pt[40];
    int n = outline_points(type, scale, 1, pt), W = w + 2 * ox, H = h + 2 * o, x, y, i;
    uint8_t *m = calloc((size_t)W, (size_t)H);

    if (!m)
        return NULL;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            for (i = 0; i < n; i++)
                if (image_mask(img, w, h, x - ox - pt[i].dx, y - o - pt[i].dy))
                {
                    m[(size_t)y * W + x] = 1;
                    break;
                }
    return m;
}

/* a coverage map placed as an FT bitmap glyph: left and top from the pen */
struct coverage
{
    uint8_t *px;
    int      w, h, left, top;
};

/* A shape's outline as the CG makes it: the shape put down at every
   point of the outline's mask, the most coverage of the copies kept, and
   (face) the shape itself as well. Returns 0 when out of memory. */
static int outline_coverage(const struct coverage *in, int type, double scale, int face, struct coverage *o)
{
    struct opoint pt[40];
    int n = outline_points(type, scale, face, pt), mx = 0, my = 0, i, row, x;

    for (i = 0; i < n; i++)
    {
        if (abs(pt[i].dx) > mx)
            mx = abs(pt[i].dx);
        if (abs(pt[i].dy) > my)
            my = abs(pt[i].dy);
    }
    o->w = in->w + 2 * mx;
    o->h = in->h + 2 * my;
    o->left = in->left - mx;
    o->top = in->top + my;
    if (!(o->px = calloc((size_t)(o->w ? o->w : 1), (size_t)(o->h ? o->h : 1))))
        return 0;
    for (i = 0; i < n; i++)
        for (row = 0; row < in->h; row++)
        {
            const uint8_t *src = in->px + (size_t)row * in->w;
            uint8_t *dst = o->px + (size_t)(row + my + pt[i].dy) * o->w + mx + pt[i].dx;

            for (x = 0; x < in->w; x++)
                if (src[x] > dst[x])
                    dst[x] = src[x];
        }
    return 1;
}

static void draw_coverage(struct canvas *c, const struct coverage *o, int x, int y, struct paint p)
{
    FT_Bitmap b;

    memset(&b, 0, sizeof(b));
    b.rows = (unsigned)o->h;
    b.width = (unsigned)o->w;
    b.pitch = o->w;
    b.buffer = o->px;
    draw_bitmap(c, &b, x + o->left, y - o->top, &p);
}

/* the glyph's own coverage as a map (rows packed; FreeType's may be padded) */
static int glyph_coverage(FT_Glyph g, struct coverage *o)
{
    FT_BitmapGlyph bg = (FT_BitmapGlyph)g;
    const FT_Bitmap *bm = &bg->bitmap;
    unsigned row;

    o->w = (int)bm->width;
    o->h = (int)bm->rows;
    o->left = bg->left;
    o->top = bg->top;
    if (!(o->px = malloc((size_t)(o->w ? o->w : 1) * (o->h ? o->h : 1))))
        return 0;
    for (row = 0; row < bm->rows; row++)
        memcpy(o->px + (size_t)row * o->w, bm->buffer + (size_t)row * bm->pitch, bm->width);
    return 1;
}

/* A shadow as the CG makes one (newscroll.c MakeBMShadow ORs every step
   into one bitplane, laid down once): the shape at steps first to len
   along (dx,dy), the most of them kept, so a see-through shadow is even
   and not darker where its steps overlap. Returns 0 when out of memory. */
static int shadow_coverage(const struct coverage *shape, int dx, int dy, int first, int len, struct coverage *o)
{
    int x0 = dx * first < dx * len ? dx * first : dx * len, x1 = dx * first < dx * len ? dx * len : dx * first;
    int y0 = dy * first < dy * len ? dy * first : dy * len, y1 = dy * first < dy * len ? dy * len : dy * first;
    int st, row, x;

    o->w = shape->w + x1 - x0;
    o->h = shape->h + y1 - y0;
    o->left = shape->left + x0;
    o->top = shape->top - y0;
    if (!(o->px = calloc((size_t)(o->w ? o->w : 1), (size_t)(o->h ? o->h : 1))))
        return 0;
    for (st = first; st <= len; st++)
        for (row = 0; row < shape->h; row++)
        {
            const uint8_t *src = shape->px + (size_t)row * shape->w;
            uint8_t *dst = o->px + (size_t)(row + dy * st - y0) * o->w + dx * st - x0;

            for (x = 0; x < shape->w; x++)
                if (src[x] > dst[x])
                    dst[x] = src[x];
        }
    return 1;
}

/* a and b (both placed from the same pen) in one map, the most of each */
static int union_coverage(const struct coverage *a, const struct coverage *b, struct coverage *o)
{
    int l = a->left < b->left ? a->left : b->left, t = a->top > b->top ? a->top : b->top;
    int r = a->left + a->w > b->left + b->w ? a->left + a->w : b->left + b->w;
    int bo = a->h - a->top > b->h - b->top ? a->h - a->top : b->h - b->top;
    const struct coverage *src[2] = { a, b };
    int k, row, x;

    o->left = l;
    o->top = t;
    o->w = r - l;
    o->h = t + bo;
    if (!(o->px = calloc((size_t)(o->w ? o->w : 1), (size_t)(o->h ? o->h : 1))))
        return 0;
    for (k = 0; k < 2; k++)
        for (row = 0; row < src[k]->h; row++)
            for (x = 0; x < src[k]->w; x++)
            {
                uint8_t v = src[k]->px[(size_t)row * src[k]->w + x];
                uint8_t *d = o->px + (size_t)(row + t - src[k]->top) * o->w + x + src[k]->left - l;

                if (v > *d)
                    *d = v;
            }
    return 1;
}

/* a brush or bitmap glyph at its own size: shadow and outline follow its
   mask; the face is the image's own colour, or the face colour for a
   mono glyph, at the face alpha */
/* an image drawn w x h at (x0,y0), with its outline and shadow */
static void draw_image_sized(struct canvas *c, const struct cg_image *img, int x0, int y0, int w, int h,
                             const struct cg_attr *a, double scale, int own_colour, struct paint face,
                             struct paint outline)
{
    int ox = (int)(cg_outline_x(a->outline_type) * scale + 0.5), o = (int)(cg_outline_y(a->outline_type) * scale + 0.5);
    int x, y;
    uint8_t *cover = NULL;

    if (!w || !h)
        return;
    /* the shadow's shape and the outline's, once for every shadow step */
    if (((a->shadow_type && a->shadow_length) || o) &&
        !(cover = cover_mask(img, w, h, ox, o, a->outline_type, scale)))
        return;
#define COVERED(x, y) cover[(size_t)((y) + o) * (w + 2 * ox) + (x) + ox]
    if (a->shadow_type && a->shadow_length)
    {
        int dx = shadow_dx[a->shadow_direction & 7], dy = shadow_dy[a->shadow_direction & 7];
        int len = (int)(a->shadow_length * scale + 0.5);
        struct coverage shape = { cover, w + 2 * ox, h + 2 * o, -ox, o }, sh;
        size_t i;

        /* the shape's set points at full coverage, laid down once */
        for (i = 0; i < (size_t)shape.w * shape.h; i++)
            cover[i] = cover[i] ? 255 : 0;
        if (shadow_coverage(&shape, dx, dy, a->shadow_type == CG_SHADOW_CAST ? len : 1, len, &sh))
        {
            draw_coverage(c, &sh, x0, y0, solid(a->shadow));
            free(sh.px);
        }
    }
    if (o)
        for (y = -o; y < h + o; y++)
            for (x = -ox; x < w + ox; x++)
                if (!image_mask(img, w, h, x, y) && COVERED(x, y))
                    blend(c, x0 + x, y0 + y, paint_at(&outline, y0 + y), 255);
#undef COVERED
    free(cover);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            const uint8_t *p = img->rgba + ((size_t)(y * img->h / h) * img->w + x * img->w / w) * 4;
            struct cg_rgba col = { p[0], p[1], p[2], a->face.a };

            if (!own_colour)
                col = paint_at(&face, y0 + y);
            /* an image's alpha is its coverage (a glyph's edge) */
            if (p[3])
                blend(c, x0 + x, y0 + y, col, p[3]);
        }
}

static void draw_image(struct canvas *c, const struct cg_image *img, int x0, int y0,
                       const struct cg_attr *a, double scale, int own_colour,
                       struct paint face, struct paint outline)
{
    draw_image_sized(c, img, x0, y0, (int)(img->w * scale + 0.5), (int)(img->h * scale + 0.5), a, scale,
                     own_colour, face, outline);
}

/* a PostScript graphic's text, rotation and shear (see cgbook.h) */
static void drawdata_text(const struct cg_data *d, char *text, int *rot, int *shear)
{
    const uint8_t *t = d->drawdata + CG_DRAW_TEXT, *x;
    size_t n = 0;

    while (n < CG_DRAWDATA_SIZE - CG_DRAW_TEXT - 1 && t[n])
        n++;
    memcpy(text, t, n);
    text[n] = 0;
    *rot = *shear = 0;
    x = t + n + 1;
    if (x + 8 <= d->drawdata + CG_DRAWDATA_SIZE && !memcmp(x, "CGrs", 4))
    {
        *rot = (int16_t)(x[4] << 8 | x[5]);
        *shear = (int16_t)(x[6] << 8 | x[7]);
    }
}

/* A PostScript graphic (DRAW): its text set in its PostScript font at its
   height, sheared and rotated as the CG's makefont matrix did ([1 0
   tan(shear) 1] then the rotation), as a coverage image cropped to the
   ink. */
static int drawing_image(struct cg_renderer *r, const struct cg_data *d, double scale, struct cg_image *img)
{
    const struct face_cache *f;
    char text[CG_DRAWDATA_SIZE];
    int rot, shear, pass, x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    double a, sh;
    FT_Matrix m;

    img->rgba = NULL;
    if (!d->drawdata || !(f = get_font(r, d->filename)) || !f->face ||
        FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)(d->height * scale + 0.5)))
        return 0;
    drawdata_text(d, text, &rot, &shear);
    if (!text[0])
        return 0;
    a = rot * M_PI / 180;
    sh = shear >= 0 && shear < 90 ? tan(shear * M_PI / 180) : 0;
    m.xx = (FT_Fixed)(cos(a) * 65536);
    m.xy = (FT_Fixed)((cos(a) * sh - sin(a)) * 65536);
    m.yx = (FT_Fixed)(sin(a) * 65536);
    m.yy = (FT_Fixed)((sin(a) * sh + cos(a)) * 65536);
    /* pass 0 finds the ink's box, pass 1 draws into it */
    for (pass = 0; pass < 2; pass++)
    {
        FT_Vector pen = { 0, 0 };
        const char *s;

        if (pass == 1)
        {
            if (x1 <= x0 || y1 <= y0)
                return 0;
            img->w = x1 - x0;
            img->h = y1 - y0;
            if (!(img->rgba = calloc((size_t)img->w * img->h, 4)))
                return 0;
        }
        for (s = text; *s; s++)
        {
            FT_GlyphSlot g;
            unsigned row, col;

            FT_Set_Transform(f->face, &m, &pen);
            if (FT_Load_Char(f->face, (unsigned char)*s, FT_LOAD_RENDER))
                continue;
            g = f->face->glyph;
            if (pass == 0 && g->bitmap.width && g->bitmap.rows)
            {
                if (g->bitmap_left < x0)
                    x0 = g->bitmap_left;
                if (-g->bitmap_top < y0)
                    y0 = -g->bitmap_top;
                if (g->bitmap_left + (int)g->bitmap.width > x1)
                    x1 = g->bitmap_left + (int)g->bitmap.width;
                if (-g->bitmap_top + (int)g->bitmap.rows > y1)
                    y1 = -g->bitmap_top + (int)g->bitmap.rows;
            }
            else if (pass == 1)
                for (row = 0; row < g->bitmap.rows; row++)
                    for (col = 0; col < g->bitmap.width; col++)
                    {
                        int px = g->bitmap_left - x0 + (int)col, py = -g->bitmap_top - y0 + (int)row;
                        uint8_t *p, v = g->bitmap.buffer[(size_t)row * g->bitmap.pitch + col];

                        if (px < 0 || py < 0 || px >= img->w || py >= img->h)
                            continue;
                        p = img->rgba + ((size_t)py * img->w + px) * 4;
                        p[0] = p[1] = p[2] = 255;
                        if (v > p[3])
                            p[3] = v;
                    }
            pen.x += g->advance.x;
            pen.y += g->advance.y;
        }
    }
    FT_Set_Transform(f->face, NULL, NULL);
    return 1;
}

int cg_render_drawing_size(struct cg_renderer *r, const struct cg_data *d, int *w, int *h)
{
    struct cg_image img;

    if (!d || d->type != CG_DATA_DRAW || !drawing_image(r, d, 1.0, &img))
        return 0;
    *w = img.w;
    *h = img.h;
    free(img.rgba);
    return 1;
}

/* A PostScript graphic stretched to the line's face size, with the line's
   outline and shadow as a brush has them (in the PSFonts and System4.1
   books, "!" of FontBankN-R/Reinhard). */
static void draw_drawing(struct cg_renderer *r, struct canvas *c, const struct cg_line *l, const struct cg_data *d,
                         const struct cg_attr *a, double scale)
{
    struct cg_image img;
    int w = (int)(l->face_width * scale + 0.5), h = (int)(l->face_height * scale + 0.5);
    int x = (int)(l->xoffset * scale), y = (int)((l->yoffset + cg_outline_y(a->outline_type)) * scale);

    if (!w || !h || !drawing_image(r, d, scale, &img))
        return;
    draw_image_sized(c, &img, x, y, w, h, a, scale, 0,
                     gradation(a->face, a->face_grad, a->special_fill & CG_FILL_FACE_GRAD, y, h),
                     gradation(a->outline, a->outline_grad, a->special_fill & CG_FILL_OUTLINE_GRAD, y, h));
    free(img.rgba);
}

/* A PostScript glyph's advance as the Toaster spaced it, at a font of
   this height drawn at this scale. Fitted against the Toaster's own
   pictures of 33 pages (frametest): from 40 lines up its letters stand
   at the outline's own (unhinted) advances; below 40 every advance,
   letters and spaces alike, is a whole pixel, the unhinted one plus 0.3
   rounded (Olnova-DemiBold 28: letters about 0.4 wider than unhinted,
   spaces 8 from 8.12). Decided at the Toaster's size of the font, so
   text keeps its spacing at every interface scale. */
static double toaster_advance(FT_GlyphSlot g, int height, double scale)
{
    double a = g->linearHoriAdvance / 65536.0 / scale;

    return (height < 40 ? floor(a + 0.3 + 0.5) : a) * scale;
}

/* Where a ToasterFont's baseline is in its glyphs. A mono font's is the
   HEAD's (SimpleFuture.36: 30 of 36, under the descenders); a colour
   font's glyph stands on the line's baseline whole, whatever HEAD says
   (Symbola.80: 40). Both as the books' own lines of each are fitted:
   with this, cg_edit_fix_line() gives back their stored height and
   baseline. */
static int tfont_baseline(const struct cg_tfont *tf)
{
    return tf->colour ? tf->height : tf->baseline;
}

static void draw_line(struct cg_renderer *r, struct canvas *c, const struct cg_book *book,
                      const struct cg_line *l, double scale)
{
    const struct cg_attr *attr = NULL;
    double pen = l->xoffset * scale;
    int base = (int)((l->yoffset + l->baseline) * scale);
    int top = (int)(l->yoffset * scale), height = (int)(l->total_height * scale + 0.5);
    uint16_t i;

    if (cg_line_is_graphic(book, l) || top + height + 64 < c->oy || top - 64 > c->oy + c->h)
        return;
#define FACE_PAINT(a) gradation((a)->face, (a)->face_grad, (a)->special_fill & CG_FILL_FACE_GRAD, top, height)
#define OUTLINE_PAINT(a) gradation((a)->outline, (a)->outline_grad, (a)->special_fill & CG_FILL_OUTLINE_GRAD, top, height)
    for (i = 0; i < l->nchars; i++)
    {
        const struct cg_char *ch = &l->chars[i];
        const struct face_cache *f;
        const struct cg_data *d;
        FT_Face face;
        FT_Glyph g;
        int x;

        if (ch->attr)
            attr = ch->attr;
        if (!attr || !(d = cg_book_find_data(book, (uint16_t)attr->id)) || d->type != CG_DATA_FONT ||
            !(f = get_font(r, d->filename)))
            continue;
        pen += ch->kerning * scale;
        if (f->tf)
        {
            const struct cg_tglyph *tg = &f->tf->glyphs[ch->ascii & 0xff];

            if (tg->img)
                draw_image(c, tg->img, (int)(pen + tg->kern * scale + 0.5),
                           base - (int)(tfont_baseline(f->tf) * scale + 0.5), attr, scale, f->tf->colour,
                           FACE_PAINT(attr), OUTLINE_PAINT(attr));
            /* as the Amiga's Text() does: the kern, then the space */
            pen += (tg->kern + tg->advance) * scale;
            continue;
        }
        if (!(face = f->face) || FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(d->height * scale + 0.5)) ||
            FT_Load_Char(face, ch->ascii, FT_LOAD_NO_BITMAP))
            continue;
        x = (int)(pen + 0.5);

        if ((g = glyph_image(face)))
        {
            struct coverage fc = { NULL, 0, 0, 0, 0 }, ring = { NULL, 0, 0, 0, 0 }, sh = { NULL, 0, 0, 0, 0 };
            struct coverage shape = { NULL, 0, 0, 0, 0 }, both = { NULL, 0, 0, 0, 0 };
            int shadow = attr->shadow_type && attr->shadow_length;
            int dx = shadow_dx[attr->shadow_direction & 7], dy = shadow_dy[attr->shadow_direction & 7];
            int len = (int)(attr->shadow_length * scale + 0.5), first = attr->shadow_type == CG_SHADOW_CAST ? len : 1;

            if (glyph_coverage(g, &fc))
            {
                if (attr->shadow_priority && shadow)
                {
                    /* shadow first: cast from the face alone, and one outline
                       around face and shadow (newscroll.c CalcOSRect) */
                    shadow_coverage(&fc, dx, dy, first, len, &sh);
                    if (attr->outline_type && sh.px && union_coverage(&fc, &sh, &both))
                        outline_coverage(&both, attr->outline_type, scale, 0, &ring);
                }
                else
                {
                    /* outline first: around the face, and the shadow cast from
                       face and outline together; a drop is extruded out to
                       its length, a cast is one copy */
                    if (attr->outline_type)
                        outline_coverage(&fc, attr->outline_type, scale, 0, &ring);
                    if (shadow && (attr->outline_type ? outline_coverage(&fc, attr->outline_type, scale, 1, &shape)
                                                      : 0))
                        shadow_coverage(&shape, dx, dy, first, len, &sh);
                    else if (shadow)
                        shadow_coverage(&fc, dx, dy, first, len, &sh);
                }
                /* shadow first puts the outline at the back, under the
                   shadow; otherwise the shadow is under the outline */
                if (sh.px && !(attr->shadow_priority && shadow))
                    draw_coverage(c, &sh, x, base, solid(attr->shadow));
                if (ring.px)
                    draw_coverage(c, &ring, x, base, OUTLINE_PAINT(attr));
                if (sh.px && attr->shadow_priority && shadow)
                    draw_coverage(c, &sh, x, base, solid(attr->shadow));
            }
            draw_glyph(c, g, x, base, FACE_PAINT(attr));
            free(fc.px);
            free(ring.px);
            free(sh.px);
            free(shape.px);
            free(both.px);
            FT_Done_Glyph(g);
        }
        pen += toaster_advance(face->glyph, d->height, scale);
    }
}

int cg_render_font_metrics(struct cg_renderer *r, const struct cg_data *d, int *height, int *baseline)
{
    const struct face_cache *f;
    FT_Face face;

    if (!d || d->type != CG_DATA_FONT || !(f = get_font(r, d->filename)))
        return 0;
    if (f->tf)
    {
        *height = f->tf->height;
        *baseline = tfont_baseline(f->tf);
        return 1;
    }
    if (!(face = f->face) || face->bbox.yMax <= face->bbox.yMin)
        return 0;
    /* the PostScript library fits the font's bounding box to the height */
    *height = d->height;
    *baseline = (int)((double)d->height * face->bbox.yMax / (face->bbox.yMax - face->bbox.yMin) + 0.5);
    return 1;
}

int cg_render_font_type(struct cg_renderer *r, const struct cg_data *d)
{
    const struct face_cache *f;

    if (!d || d->type != CG_DATA_FONT || !(f = get_font(r, d->filename)))
        return 0;
    if (f->tf)
        return f->tf->colour ? 2 : 1;
    return f->face ? 4 : 0;
}

int cg_render_data_info(struct cg_renderer *r, const struct cg_data *d, char *path, size_t pathlen, long *disksize,
                        char *notice, size_t noticelen, int *kernpairs)
{
    const struct face_cache *f = NULL;
    FILE *fp;
    char afm[1100], line[256], *dot;

    notice[0] = 0;
    *kernpairs = -1;
    *disksize = 0;
    if (!d || !d->filename || !resolve(r, d->filename, path, pathlen))
        return 0;
    if ((fp = fopen(path, "rb")))
    {
        if (!fseek(fp, 0, SEEK_END))
            *disksize = ftell(fp);
        fclose(fp);
    }
    if (d->type != CG_DATA_FONT || !(f = get_font(r, d->filename)) || !f->face)
        return 1;
    /* the notice from the font's clear text, "/Notice (...) readonly def"
       (AROS's freetype2.library has no FT_Get_PS_Font_Info) */
    if ((fp = fopen(path, "rb")))
    {
        static char head[16384];
        size_t n = fread(head, 1, sizeof(head) - 1, fp), o = 0;
        char *s, *e;
        int depth = 1;

        head[n] = 0;
        for (s = head; s < head + n && (s = memchr(s, '/', (size_t)(head + n - s))); s++)
            if (!strncmp(s, "/Notice", 7))
                break;
        if (s && s < head + n && (s = memchr(s, '(', (size_t)(head + n - s))))
            for (e = s + 1; e < head + n && o + 1 < noticelen; e++)
            {
                if (*e == '\\' && e + 1 < head + n)
                    e++;
                else if (*e == '(')
                    depth++;
                else if (*e == ')' && !--depth)
                    break;
                notice[o++] = *e == '\r' || *e == '\n' ? ' ' : *e;
            }
        notice[o] = 0;
        fclose(fp);
    }
    /* the pairs NewTek's renderer counted come from the .AFM */
    snprintf(afm, sizeof(afm), "%s", path);
    if ((dot = strrchr(afm, '.')) && dot > strrchr(afm, '/'))
        *dot = 0;
    strncat(afm, ".afm", sizeof(afm) - strlen(afm) - 1);
    if (!(fp = fopen(afm, "r")))
    {
        afm[strlen(afm) - 4] = 0;
        strncat(afm, ".AFM", sizeof(afm) - strlen(afm) - 1);
        fp = fopen(afm, "r");
    }
    if (fp)
    {
        *kernpairs = 0;
        while (fgets(line, sizeof(line), fp))
            if (!strncmp(line, "KPX ", 4))
                (*kernpairs)++;
        fclose(fp);
    }
    return 1;
}

int cg_render_brush_size(struct cg_renderer *r, const struct cg_data *d, int *w, int *h)
{
    const struct cg_image *img;

    if (!d || d->type != CG_DATA_BRUSH || !(img = get_brush(r, d->filename)))
        return 0;
    *w = img->w;
    *h = img->h;
    return 1;
}

int cg_render_line_layout(struct cg_renderer *r, const struct cg_book *book, const struct cg_line *l,
                          double *xs)
{
    const struct cg_attr *attr = NULL;
    double pen = 0;
    uint16_t i;

    for (i = 0; i < l->nchars; i++)
    {
        const struct cg_char *ch = &l->chars[i];
        const struct face_cache *f = NULL;
        const struct cg_data *d = NULL;

        if (ch->attr)
            attr = ch->attr;
        pen += ch->kerning;
        xs[i] = pen;
        if (!attr || !(d = cg_book_find_data(book, (uint16_t)attr->id)) || d->type != CG_DATA_FONT ||
            !(f = get_font(r, d->filename)))
            continue;
        if (f->tf)
            pen += f->tf->glyphs[ch->ascii & 0xff].kern + f->tf->glyphs[ch->ascii & 0xff].advance;
        else if (f->face && !FT_Set_Pixel_Sizes(f->face, 0, d->height) &&
                 !FT_Load_Char(f->face, ch->ascii, FT_LOAD_NO_BITMAP))
            pen += toaster_advance(f->face->glyph, d->height, 1.0);
    }
    xs[l->nchars] = pen;
    return 1;
}

static void fill_rect(struct canvas *c, int x0, int y0, int w, int h, struct paint p)
{
    int x, y;

    for (y = y0; y < y0 + h; y++)
    {
        struct cg_rgba col = paint_at(&p, y);

        for (x = x0; x < x0 + w; x++)
            blend(c, x, y, col, 255);
    }
}

/* a box: shadow, outline border, then the face, like a glyph */
/* A box's outline as the CG makes it: the box put down at every point of
   the outline's mask, and (face) the box itself; row by row, a row
   reaches as far as the points whose copies cover it */
static void box_outline(struct canvas *c, int x, int y, int w, int h, int type, double scale, int face,
                        struct paint p)
{
    struct opoint pt[40];
    int n = outline_points(type, scale, face, pt), my = 0, i, row;

    for (i = 0; i < n; i++)
        if (abs(pt[i].dy) > my)
            my = abs(pt[i].dy);
    for (row = y - my; row < y + h + my; row++)
    {
        int lo = 1 << 30, hi = -(1 << 30);

        for (i = 0; i < n; i++)
            if (row - pt[i].dy >= y && row - pt[i].dy < y + h)
            {
                if (pt[i].dx < lo)
                    lo = pt[i].dx;
                if (pt[i].dx > hi)
                    hi = pt[i].dx;
            }
        if (lo <= hi)
            fill_rect(c, x + lo, row, w + hi - lo, 1, p);
    }
}

/* a box's shadow: its outlined shape at every step, row by row as far
   as any copy reaches, laid down once */
static void box_shadow(struct canvas *c, int x, int y, int w, int h, int type, double scale, int dx, int dy,
                       int first, int len, struct paint p)
{
    struct opoint pt[40];
    int n = outline_points(type, scale, 1, pt), my = 0, i, st, row;
    int y0 = dy * first < dy * len ? dy * first : dy * len, y1 = dy * first < dy * len ? dy * len : dy * first;

    for (i = 0; i < n; i++)
        if (abs(pt[i].dy) > my)
            my = abs(pt[i].dy);
    for (row = y - my + y0; row < y + h + my + y1; row++)
    {
        int lo = 1 << 30, hi = -(1 << 30);

        for (st = first; st <= len; st++)
            for (i = 0; i < n; i++)
                if (row - dy * st - pt[i].dy >= y && row - dy * st - pt[i].dy < y + h)
                {
                    if (pt[i].dx + dx * st < lo)
                        lo = pt[i].dx + dx * st;
                    if (pt[i].dx + dx * st > hi)
                        hi = pt[i].dx + dx * st;
                }
        if (lo <= hi)
            fill_rect(c, x + lo, row, w + hi - lo, 1, p);
    }
}

static void draw_box(struct canvas *c, const struct cg_line *l, double scale)
{
    const struct cg_attr *a = l->nchars ? l->chars[0].attr : NULL;
    int x = (int)(l->xoffset * scale), y = (int)((l->yoffset + cg_outline_y(a ? a->outline_type : 0)) * scale);
    int w = (int)(l->face_width * scale + 0.5), h = (int)(l->face_height * scale + 0.5);
    int o;

    if (!a || !w || !h)
        return;
    o = (int)(cg_outline_y(a->outline_type) * scale + 0.5);
    if (a->shadow_type && a->shadow_length)
    {
        int dx = shadow_dx[a->shadow_direction & 7], dy = shadow_dy[a->shadow_direction & 7];
        int len = (int)(a->shadow_length * scale + 0.5);

        box_shadow(c, x, y, w, h, a->outline_type, scale, dx, dy, a->shadow_type == CG_SHADOW_CAST ? len : 1, len,
                   solid(a->shadow));
    }
    if (o)
        box_outline(c, x, y, w, h, a->outline_type, scale, 0,
                    gradation(a->outline, a->outline_grad, a->special_fill & CG_FILL_OUTLINE_GRAD, y - o, h + 2 * o));
    fill_rect(c, x, y, w, h, gradation(a->face, a->face_grad, a->special_fill & CG_FILL_FACE_GRAD, y, h));
}

int cg_render_page(struct cg_renderer *r, const struct cg_book *book, int page,
                   uint8_t *rgba, int width, int height, double scale)
{
    return cg_render_page_view(r, book, page, rgba, width, height, scale, 0, 0);
}

/* Text lines go down last first: the Toaster puts the page's first line
   on top (TextTrix1: the face layer's "F" over the outline's, which
   follows it in the book; TextTrix6's pink "Shadow" over its shadow) */
static void draw_text_lines(struct cg_renderer *r, struct canvas *c, const struct cg_book *book,
                            const struct cg_line *lines, double scale)
{
    const struct cg_line *l, **v;
    size_t n = 0;

    for (l = lines; l; l = l->next)
        n++;
    if (!(v = malloc(sizeof(*v) * (n ? n : 1))))
    {
        for (l = lines; l; l = l->next)
            draw_line(r, c, book, l, scale);
        return;
    }
    n = 0;
    for (l = lines; l; l = l->next)
        v[n++] = l;
    while (n--)
        draw_line(r, c, book, v[n], scale);
    free(v);
}

int cg_render_page_view(struct cg_renderer *r, const struct cg_book *book, int page,
                        uint8_t *rgba, int width, int height, double scale, int ox, int oy)
{
    const struct cg_page *p;
    const struct cg_line *l;
    struct canvas c = { rgba, width, height, ox, oy };
    int pri;

    if (page < 0 || page >= CG_PAGES_PER_BOOK || !book->pages[page].used)
        return 0;
    p = &book->pages[page];
    draw_background(r, &c, p);
    /* boxes and brushes back to front, then the text over them */
    for (pri = 0; pri < 256; pri++)
        for (l = p->lines; l; l = l->next)
            if (cg_line_is_graphic(book, l) && l->render_pri == pri)
            {
                const struct cg_attr *a = l->nchars ? l->chars[0].attr : NULL;
                const struct cg_data *d = a ? cg_book_find_data(book, (uint16_t)a->id) : NULL;
                const struct cg_image *img;

                if (!d)
                    draw_box(&c, l, scale);
                else if (d->type == CG_DATA_DRAW)
                    draw_drawing(r, &c, l, d, a, scale);
                else if (d->type == CG_DATA_BRUSH && (img = get_brush(r, d->filename)))
                    draw_image(&c, img, (int)(l->xoffset * scale),
                               (int)((l->yoffset + cg_outline_y(a->outline_type)) * scale), a, scale, 1,
                               solid(a->face), solid(a->outline));
            }
    if (p->type == CG_PAGE_CRAWL)
    {
        struct cg_line crawl;

        if (cg_crawl_merge(p, &crawl))
        {
            draw_line(r, &c, book, &crawl, scale);
            cg_crawl_free(&crawl);
        }
        return 1;
    }
    draw_text_lines(r, &c, book, p->lines, scale);
    return 1;
}

int cg_render_set_picture(struct cg_renderer *r, const char *path)
{
    struct cg_image *img = path ? cg_ilbm_load(path, NULL) : NULL;

    if (path && !img)
        return 0;
    cg_image_free(r->picture);
    r->picture = img;
    return 1;
}

void cg_render_flatten(uint8_t *rgba, int w, int h)
{
    size_t i, n = (size_t)w * h;

    for (i = 0; i < n; i++, rgba += 4)
        if (rgba[3] != 255)
        {
            rgba[0] = (uint8_t)(rgba[0] * rgba[3] / 255);
            rgba[1] = (uint8_t)(rgba[1] * rgba[3] / 255);
            rgba[2] = (uint8_t)(rgba[2] * rgba[3] / 255);
            rgba[3] = 255;
        }
}

double *cg_render_line_xs(struct cg_renderer *r, const struct cg_book *book, const struct cg_line *l)
{
    double *xs = malloc(((size_t)l->nchars + 1) * sizeof(*xs));

    if (xs)
        cg_render_line_layout(r, book, l, xs);
    return xs;
}
