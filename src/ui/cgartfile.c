/*
    The CG's interface art, from the user's own Video Toaster: the gadget
    imagery and the interface palette the Toaster's CG program holds
    (Programs/CG, an AmigaDOS load file) and its CGInterface/14 colour
    font. Redrawn as outlines (cgtrace.c) it is kept in a file, so that
    it is read and redrawn once.
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgart.h"
#include "cgicons.h"

#define KEEP_MAGIC   0x54434741u    /* TCGA */
/* of the kept file's layout and of the redrawing: a file of another is made again */
#define KEEP_VERSION 1

#define HUNK_HEADER  0x3F3
#define HUNK_CODE    0x3E9
#define HUNK_DATA    0x3EA
#define HUNK_BSS     0x3EB
#define HUNK_RELOC32 0x3EC
#define HUNK_SYMBOL  0x3F0
#define HUNK_DEBUG   0x3F1
#define HUNK_END     0x3F2

/* the CG's hunk with its Gadgets and Images, and the palette's name in its code hunk */
#define CG_DATA_HUNK 4
#define CG_PALETTE   "_AAIntPalette"
/* the TextFont in a font file's hunk */
#define FONT_TF      58

const struct cg_art_gadget *cg_art_gadgets;
int                         cg_art_ngadgets;
uint32_t                    cg_art_palette[8];
struct cg_art_font          cg_art_font;
const struct cg_icon       *cg_icons;
int                         cg_nicons;
char                      **cg_vfont;

static struct cg_art_gadget *gadgets;
static struct cg_icon       *icons;
static int                   nvfont;

void cg_art_free(void)
{
    int i;

    for (i = 0; gadgets && i < cg_art_ngadgets; i++)
    {
        free((uint8_t *)gadgets[i].render.px);
        free((uint8_t *)gadgets[i].select.px);
    }
    free(gadgets);
    free((uint8_t *)cg_art_font.strip);
    free((struct cg_art_char *)cg_art_font.chars);
    cg_trace_free(icons, cg_nicons, cg_vfont, nvfont);
    gadgets = NULL;
    icons = NULL;
    cg_art_gadgets = NULL;
    cg_art_ngadgets = 0;
    cg_icons = NULL;
    cg_nicons = 0;
    cg_vfont = NULL;
    nvfont = 0;
    memset(&cg_art_font, 0, sizeof(cg_art_font));
}

/* big-endian values read in turn; past the end they are 0 and the reader is marked */
struct rd
{
    const uint8_t *d;
    size_t         len, o;
    int            bad;
};

static const uint8_t *take(struct rd *r, size_t n)
{
    const uint8_t *p = r->d + r->o;

    if (r->bad || n > r->len - r->o)
    {
        r->bad = 1;
        return NULL;
    }
    r->o += n;
    return p;
}

static uint32_t get32(struct rd *r)
{
    const uint8_t *p = take(r, 4);

    return p ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3] : 0;
}

static uint16_t get16(struct rd *r)
{
    const uint8_t *p = take(r, 2);

    return p ? (uint16_t)(p[0] << 8 | p[1]) : 0;
}

/* a copy of the next n bytes */
static uint8_t *getbytes(struct rd *r, size_t n)
{
    const uint8_t *p = take(r, n);
    uint8_t *c = p ? malloc(n ? n : 1) : NULL;

    if (c)
        memcpy(c, p, n);
    else
        r->bad = 1;
    return c;
}

static uint32_t sum(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;

    while (n--)
        h = (h ^ *d++) * 16777619u;
    return h;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *d = NULL;
    long n;

    if (!f)
        return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && n < (64L << 20) && !fseek(f, 0, SEEK_SET) &&
        (d = malloc((size_t)n)) && fread(d, 1, (size_t)n, f) != (size_t)n)
    {
        free(d);
        d = NULL;
    }
    fclose(f);
    if (d)
        *len = (size_t)n;
    return d;
}

/* the file `rel` of the drawer `dir`; where names are told apart by case
   (a CD mounted elsewhere) also with `rel` in lower case. path is the
   name tried first. */
static uint8_t *slurp_in(const char *dir, const char *rel, size_t *len, char *path, size_t pathlen)
{
    size_t n = strlen(dir), i;
    char low[600];
    uint8_t *d;

    snprintf(path, pathlen, "%s%s%s", dir, n && !strchr(":/", dir[n - 1]) ? "/" : "", rel);
    if ((d = slurp(path, len)) || strlen(path) >= sizeof(low))
        return d;
    strcpy(low, path);
    for (i = strlen(path) - strlen(rel); low[i]; i++)
        low[i] = (char)tolower((unsigned char)low[i]);
    return slurp(low, len);
}

/* ---- an AmigaDOS load file's hunks ---------------------------------- */

struct hunk
{
    const uint8_t *d;
    uint32_t       len;
    uint8_t       *rel;     /* per byte: 1 + the hunk a pointer there points into, 0 when it is none */
};

struct hunks
{
    struct hunk *h;
    int          n;
    int          sym_hunk;  /* where the symbol asked for is, -1 when it is not there */
    uint32_t     sym;
};

static void hunks_free(struct hunks *f)
{
    int i;

    for (i = 0; f->h && i < f->n; i++)
        free(f->h[i].rel);
    free(f->h);
    f->h = NULL;
    f->n = 0;
}

static int hunks_load(struct hunks *f, const uint8_t *d, size_t len, const char *symbol)
{
    struct rd r = { d, len, 0, 0 };
    uint32_t n, first, last, max, i;
    struct hunk *cur = NULL;

    memset(f, 0, sizeof(*f));
    f->sym_hunk = -1;
    if (get32(&r) != HUNK_HEADER)
        return 0;
    while ((n = get32(&r)))
        if (n > len / 4 || !take(&r, (size_t)n * 4))
            return 0;
    get32(&r);
    first = get32(&r);
    last = get32(&r);
    if (r.bad || last < first || (max = last - first + 1) > 250 || !take(&r, (size_t)max * 4) ||
        !(f->h = calloc(max, sizeof(*f->h))))
        return 0;
    while (!r.bad && r.o < len)
    {
        uint32_t t = get32(&r) & 0x3FFFFFFF;

        switch (t)
        {
        case HUNK_CODE:
        case HUNK_DATA:
        case HUNK_BSS:
            n = get32(&r);
            if ((uint32_t)f->n >= max || n > len / 4)
                goto bad;
            cur = &f->h[f->n++];
            if (t != HUNK_BSS)
            {
                cur->len = n * 4;
                if (!(cur->d = take(&r, cur->len)) || !(cur->rel = calloc(cur->len ? cur->len : 1, 1)))
                    goto bad;
            }
            break;
        case HUNK_RELOC32:
            while ((n = get32(&r)))
            {
                uint32_t target = get32(&r);

                if (n > len / 4)
                    goto bad;
                for (i = 0; i < n; i++)
                {
                    uint32_t off = get32(&r);

                    if (cur && off < cur->len && target < 255)
                        cur->rel[off] = (uint8_t)(target + 1);
                }
                if (r.bad)
                    goto bad;
            }
            break;
        case HUNK_SYMBOL:
            while ((n = get32(&r) & 0xFFFFFF))
            {
                const uint8_t *name = take(&r, (size_t)n * 4);
                uint32_t value = get32(&r);
                size_t sl = symbol ? strlen(symbol) : 0;

                if (r.bad)
                    goto bad;
                if (symbol && cur && f->sym_hunk < 0 && sl <= (size_t)n * 4 && !memcmp(name, symbol, sl) &&
                    (sl == (size_t)n * 4 || !name[sl]))
                {
                    f->sym_hunk = (int)(cur - f->h);
                    f->sym = value;
                }
            }
            break;
        case HUNK_DEBUG:
            n = get32(&r);
            if (n > len / 4)
                goto bad;
            take(&r, (size_t)n * 4);
            break;
        case HUNK_END:
            break;
        default:
            goto bad;
        }
    }
    if (!r.bad && f->n)
        return 1;
bad:
    hunks_free(f);
    return 0;
}

static int has(const struct hunk *h, uint32_t o, uint32_t n)
{
    return o <= h->len && n <= h->len - o;
}

static uint32_t h32(const struct hunk *h, uint32_t o)
{
    return (uint32_t)h->d[o] << 24 | (uint32_t)h->d[o + 1] << 16 | (uint32_t)h->d[o + 2] << 8 | h->d[o + 3];
}

static uint16_t h16(const struct hunk *h, uint32_t o)
{
    return (uint16_t)(h->d[o] << 8 | h->d[o + 1]);
}

/* the Image a pointer at `ptr` of the CG's data hunk points to, its
   planes as pens; left empty when there is none. 0 when memory runs out */
static int image_at(const struct hunks *f, uint32_t ptr, struct cg_art_image *im)
{
    const struct hunk *h = &f->h[CG_DATA_HUNK], *src;
    uint32_t o, base, bpr, size, i;
    int w, hgt, depth, pick, onoff, p, plane = 0, x, y;
    uint8_t *px;

    memset(im, 0, sizeof(*im));
    if (h->rel[ptr] != CG_DATA_HUNK + 1 || !has(h, o = h32(h, ptr), 16))
        return 1;
    w = (int16_t)h16(h, o + 4);
    hgt = (int16_t)h16(h, o + 6);
    depth = (int16_t)h16(h, o + 8);
    if (!h->rel[o + 10] || h->rel[o + 10] > f->n || w <= 0 || hgt <= 0)
        return 1;
    src = &f->h[h->rel[o + 10] - 1];
    base = h32(h, o + 10);
    pick = h->d[o + 14];
    onoff = h->d[o + 15];
    bpr = ((uint32_t)w + 15) / 16 * 2;
    size = (uint32_t)w * (uint32_t)hgt;
    if (!(px = calloc(size, 1)))
        return 0;
    for (p = 0; p < 8; p++)
    {
        if (!(pick >> p & 1))
        {
            if (onoff >> p & 1)
                for (i = 0; i < size; i++)
                    px[i] |= (uint8_t)(1 << p);
            continue;
        }
        if (plane >= depth)
            break;
        if (!has(src, base, (uint32_t)(plane + 1) * bpr * (uint32_t)hgt))
        {
            free(px);
            return 1;
        }
        for (y = 0; y < hgt; y++)
            for (x = 0; x < w; x++)
                if (src->d[base + (uint32_t)plane * bpr * (uint32_t)hgt + (uint32_t)y * bpr + (uint32_t)(x >> 3)] >>
                    (7 - (x & 7)) & 1)
                    px[y * w + x] |= (uint8_t)(1 << p);
        plane++;
    }
    im->x = (int16_t)h16(h, o);
    im->y = (int16_t)h16(h, o + 2);
    im->w = (int16_t)w;
    im->h = (int16_t)hgt;
    im->px = px;
    return 1;
}

/* the Intuition Gadgets in the CG's data hunk that carry its own IDs, by ID */
static int read_gadgets(const struct hunks *f)
{
    const struct hunk *h = &f->h[CG_DATA_HUNK];
    static uint32_t at[1000];
    uint32_t o;
    int n = 0, id, k = 0;

    memset(at, 0, sizeof(at));
    for (o = 0; o + 44 < h->len; o += 2)
    {
        int left = (int16_t)h16(h, o + 4), top = (int16_t)h16(h, o + 6);
        int w = (int16_t)h16(h, o + 8), hgt = (int16_t)h16(h, o + 10), type = h16(h, o + 16) & 7;

        if (h->rel[o] != CG_DATA_HUNK + 1 && h32(h, o))
            continue;
        if (!(0 < w && w <= 800 && 0 < hgt && hgt <= 480 && -100 < left && left < 800 && -100 < top && top < 500))
            continue;
        if ((type != 1 && type != 3 && type != 4 && type != 5) || (!h->rel[o + 18] && h32(h, o + 18)))
            continue;
        id = h16(h, o + 38);
        if (id >= 1000 || at[id])
            continue;
        at[id] = o + 1;
        n++;
    }
    if (!n || !(gadgets = calloc((size_t)n, sizeof(*gadgets))))
        return 0;
    cg_art_gadgets = gadgets;
    for (id = 0; id < 1000; id++)
        if (at[id])
        {
            struct cg_art_gadget *g = &gadgets[k++];

            o = at[id] - 1;
            cg_art_ngadgets = k;
            g->id = (uint16_t)id;
            g->w = (int16_t)h16(h, o + 8);
            g->h = (int16_t)h16(h, o + 10);
            g->flags = h16(h, o + 12);
            g->activation = h16(h, o + 14);
            g->type = h16(h, o + 16);
            if (!image_at(f, o + 18, &g->render) || !image_at(f, o + 22, &g->select))
                return 0;
        }
    return 1;
}

/* the CGInterface font: a ColorTextFont in the font file's one hunk */
static int read_font(const uint8_t *d, size_t len)
{
    struct cg_art_font *ft = &cg_art_font;
    struct cg_art_char *chars = NULL;
    uint8_t *strip = NULL;
    const struct hunk *h;
    struct hunks f;
    uint32_t tf = FONT_TF, modulo, loc, space, kern, xsize, planes[8];
    int depth, n, i, p, x, y, ok = 0;

    if (!hunks_load(&f, d, len, NULL))
        return 0;
    h = &f.h[0];
    if (!has(h, tf, 96))
        goto out;
    ft->height = h16(h, tf + 20);
    xsize = h16(h, tf + 24);
    ft->baseline = h16(h, tf + 26);
    ft->lo = h->d[tf + 32];
    ft->hi = h->d[tf + 33];
    modulo = h16(h, tf + 38);
    loc = h32(h, tf + 40);
    space = h32(h, tf + 44);
    kern = h32(h, tf + 48);
    depth = h->d[tf + 54];
    n = ft->hi - ft->lo + 2;
    if (ft->height < 1 || ft->height > 200 || !modulo || depth < 1 || depth > 8 || ft->hi < ft->lo ||
        !has(h, loc, (uint32_t)n * 4) || (space && !has(h, space, (uint32_t)n * 2)) ||
        (kern && !has(h, kern, (uint32_t)n * 2)))
        goto out;
    ft->stripw = (int)modulo * 8;
    if (!(strip = calloc((size_t)ft->stripw * ft->height, 1)) || !(chars = calloc((size_t)n, sizeof(*chars))))
        goto out;
    for (p = 0; p < depth; p++)
    {
        planes[p] = h32(h, tf + 64 + 4 * (uint32_t)p);
        if (!has(h, planes[p], modulo * (uint32_t)ft->height))
            goto out;
        for (y = 0; y < ft->height; y++)
            for (x = 0; x < ft->stripw; x++)
                if (h->d[planes[p] + (uint32_t)y * modulo + (uint32_t)(x >> 3)] >> (7 - (x & 7)) & 1)
                    strip[y * ft->stripw + x] |= (uint8_t)(1 << p);
    }
    for (i = 0; i < n; i++)
    {
        chars[i].loc = h16(h, loc + 4 * (uint32_t)i);
        chars[i].width = h16(h, loc + 4 * (uint32_t)i + 2);
        chars[i].space = space ? (int16_t)h16(h, space + 2 * (uint32_t)i) : (int16_t)xsize;
        chars[i].kern = kern ? (int16_t)h16(h, kern + 2 * (uint32_t)i) : 0;
        if (chars[i].loc + chars[i].width > ft->stripw)
            goto out;
    }
    ft->strip = strip;
    ft->chars = chars;
    strip = NULL;
    chars = NULL;
    ok = 1;
out:
    free(strip);
    free(chars);
    hunks_free(&f);
    return ok;
}

/* the pixels, from the CG program and the font */
static int read_art(const uint8_t *cg, size_t cglen, const uint8_t *font, size_t fontlen)
{
    struct hunks f;
    int i, ok = 0;

    if (!hunks_load(&f, cg, cglen, CG_PALETTE))
        return 0;
    if (f.n > CG_DATA_HUNK && f.h[CG_DATA_HUNK].d && f.sym_hunk == 0 && has(&f.h[0], f.sym, 16))
    {
        for (i = 0; i < 8; i++)
        {
            uint32_t c = h16(&f.h[0], f.sym + 2 * (uint32_t)i);

            cg_art_palette[i] = (c >> 8 & 15) * 17 << 16 | (c >> 4 & 15) * 17 << 8 | (c & 15) * 17;
        }
        ok = read_gadgets(&f) && read_font(font, fontlen);
    }
    hunks_free(&f);
    return ok;
}

/* ---- the kept file --------------------------------------------------- */

struct wr
{
    uint8_t *d;
    size_t   len, cap;
    int      bad;
};

static void put(struct wr *w, const void *p, size_t n)
{
    if (w->bad)
        return;
    if (n > w->cap - w->len)
    {
        size_t cap = w->cap * 2 + n + 4096;
        uint8_t *d = realloc(w->d, cap);

        if (!d)
        {
            w->bad = 1;
            return;
        }
        w->d = d;
        w->cap = cap;
    }
    memcpy(w->d + w->len, p, n);
    w->len += n;
}

static void put32(struct wr *w, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };

    put(w, b, 4);
}

static void put16(struct wr *w, unsigned v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };

    put(w, b, 2);
}

static void put_image(struct wr *w, const struct cg_art_image *im)
{
    put16(w, (uint16_t)im->x);
    put16(w, (uint16_t)im->y);
    put16(w, (uint16_t)(im->px ? im->w : 0));
    put16(w, (uint16_t)(im->px ? im->h : 0));
    if (im->px)
        put(w, im->px, (size_t)im->w * im->h);
}

static int get_image(struct rd *r, struct cg_art_image *im)
{
    im->x = (int16_t)get16(r);
    im->y = (int16_t)get16(r);
    im->w = (int16_t)get16(r);
    im->h = (int16_t)get16(r);
    if (r->bad || im->w < 0 || im->h < 0)
        return 0;
    if (im->w && im->h)
        im->px = getbytes(r, (size_t)im->w * im->h);
    return !r->bad;
}

static int keep_write(const char *path, const uint32_t key[4])
{
    const struct cg_art_font *ft = &cg_art_font;
    struct wr w = { NULL, 0, 0, 0 };
    int n = ft->hi - ft->lo + 2, i, ok = 0;
    FILE *f;

    put32(&w, KEEP_MAGIC);
    put32(&w, KEEP_VERSION);
    for (i = 0; i < 4; i++)
        put32(&w, key[i]);
    for (i = 0; i < 8; i++)
        put32(&w, cg_art_palette[i]);
    put32(&w, (uint32_t)cg_art_ngadgets);
    for (i = 0; i < cg_art_ngadgets; i++)
    {
        const struct cg_art_gadget *g = &cg_art_gadgets[i];

        put16(&w, g->id);
        put16(&w, (uint16_t)g->w);
        put16(&w, (uint16_t)g->h);
        put16(&w, g->flags);
        put16(&w, g->activation);
        put16(&w, g->type);
        put_image(&w, &g->render);
        put_image(&w, &g->select);
    }
    put32(&w, (uint32_t)ft->height);
    put32(&w, (uint32_t)ft->baseline);
    put32(&w, (uint32_t)ft->lo);
    put32(&w, (uint32_t)ft->hi);
    put32(&w, (uint32_t)ft->stripw);
    put(&w, ft->strip, (size_t)ft->stripw * ft->height);
    for (i = 0; i < n; i++)
    {
        put16(&w, ft->chars[i].loc);
        put16(&w, ft->chars[i].width);
        put16(&w, (uint16_t)ft->chars[i].space);
        put16(&w, (uint16_t)ft->chars[i].kern);
    }
    put32(&w, (uint32_t)cg_nicons);
    for (i = 0; i < cg_nicons; i++)
    {
        size_t len = strlen(cg_icons[i].svg);

        put32(&w, (uint32_t)cg_icons[i].gadget);
        put32(&w, (uint32_t)cg_icons[i].select);
        put32(&w, (uint32_t)cg_icons[i].w);
        put32(&w, (uint32_t)cg_icons[i].h);
        put32(&w, (uint32_t)len);
        put(&w, cg_icons[i].svg, len);
    }
    for (i = 0; i < n; i++)
    {
        size_t len = strlen(cg_vfont[i]);

        put32(&w, (uint32_t)len);
        put(&w, cg_vfont[i], len);
    }
    if (!w.bad)
        put32(&w, sum(w.d, w.len));
    if (!w.bad && (f = fopen(path, "wb")))
    {
        ok = fwrite(w.d, 1, w.len, f) == w.len;
        if (fclose(f))
            ok = 0;
        if (!ok)
            remove(path);
    }
    free(w.d);
    return ok;
}

static char *get_string(struct rd *r)
{
    uint32_t len = get32(r);
    const uint8_t *p = take(r, len);
    char *s = p ? malloc((size_t)len + 1) : NULL;

    if (!s)
    {
        r->bad = 1;
        return NULL;
    }
    memcpy(s, p, len);
    s[len] = 0;
    return s;
}

/* sets the art up from a kept file's bytes; with a key, only when it was made from that */
static int keep_read(const uint8_t *d, size_t len, const uint32_t *key)
{
    struct rd r = { d, len < 4 ? 0 : len - 4, 0, 0 };
    struct cg_art_font *ft = &cg_art_font;
    struct cg_art_char *chars;
    uint32_t n;
    int i;

    if (len < 28 || get32(&r) != KEEP_MAGIC || get32(&r) != KEEP_VERSION)
        return 0;
    for (i = 0; i < 4; i++)
    {
        n = get32(&r);
        if (key && n != key[i])
            return 0;
    }
    r.o = len - 4;
    r.len = len;
    if (get32(&r) != sum(d, len - 4))
        return 0;
    r.o = 24;
    r.len = len - 4;
    for (i = 0; i < 8; i++)
        cg_art_palette[i] = get32(&r);
    n = get32(&r);
    if (r.bad || !n || n > 1000 || !(gadgets = calloc(n, sizeof(*gadgets))))
        return 0;
    cg_art_gadgets = gadgets;
    for (i = 0; i < (int)n; i++)
    {
        struct cg_art_gadget *g = &gadgets[i];

        cg_art_ngadgets = i + 1;
        g->id = get16(&r);
        g->w = (int16_t)get16(&r);
        g->h = (int16_t)get16(&r);
        g->flags = get16(&r);
        g->activation = get16(&r);
        g->type = get16(&r);
        if (!get_image(&r, &g->render) || !get_image(&r, &g->select))
            goto bad;
    }
    ft->height = (int)get32(&r);
    ft->baseline = (int)get32(&r);
    ft->lo = (int)get32(&r);
    ft->hi = (int)get32(&r);
    ft->stripw = (int)get32(&r);
    if (r.bad || ft->height < 1 || ft->height > 200 || ft->stripw < 1 || ft->stripw > (1 << 20) || ft->lo < 0 ||
        ft->hi > 255 || ft->hi < ft->lo)
        goto bad;
    nvfont = ft->hi - ft->lo + 2;
    ft->strip = getbytes(&r, (size_t)ft->stripw * ft->height);
    ft->chars = chars = calloc((size_t)nvfont, sizeof(*chars));
    if (r.bad || !chars)
        goto bad;
    for (i = 0; i < nvfont; i++)
    {
        chars[i].loc = get16(&r);
        chars[i].width = get16(&r);
        chars[i].space = (int16_t)get16(&r);
        chars[i].kern = (int16_t)get16(&r);
        if (chars[i].loc + chars[i].width > ft->stripw)
            goto bad;
    }
    n = get32(&r);
    if (r.bad || n > 4000 || !(icons = calloc(n ? n : 1, sizeof(*icons))))
        goto bad;
    cg_icons = icons;
    for (i = 0; i < (int)n; i++)
    {
        cg_nicons = i + 1;
        icons[i].gadget = (int)get32(&r);
        icons[i].select = (int)get32(&r);
        icons[i].w = (int)get32(&r);
        icons[i].h = (int)get32(&r);
        icons[i].svg = get_string(&r);
        if (r.bad || icons[i].gadget < 0 || icons[i].gadget >= cg_art_ngadgets)
            goto bad;
    }
    if (!(cg_vfont = calloc((size_t)nvfont, sizeof(*cg_vfont))))
        goto bad;
    for (i = 0; i < nvfont; i++)
        if (!(cg_vfont[i] = get_string(&r)))
            goto bad;
    if (r.o == r.len)
        return 1;
bad:
    cg_art_free();
    return 0;
}

int cg_art_load(const char *toaster, const char *const *keep, void (*note)(void *ud, int done, int of), void *ud,
                char *err, size_t errlen)
{
    static const char *const fonts[] = { "Programs/System_Support/Fonts/CGInterface/14.4C",
                                         "Fonts/CGInterface/14.4C" };
    char cgpath[600], fontpath[600], first[600];
    size_t cglen = 0, fontlen = 0, len;
    uint8_t *cg, *font, *d;
    uint32_t key[4];
    int i, rc = 0;

    cg_art_free();
    cg = slurp_in(toaster, "Programs/CG", &cglen, cgpath, sizeof(cgpath));
    if (!(font = slurp_in(toaster, fonts[0], &fontlen, first, sizeof(first))))
        font = slurp_in(toaster, fonts[1], &fontlen, fontpath, sizeof(fontpath));
    if (cg && font)
    {
        key[0] = (uint32_t)cglen;
        key[1] = sum(cg, cglen);
        key[2] = (uint32_t)fontlen;
        key[3] = sum(font, fontlen);
    }
    for (i = 0; keep[i] && !rc; i++)
        if ((d = slurp(keep[i], &len)))
        {
            if (keep_read(d, len, cg && font ? key : NULL))
                rc = CG_ART_KEPT;
            free(d);
        }
    if (rc)
        goto out;
    if (!cg || !font)
    {
        snprintf(err, errlen,
                 "The CG's panel is drawn from the CG program and the interface font of your\n"
                 "Video Toaster, and they were not both found:\n"
                 "  %s\n"
                 "  %s\n"
                 "Assign Toaster: to the Video Toaster's drawer, or give TOASTER.",
                 cgpath, first);
        goto out;
    }
    if (!read_art(cg, cglen, font, fontlen))
    {
        snprintf(err, errlen,
                 "%s is not a CG program the panel can be drawn from\n"
                 "(the one it was made with is Toaster CG 4.1 of the Video Toaster 4.2),\n"
                 "or its font CGInterface/14.4C is not that font.",
                 cgpath);
        goto out;
    }
    nvfont = cg_art_font.hi - cg_art_font.lo + 2;
    if (!cg_trace(&icons, &cg_nicons, &cg_vfont, note, ud))
    {
        snprintf(err, errlen, "There is not enough memory to draw the CG's panel.");
        goto out;
    }
    cg_icons = icons;
    rc = CG_ART_UNKEPT;
    for (i = 0; keep[i] && rc == CG_ART_UNKEPT; i++)
        if (keep_write(keep[i], key))
            rc = CG_ART_MADE;
out:
    free(cg);
    free(font);
    if (!rc)
        cg_art_free();
    return rc;
}
