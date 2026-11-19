/*
    Loader for the deep ILBMs the CG uses as brushes: 24 bitplanes
    (red, green, blue, least significant bit first), an optional mask
    plane, uncompressed or ByteRun1. With 32 planes the last eight are
    alpha, as in the pictures at the head of this CG's page files.

    Also the Toaster's own framestores (Toaster:Framestore, and the
    picture at the head of a page file): 16 or 18 planes that a PLTP
    chunk maps onto bits of the framestore's bytes. Bytes 6 and 7 hold
    NTSC composite samples, 4 to a subcarrier cycle, of two frames whose
    chroma is inverted against each other, so their sum is luma and their
    difference chroma; byte 11, on key pages, a 2-bit key. BMHD's y
    places a key in the 480 line frame; 752 of the 768 columns are
    picture.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgilbm.h"

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

/* unpack one row of n bytes; returns the new source position or NULL */
static const uint8_t *unpack_row(const uint8_t *s, const uint8_t *end, uint8_t *d, int n, int comp)
{
    if (!comp)
    {
        if (end - s < n)
            return NULL;
        memcpy(d, s, n);
        return s + n;
    }
    while (n > 0)
    {
        int c;

        if (s >= end)
            return NULL;
        c = (int8_t)*s++;
        if (c >= 0)
        {
            if (c + 1 > n || end - s < c + 1)
                return NULL;
            memcpy(d, s, c + 1);
            s += c + 1;
            d += c + 1;
            n -= c + 1;
        }
        else if (c != -128)
        {
            if (-c + 1 > n || s >= end)
                return NULL;
            memset(d, *s++, -c + 1);
            d += -c + 1;
            n -= -c + 1;
        }
    }
    return s;
}

#define FRAME_W 752
#define FRAME_H 480

static uint8_t clamp8(double v)
{
    return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255 + 0.5);
}

static struct cg_image *framestore(const uint8_t *body, uint32_t bodylen, int w, int h, int planes, int comp,
                                   const uint8_t *pltp, uint32_t pltplen, int top, const char **err)
{
    const uint8_t *p = body, *end = body + bodylen;
    int rowbytes = (w + 15) / 16 * 2, y, pl, x;
    uint8_t *row = malloc((size_t)rowbytes), *a = calloc((size_t)w, 3), *b = a ? a + w : NULL, *k = a ? a + 2 * w : NULL;
    struct cg_image *img = calloc(1, sizeof(*img));
    int keyed = 0;

    if (img)
        img->rgba = calloc((size_t)FRAME_W * FRAME_H, 4);
    if (!row || !a || !img || !img->rgba || (uint32_t)planes * 2 > pltplen)
    {
        free(row);
        free(a);
        cg_image_free(img);
        *err = "out of memory";
        return NULL;
    }
    img->w = FRAME_W;
    img->h = FRAME_H;
    for (pl = 0; pl < planes; pl++)
        if (pltp[2 * pl] == 11)
            keyed = 1;
    /* a full frame is opaque; a key only where its key says */
    for (x = 0; x < FRAME_W * FRAME_H; x++)
        img->rgba[x * 4 + 3] = keyed ? 0 : 255;
    for (y = 0; y < h; y++)
    {
        int fy = top + y, sign = ((fy + 1) >> 1 & 1) ? 1 : -1;

        memset(a, 0, (size_t)w * 3);
        for (pl = 0; pl < planes; pl++)
        {
            int byte = pltp[2 * pl], bit = pltp[2 * pl + 1];
            uint8_t *dst = byte == 6 ? a : byte == 7 ? b : byte == 11 ? k : NULL;

            if (!(p = unpack_row(p, end, row, rowbytes, comp)))
            {
                free(row);
                free(a);
                cg_image_free(img);
                *err = "truncated BODY";
                return NULL;
            }
            if (dst)
                for (x = 0; x < w; x++)
                    if (row[x >> 3] >> (7 - (x & 7)) & 1)
                        dst[x] |= (uint8_t)(1 << bit);
        }
        if (fy < 0 || fy >= FRAME_H)
            continue;
        for (x = 0; x < FRAME_W && x < w; x++)
        {
            uint8_t *d = img->rgba + ((size_t)fy * FRAME_W + x) * 4;
            double c[4] = { 0, 0, 0, 0 }, Y, I, Q;
            int j;

            /* chroma of the four samples around x, by their phase */
            for (j = x - 1; j <= x + 2; j++)
            {
                int jx = j < 0 ? 0 : j >= w ? w - 1 : j;

                c[j & 3] = (a[jx] - b[jx]) / 2.0;
            }
            Y = ((a[x] + b[x]) / 2.0 - 70.5) / 129.5;
            I = sign * (c[0] - c[2]) / 2.0 / 129.5;
            Q = sign * (c[1] - c[3]) / 2.0 / 129.5;
            d[0] = clamp8(Y + 0.956 * I + 0.621 * Q);
            d[1] = clamp8(Y - 0.272 * I - 0.647 * Q);
            d[2] = clamp8(Y - 1.106 * I + 1.703 * Q);
            if (keyed)
                d[3] = (uint8_t)(k[x] * 85);
        }
    }
    free(row);
    free(a);
    return img;
}

static struct cg_image *decode(const uint8_t *buf, size_t len, const char **err)
{
    const uint8_t *p = buf + 12, *end = buf + len, *body = NULL, *bmhd = NULL, *pltp = NULL;
    uint32_t bodylen = 0, pltplen = 0;
    struct cg_image *img;
    uint8_t *row;
    int w, h, planes, mask, comp, rowbytes, y, pl, x;

    if (len < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "ILBM", 4))
    {
        *err = "not an IFF ILBM";
        return NULL;
    }
    if (be32(buf + 4) + 8 < len)
        end = buf + be32(buf + 4) + 8;
    while (end - p >= 8)
    {
        uint32_t n = be32(p + 4);

        if (n > (size_t)(end - p - 8))
            n = (uint32_t)(end - p - 8);
        if (!memcmp(p, "BMHD", 4) && n >= 20)
            bmhd = p + 8;
        else if (!memcmp(p, "BODY", 4))
        {
            body = p + 8;
            bodylen = n;
        }
        else if (!memcmp(p, "PLTP", 4))
        {
            pltp = p + 8;
            pltplen = n;
        }
        p += 8 + n + (n & 1);
    }
    if (!bmhd || !body)
    {
        *err = "no BMHD or BODY";
        return NULL;
    }
    w = be16(bmhd);
    h = be16(bmhd + 2);
    planes = bmhd[8];
    mask = bmhd[9];
    comp = bmhd[10];
    if (pltp && planes <= 24 && comp <= 1 && w && h)
        return framestore(body, bodylen, w, h, planes, comp, pltp, pltplen, (int16_t)be16(bmhd + 6), err);
    if ((planes != 24 && planes != 32) || comp > 1 || !w || !h)
    {
        *err = "not a 24-bit ILBM";
        return NULL;
    }
    rowbytes = (w + 15) / 16 * 2;
    img = calloc(1, sizeof(*img));
    row = malloc((size_t)rowbytes);
    if (img)
        img->rgba = calloc((size_t)w * h, 4);
    if (!img || !row || !img->rgba)
    {
        free(row);
        cg_image_free(img);
        *err = "out of memory";
        return NULL;
    }
    img->w = w;
    img->h = h;
    p = body;
    end = body + bodylen;
    for (y = 0; y < h; y++)
    {
        uint8_t *d = img->rgba + (size_t)y * w * 4;

        for (pl = 0; pl < planes + (mask == 1); pl++)
        {
            if (!(p = unpack_row(p, end, row, rowbytes, comp)))
            {
                free(row);
                cg_image_free(img);
                *err = "truncated BODY";
                return NULL;
            }
            for (x = 0; x < w; x++)
            {
                int bit = row[x >> 3] >> (7 - (x & 7)) & 1;

                if (pl < planes)
                    d[x * 4 + pl / 8] |= (uint8_t)(bit << (pl & 7));
                else if (planes == 24)
                    d[x * 4 + 3] = bit ? 255 : 0;
            }
        }
        if (mask != 1 && planes == 24)
            for (x = 0; x < w; x++)
                d[x * 4 + 3] = 255;
    }
    free(row);
    return img;
}

struct cg_image *cg_ilbm_load(const char *path, const char **err)
{
    const char *dummy;
    struct cg_image *img;
    uint8_t *buf;
    long len;
    FILE *f;

    if (!err)
        err = &dummy;
    if (!(f = fopen(path, "rb")))
    {
        *err = "cannot open";
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = len > 0 ? malloc((size_t)len) : NULL;
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        *err = "cannot read";
        return NULL;
    }
    fclose(f);
    img = decode(buf, (size_t)len, err);
    free(buf);
    return img;
}

void cg_image_free(struct cg_image *img)
{
    if (img)
    {
        free(img->rgba);
        free(img);
    }
}

/************************************************************ writing */

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* one row by ByteRun1 into out (at most n + n / 128 + 1 bytes) */
static size_t pack_row(const uint8_t *in, int n, uint8_t *out)
{
    size_t o = 0;
    int i = 0;

    while (i < n)
    {
        int run = 1;

        while (i + run < n && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 3)
        {
            out[o++] = (uint8_t)(257 - run);
            out[o++] = in[i];
            i += run;
        }
        else
        {
            int lit = 0;

            /* literals up to the next run of three */
            while (i + lit < n && lit < 128 &&
                   !(i + lit + 2 < n && in[i + lit] == in[i + lit + 1] && in[i + lit] == in[i + lit + 2]))
                lit++;
            out[o++] = (uint8_t)(lit - 1);
            memcpy(out + o, in + i, (size_t)lit);
            o += (size_t)lit;
            i += lit;
        }
    }
    return o;
}

int cg_ilbm_write(FILE *f, const uint8_t *rgba, int w, int h, int planes, int top)
{
    int rowbytes = (w + 15) / 16 * 2, rows = planes == 24 ? 25 : planes, y, pl, x;
    uint8_t *row = calloc((size_t)rowbytes, 1), *packed = malloc((size_t)rowbytes * 2 + 2), hdr[12], bmhd[28];
    size_t body = 0;
    long form_at = ftell(f), body_at;
    int ok = 1;

    if (!row || !packed || form_at < 0 || (planes != 24 && planes != 32) || w <= 0 || h <= 0 || w > 65535 || h > 65535)
    {
        free(row);
        free(packed);
        return 0;
    }
    memcpy(hdr, "FORM", 4);
    memset(hdr + 4, 0, 4);
    memcpy(hdr + 8, "ILBM", 4);
    memcpy(bmhd, "BMHD", 4);
    put_be32(bmhd + 4, 20);
    memset(bmhd + 8, 0, 20);
    bmhd[8] = (uint8_t)(w >> 8);
    bmhd[9] = (uint8_t)w;
    bmhd[10] = (uint8_t)(h >> 8);
    bmhd[11] = (uint8_t)h;
    bmhd[14] = (uint8_t)(top >> 8);
    bmhd[15] = (uint8_t)top;
    bmhd[16] = (uint8_t)planes;
    bmhd[17] = planes == 24;    /* mskHasMask */
    bmhd[18] = 1;       /* ByteRun1 */
    bmhd[22] = 10;      /* aspect 10:11, the Toaster's pixels */
    bmhd[23] = 11;
    bmhd[24] = (uint8_t)(w >> 8);
    bmhd[25] = (uint8_t)w;
    bmhd[26] = (uint8_t)(h >> 8);
    bmhd[27] = (uint8_t)h;
    fwrite(hdr, 1, 12, f);
    fwrite(bmhd, 1, 28, f);
    fwrite("BODY\0\0\0\0", 1, 8, f);
    body_at = ftell(f);
    for (y = 0; y < h && ok; y++)
        for (pl = 0; pl < rows && ok; pl++)
        {
            const uint8_t *s = rgba + (size_t)y * w * 4;
            size_t n;

            memset(row, 0, (size_t)rowbytes);
            for (x = 0; x < w; x++)
            {
                int bit = pl < planes ? s[x * 4 + pl / 8] >> (pl & 7) & 1 : s[x * 4 + 3] >= 128;

                row[x >> 3] |= (uint8_t)(bit << (7 - (x & 7)));
            }
            n = pack_row(row, rowbytes, packed);
            ok = fwrite(packed, 1, n, f) == n;
            body += n;
        }
    if (ok && body & 1)
        ok = fputc(0, f) != EOF;
    if (ok)
    {
        uint8_t len[4];

        put_be32(len, (uint32_t)body);
        ok = !fseek(f, body_at - 4, SEEK_SET) && fwrite(len, 1, 4, f) == 4;
        put_be32(len, (uint32_t)(4 + 28 + 8 + body + (body & 1)));
        ok = ok && !fseek(f, form_at + 4, SEEK_SET) && fwrite(len, 1, 4, f) == 4 && !fseek(f, 0, SEEK_END);
    }
    free(row);
    free(packed);
    return ok;
}

int cg_ilbm_save(const char *path, const uint8_t *rgba, int w, int h, const char **err)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
    {
        *err = "cannot write the file";
        return 0;
    }
    ok = cg_ilbm_write(f, rgba, w, h, 24, 0);
    ok = !fclose(f) && ok;
    if (!ok)
        *err = "write error";
    return ok;
}
