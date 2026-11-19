/*
    ToasterFont loader. The file is "ToasterFont:" and IFF-style chunks:

    HEAD  version (1 mono, 2 colour), baseline, first and last char,
          strip width in words, height, flags
    CHAR  per char: strip bit offset, width, advance, kern
    TEXT  the glyph strip, 1 bit deep
    SPEC  colour fonts: a 10 byte header, then 8 bitplanes of the strip
          holding 8 bit NTSC composite samples (4 per subcarrier cycle,
          on the I and Q axes, black at 70.5, white at 200)

    TEXT and SPEC are stored a field at a time, the even lines and then
    the odd ones, as the Toaster's framestore holds them. Their sizes
    follow from HEAD; some fonts carry truncated chunk lengths.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgtfont.h"

#define MAGIC     "ToasterFont:"
#define MAGIC_LEN 12

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

/* strip row y of a field-split bitmap */
static size_t field_row(int y, int h)
{
    return (y & 1) ? (size_t)(h + 1) / 2 + y / 2 : (size_t)y / 2;
}

static int bit(const uint8_t *row, int x)
{
    return row[x >> 3] >> (7 - (x & 7)) & 1;
}

static uint8_t clamp8(double v)
{
    return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255 + 0.5);
}

/* composite sample at strip x, line y: 8 planes a field at a time, each
   plane of a field bpr*h/2 bytes, so with an odd height the last even
   line runs into the next plane, as NewTek's writer left it */
static int sample(const uint8_t *spec, int bpr, int h, int x, int y)
{
    size_t stride = (size_t)bpr * h / 2;
    int pl, v = 0;

    for (pl = 0; pl < 8; pl++)
        v |= bit(spec + ((y & 1) * 8 + pl) * stride + (size_t)(y / 2) * bpr, x) << pl;
    return v;
}

/* the subcarrier phase inverts every line within a field */
static void decode(const uint8_t *spec, int bpr, int h, int stripw, int x, int y, uint8_t *rgb)
{
    int s[4], k, sign = ((y + 1) >> 1 & 1) ? -1 : 1;
    double Y = 0, I, Q;

    for (k = x - 1; k <= x + 2; k++)
    {
        int kx = k < 0 ? 0 : k >= stripw ? stripw - 1 : k;

        s[k & 3] = sample(spec, bpr, h, kx, y);
        Y += s[k & 3];
    }
    Y = (Y / 4 - 70.5) / 129.5;
    I = sign * (s[0] - s[2]) / 2.0 / 129.5;
    Q = sign * (s[1] - s[3]) / 2.0 / 129.5;
    rgb[0] = clamp8(Y + 0.956 * I + 0.621 * Q);
    rgb[1] = clamp8(Y - 0.272 * I - 0.647 * Q);
    rgb[2] = clamp8(Y - 1.106 * I + 1.703 * Q);
}

static struct cg_tfont *parse(const uint8_t *buf, size_t len, const char **err)
{
    const uint8_t *p = buf + MAGIC_LEN, *end = buf + len, *head = NULL, *chars = NULL;
    const uint8_t *text = NULL, *spec = NULL;
    struct cg_tfont *tf;
    uint32_t ncharbytes = 0;
    int version, lo, hi, bpr, h, stripw, c;
    size_t textlen, speclen;

    /* HEAD and CHAR lengths are sound; TEXT and SPEC sizes come from HEAD */
    while (end - p >= 8 && !text)
    {
        uint32_t n = be32(p + 4);

        if (!memcmp(p, "HEAD", 4) && n >= 12 && n <= (size_t)(end - p - 8))
            head = p + 8;
        else if (!memcmp(p, "CHAR", 4) && n <= (size_t)(end - p - 8))
        {
            chars = p + 8;
            ncharbytes = n;
        }
        else if (!memcmp(p, "TEXT", 4))
        {
            text = p + 8;
            break;
        }
        else
            break;
        p += 8 + n;
    }
    if (!head || !chars || !text)
    {
        *err = "missing HEAD, CHAR or TEXT";
        return NULL;
    }
    version = be16(head);
    lo = head[4];
    hi = head[5];
    bpr = be16(head + 6) * 2;
    h = be16(head + 8);
    stripw = bpr * 8;
    textlen = (size_t)bpr * h;
    if (hi < lo || !h || !bpr || ncharbytes < (uint32_t)(hi - lo + 1) * 8 ||
        (size_t)(end - text) < textlen)
    {
        *err = "bad HEAD";
        return NULL;
    }
    if (version == 2)
    {
        speclen = textlen * 8;
        p = text + textlen;
        if (end - p < 18 || memcmp(p, "SPEC", 4) || (size_t)(end - p - 18) < speclen)
        {
            *err = "bad SPEC";
            return NULL;
        }
        spec = p + 18;
    }
    if (!(tf = calloc(1, sizeof(*tf))))
    {
        *err = "out of memory";
        return NULL;
    }
    tf->height = h;
    tf->baseline = be16(head + 2);
    tf->colour = spec != NULL;
    for (c = lo; c <= hi; c++)
    {
        const uint8_t *e = chars + (c - lo) * 8;
        int loc = be16(e), w = be16(e + 2), x, y;
        struct cg_image *img;

        tf->glyphs[c].advance = be16(e + 4);
        tf->glyphs[c].kern = (int16_t)be16(e + 6);
        if (!w || loc + w > stripw)
            continue;
        if (!(img = calloc(1, sizeof(*img))) || !(img->rgba = calloc((size_t)w * h, 4)))
        {
            free(img);
            cg_tfont_free(tf);
            *err = "out of memory";
            return NULL;
        }
        img->w = w;
        img->h = h;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                uint8_t *d = img->rgba + ((size_t)y * w + x) * 4;

                if (!bit(text + field_row(y, h) * bpr, loc + x))
                    continue;
                d[3] = 255;
                if (spec)
                    decode(spec, bpr, h, stripw, loc + x, y, d);
                else
                    d[0] = d[1] = d[2] = 255;
            }
        tf->glyphs[c].img = img;
    }
    /* colour fonts have no space; 5/16 of the height centres System4.1's
       row of Symbola stars on the frame */
    if (lo > ' ')
        tf->glyphs[' '].advance = (h * 5 + 8) / 16;
    return tf;
}

struct cg_tfont *cg_tfont_load(const char *path, const char **err)
{
    const char *dummy;
    struct cg_tfont *tf;
    uint8_t magic[MAGIC_LEN], *buf;
    long len;
    FILE *f;

    if (!err)
        err = &dummy;
    *err = NULL;
    if (!(f = fopen(path, "rb")))
    {
        *err = "cannot open";
        return NULL;
    }
    if (fread(magic, 1, MAGIC_LEN, f) != MAGIC_LEN || memcmp(magic, MAGIC, MAGIC_LEN))
    {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        *err = "cannot read";
        return NULL;
    }
    fclose(f);
    tf = parse(buf, (size_t)len, err);
    free(buf);
    return tf;
}

void cg_tfont_free(struct cg_tfont *tf)
{
    int c;

    if (!tf)
        return;
    for (c = 0; c < 256; c++)
        cg_image_free(tf->glyphs[c].img);
    free(tf);
}
