/*
    The CG's interface imagery redrawn as SVG, from the pixels the
    Toaster's own CG program holds (cgartfile.c reads them).

    A gadget in the CG's bevel gets a drawn frame (2 px, rounded corners
    split on the diagonal, the select state's colours where it has them)
    and its interior traced colour by colour into smooth outlines on the
    original pixel grid: the image upscaled, lightly blurred, its half
    level traced and the contour smoothed along its length between
    corners (trace.c). The CG's dithered mid-tones become the colour they
    stand for, and the speed gauges, whose needles are dithered single
    pixels, are drawn. Any other image is traced whole. The CGInterface
    font's characters are traced the same way, into the CG's own
    letterforms as smooth outlines.
*/

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgart.h"
#include "cgicons.h"
#include "trace.h"

static const double sigma = 0.40, tol = 0.10;

/* an SVG document as it grows; a failed allocation leaves it marked */
struct sbuf
{
    char  *s;
    size_t len, cap;
    int    failed;
};

static void sb_printf(struct sbuf *b, const char *fmt, ...)
{
    for (;;)
    {
        va_list ap;
        int     n;

        if (b->failed)
            return;
        va_start(ap, fmt);
        n = trace_vfmt(b->s + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < b->cap - b->len)
        {
            b->len += (size_t)n;
            return;
        }
        else
        {
            size_t cap = b->cap * 2 + (n > 0 ? (size_t)n : 0) + 256;
            char  *s = realloc(b->s, cap);

            if (!s)
            {
                b->failed = 1;
                return;
            }
            b->s = s;
            b->cap = cap;
        }
    }
}

/* pens 0-7 are the CG's; 8 on are the mid-tones its dithers stand for */
static uint32_t pal[32];
static int npal;

static double lum(uint32_t c)
{
    return 0.299 * (c >> 16) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255);
}

static int mid_pen(int a, int b)
{
    uint32_t ca = pal[a], cb = pal[b];
    uint32_t c = ((((ca >> 16) & 255) + ((cb >> 16) & 255)) / 2) << 16 | ((((ca >> 8) & 255) + ((cb >> 8) & 255)) / 2) << 8 |
                 (((ca & 255) + (cb & 255)) / 2);
    int i;

    for (i = 0; i < npal; i++)
        if (pal[i] == c)
            return i;
    if (npal == 32)
        return a;
    pal[npal] = c;
    return npal++;
}

/* the image's pens, with 2x2 checkerboards (the CG's dithered mid-tones)
   turned into the mid-tone they stand for */
static uint8_t *undither(const struct cg_art_image *im)
{
    int w = im->w, h = im->h, x, y;
    uint8_t *q = malloc((size_t)w * h), *mark = calloc((size_t)w * h, 1);

    for (x = 0; x < w * h; x++)
        q[x] = (im->px[x] & 7) == 0 ? 1 : im->px[x] & 7;
    /* a 3x3 checkerboard: smaller ones are the diagonal joins of pixel
       letters, and a dotted line is one pixel wide */
    for (y = 0; y + 2 < h; y++)
        for (x = 0; x + 2 < w; x++)
        {
            int a = q[y * w + x], b = q[y * w + x + 1], i, j, ok = a != b;

            for (j = 0; j < 3 && ok; j++)
                for (i = 0; i < 3 && ok; i++)
                    ok = q[(y + j) * w + x + i] == (((i + j) & 1) ? b : a);
            if (ok)
                for (j = 0; j < 3; j++)
                    for (i = 0; i < 3; i++)
                        mark[(y + j) * w + x + i] = 1;
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (mark[y * w + x])
            {
                /* the other colour of the checker: a neighbour also marked */
                int other = -1;

                if (x + 1 < w && mark[y * w + x + 1] && im->px[y * w + x + 1] != im->px[y * w + x])
                    other = q[y * w + x + 1];
                else if (x > 0 && mark[y * w + x - 1])
                    other = q[y * w + x - 1];
                if (other >= 0 && other < 8 && q[y * w + x] < 8)
                    q[y * w + x] = (uint8_t)mid_pen(q[y * w + x], other);
            }
    /* the second pass above turned one of each pair already; settle pairs */
    free(mark);
    return q;
}

/* A button in the CG's bevel: brown or blue down the left, 2 px, and
   its dark bevel along the bottom. Its frame's width and height, from
   the dark bevel's last column and row. */
static int framed(const struct cg_art_image *im, int *fw, int *fh)
{
    int w = im->w, h = im->h, p, q, dark, x, y;

    if (w < 12 || h < 10)
        return 0;
    p = im->px[(h / 2) * w + 0] & 7;
    q = im->px[(h / 2) * w + 1] & 7;
    dark = im->px[(h - 1) * w + w / 2] & 7;
    if (p != q || (p != 3 && p != 5) || dark == p)
        return 0;
    for (x = w - 1; x > 0 && (im->px[(h / 2) * w + x] & 7) != dark; x--)
        ;
    for (y = h - 1; y > 0 && (im->px[y * w + w / 2] & 7) != dark; y--)
        ;
    *fw = x + 1;
    *fh = y + 1;
    return *fw > 8 && *fh > 8;
}

static void frame(struct sbuf *f, int w, int h, uint32_t light, uint32_t dark, uint32_t face)
{
    double r = 2.5, t = 2, k = r - r / sqrt(2);
    double trx = w - k, try_ = k, blx = k, bly = h - k;

    sb_printf(f, "<path fill=\"#%06x\" d=\"M%.3f %.3f L0 %g A%g %g 0 0 1 %g 0 L%g 0 A%g %g 0 0 1 %.3f %.3f L%g %g L%g %g L%g %g Z\"/>",
            light, blx, bly, r, r, r, r, w - r, r, r, trx, try_, w - t, t, t, t, t, h - t);
    sb_printf(f, "<path fill=\"#%06x\" d=\"M%.3f %.3f A%g %g 0 0 1 %d %g L%d %g A%g %g 0 0 1 %g %d L%g %d A%g %g 0 0 1 %.3f %.3f L%g %g L%g %g L%g %g Z\"/>",
            dark, trx, try_, r, r, w, r, w, h - r, r, r, w - r, h, r, h, r, r, blx, bly, t, h - t, w - t, h - t, w - t, t);
    sb_printf(f, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"1\" fill=\"#%06x\"/>", t, t, w - 2 * t, h - 2 * t, face);
}

/* the gadget being drawn, for the parts of it that are drawn by hand */
static int cur_id = -1;

/* pictures of straight-edged things (pages, boxes, arrows, a ruler),
   unlettered, are traced as polygons: their edges stay straight and their
   corners square; the rest, curved things and lettering, smoothly */
static const int geometric[] = { 3, 4, 5, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 61, 62, 68, 69, 74, 75,
                                 95, 100, 101, 103, 104, 105, 109, 110, 111, 112 };

static int is_geometric(int id)
{
    size_t i;

    for (i = 0; i < sizeof(geometric) / sizeof(geometric[0]); i++)
        if (geometric[i] == id)
            return 1;
    return 0;
}

static int erase_drawn(int x, int y);

static void layers(struct sbuf *f, const struct cg_art_image *im, int x0, int y0, int x1, int y1, int skip)
{
    int count[32] = { 0 }, order[32], n = 0, i, j, x, y;
    int w = im->w, h = im->h;
    uint8_t *mask = malloc((size_t)w * h), *pen = undither(im);

    /* what is drawn by hand is not traced */
    if (skip >= 0)
        for (y = y0; y < y1; y++)
            for (x = x0; x < x1; x++)
                if (erase_drawn(x, y))
                    pen[y * w + x] = (uint8_t)skip;
    /* in a frame, the bevel's rounded inner corners reach a pixel into
       each corner of the interior: they are the face */
    if (skip >= 0)
    {
        pen[y0 * w + x0] = pen[y0 * w + x1 - 1] = (uint8_t)skip;
        pen[(y1 - 1) * w + x0] = pen[(y1 - 1) * w + x1 - 1] = (uint8_t)skip;
    }

    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            count[pen[y * w + x]]++;
    for (i = 0; i < npal; i++)
        if (count[i] && i != skip)
            order[n++] = i;
    /* light first; each layer also covers the darker ones drawn over it */
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (lum(pal[order[j]]) > lum(pal[order[i]]))
            {
                int t = order[i];

                order[i] = order[j];
                order[j] = t;
            }
    for (i = 0; i < n; i++)
    {
        char *d;

        memset(mask, 0, (size_t)w * h);
        for (y = y0; y < y1; y++)
            for (x = x0; x < x1; x++)
            {
                int p = pen[y * w + x], k, dx, dy;

                /* its own pixels, and the darker ones touching them: the
                   layers overlap a pixel so their edges leave no seam */
                if (p == order[i])
                    mask[y * w + x] = 1;
                else
                    for (k = i + 1; k < n; k++)
                        if (p == order[k])
                            for (dy = -1; dy <= 1; dy++)
                                for (dx = -1; dx <= 1; dx++)
                                    if (x + dx >= x0 && y + dy >= y0 && x + dx < x1 && y + dy < y1 &&
                                        pen[(y + dy) * w + x + dx] == order[i])
                                        mask[y * w + x] = 1;
            }
        /* a hole of a pixel or two of the face inside one ink was a gap
           between pens the bar draws alike: filled */
        if (skip >= 0)
            for (y = y0; y < y1; y++)
                for (x = x0; x < x1; x++)
                {
                    int hx[3], hy[3], nh = 0, open = 0, q, dx2, dy2, dd;

                    if (mask[y * w + x] || pen[y * w + x] != skip)
                        continue;
                    hx[0] = x;
                    hy[0] = y;
                    nh = 1;
                    for (q = 0; q < nh && !open; q++)
                        for (dd = 0; dd < 4 && !open; dd++)
                        {
                            int ax, ay, r;

                            dx2 = dd == 0 ? 1 : dd == 1 ? -1 : 0;
                            dy2 = dd == 2 ? 1 : dd == 3 ? -1 : 0;
                            ax = hx[q] + dx2;
                            ay = hy[q] + dy2;
                            if (ax < x0 || ay < y0 || ax >= x1 || ay >= y1)
                            {
                                open = 1;
                                break;
                            }
                            if (mask[ay * w + ax])
                                continue;
                            if (pen[ay * w + ax] != skip)
                            {
                                open = 1;
                                break;
                            }
                            for (r = 0; r < nh && (hx[r] != ax || hy[r] != ay); r++)
                                ;
                            if (r == nh)
                            {
                                if (nh == 2)
                                    open = 1;
                                else
                                {
                                    hx[nh] = ax;
                                    hy[nh] = ay;
                                    nh++;
                                }
                            }
                        }
                    if (!open)
                        for (q = 0; q < nh; q++)
                            mask[hy[q] * w + hx[q]] = 1;
                }
        d = is_geometric(cur_id) ? trace_poly(mask, w, h) : trace_mask(mask, w, h, sigma, tol);
        if (*d)
            sb_printf(f, "<path fill=\"#%06x\"%s d=\"%s\"/>", pal[order[i]], is_geometric(cur_id) ? "" : " fill-rule=\"evenodd\"", d);
        free(d);
    }
    free(mask);
    free(pen);
}

/* The erase buttons (page, book, line): an eraser and what it rubs out,
   drawn in pixel shading the tracer cannot make clean (dotted outlines,
   a hatched side face), are drawn; their labels are traced. */
#define ID_ERASE_PAGE 55
#define ID_ERASE_BOOK 56
#define ID_ERASE_LINE 93

static int erase_drawn(int x, int y)
{
    switch (cur_id)
    {
    case ID_ERASE_PAGE: return y < 19 || (y <= 21 && x >= 31);
    case ID_ERASE_BOOK: return y < 17 || (y <= 18 && x >= 6 && x <= 14) || (y <= 19 && x >= 27) || (y <= 22 && x >= 31);
    case ID_ERASE_LINE: return y < 17 || (y <= 19 && x >= 24) || (y <= 22 && x >= 28);
    default: return 0;
    }
}

static void page_shape(struct sbuf *f, double x, double y, uint32_t ink, uint32_t face, int dashed, int lines)
{
    /* a page 9.5 x 11 with its top right corner folded */
    sb_printf(f, "<path d=\"M%.2f %.2f H%.2f L%.2f %.2f V%.2f H%.2f Z\" fill=\"#%06x\"/>", x, y, x + 6, x + 9.5, y + 3.5,
            y + 11, x, face);
    sb_printf(f, "<path d=\"M%.2f %.2f H%.2f L%.2f %.2f V%.2f H%.2f\" fill=\"none\" stroke=\"#%06x\" stroke-width=\"1.3\" "
               "stroke-linejoin=\"round\"/>", x + 3.5, y, x + 6, x + 9.5, y + 3.5, y + 11, x + 3.5, ink);
    sb_printf(f, "<path d=\"M%.2f %.2f H%.2f V%.2f H%.2f\" fill=\"none\" stroke=\"#%06x\" stroke-width=\"1.3\"%s/>", x + 3.5,
            y + 11, x, y, x + 3.5, ink, dashed ? " stroke-dasharray=\"1.1 1.1\"" : "");
    sb_printf(f, "<path d=\"M%.2f %.2f V%.2f H%.2f Z\" fill=\"#%06x\"/>", x + 6, y, y + 3.5, x + 9.5, ink);
    if (lines)
        sb_printf(f, "<g stroke=\"#%06x\" stroke-width=\"1.2\" stroke-linecap=\"round\"><path d=\"M%.2f %.2fh2.2\"/>"
                   "<path d=\"M%.2f %.2fh3.2\"/><path d=\"M%.2f %.2fh4\"/></g>", ink, x + 3, y + 4.5, x + 3, y + 6.5, x + 3, y + 8.5);
}

static int erase(struct sbuf *f, int id, uint32_t ink, uint32_t face)
{
    uint32_t mid = ((((ink >> 16) & 255) + ((face >> 16) & 255)) / 2) << 16 |
                   ((((ink >> 8) & 255) + ((face >> 8) & 255)) / 2) << 8 | (((ink & 255) + (face & 255)) / 2);

    if (id != ID_ERASE_PAGE && id != ID_ERASE_BOOK && id != ID_ERASE_LINE)
        return 0;
    if (id == ID_ERASE_PAGE)
        page_shape(f, 7.5, 4.5, ink, face, 1, 1);
    else if (id == ID_ERASE_BOOK)
    {
        page_shape(f, 10.5, 3.5, ink, face, 0, 0);
        page_shape(f, 7, 5.5, ink, face, 0, 0);
        page_shape(f, 3.5, 7.5, ink, face, 1, 1);
    }
    else
    {
        /* the line, and the dotted traces the eraser has left of it */
        sb_printf(f, "<path d=\"M3.5 10.5 H22\" stroke=\"#%06x\" stroke-width=\"1.6\" stroke-linecap=\"round\"/>", ink);
        sb_printf(f, "<g stroke=\"#%06x\" stroke-width=\"1\" stroke-linecap=\"round\" stroke-dasharray=\"0.01 2\">"
                   "<path d=\"M5.5 8.5 H22\"/><path d=\"M4.5 12.5 H22\"/></g>", ink);
    }
    /* the eraser: its side face, shaded, under its black top */
    sb_printf(f, "<path d=\"M23 8.6 L31.6 17.6 L37.6 11.8 L36.2 15.6 L30.2 21.5 L21.6 12.5 Z\" fill=\"#%06x\" "
               "stroke=\"#%06x\" stroke-width=\"0.6\" stroke-linejoin=\"round\"/>", mid, mid);
    sb_printf(f, "<path d=\"M28.8 2.8 Q29.5 2.4 30.2 3 L37.4 10.6 Q38.1 11.4 37.4 12.1 L32.4 17.2 Q31.6 18 30.8 17.2 "
               "L23.4 9.6 Q22.7 8.8 23.4 8.1 Z\" fill=\"#%06x\"/>", ink);
    return 1;
}

/* The speed gauges (scroll 1-5, crawl 1-4): their needles and ticks are
   dithered single pixels no tracer keeps, so they are drawn: the black
   dial, the ticks at the angles of the original's tick pixels, the
   needle from its pivot towards the original's farthest needle pixel. */
static int gauge(struct sbuf *f, const struct cg_art_image *im, int id, int face)
{
    int w = im->w, h = im->h, x, y, accent = -1, n = 0;
    double px = 20.5, py = 23.5, tx = 0, ty = 0, best = -1;
    double ang[16];

    if (id < 44 || id > 52)
        return 0;
    /* the accent pen: neither black nor the face */
    for (y = 4; y < h - 4 && accent < 0; y++)
        for (x = 4; x < w - 4; x++)
        {
            int p = im->px[y * w + x] & 7;

            if (p > 1 && p != face)
            {
                accent = p;
                break;
            }
        }
    if (accent < 0)
        return 0;
    /* the needle: the accent pixels joined (8-way) to the lowest one, its
       pivot; its tip the one of them farthest from there */
    {
        int *stack = malloc(sizeof(int) * (size_t)w * h), sp = 0, seed = -1;
        uint8_t *seen = calloc((size_t)w * h, 1);

        for (y = h - 5; y >= 4 && seed < 0; y--)
            for (x = 4; x < w - 4; x++)
                if ((im->px[y * w + x] & 7) == accent)
                {
                    seed = y * w + x;
                    break;
                }
        if (seed >= 0)
        {
            stack[sp++] = seed;
            seen[seed] = 1;
        }
        while (sp)
        {
            int c = stack[--sp], cx = c % w, cy = c / w, dx, dy;
            double d = hypot(cx + 0.5 - px, cy + 0.5 - py);

            if (d > best)
            {
                best = d;
                tx = cx + 0.5;
                ty = cy + 0.5;
            }
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                {
                    int nx = cx + dx, ny = cy + dy;

                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && !seen[ny * w + nx] &&
                        (im->px[ny * w + nx] & 7) == accent)
                    {
                        seen[ny * w + nx] = 1;
                        stack[sp++] = ny * w + nx;
                    }
                }
        }
        free(stack);
        free(seen);
    }
    /* ticks: accent pixels far from the pivot and off the needle's line */
    for (y = 4; y < h - 4; y++)
        for (x = 4; x < w - 4; x++)
            if ((im->px[y * w + x] & 7) == accent)
            {
                double a = atan2(py - (y + 0.5), x + 0.5 - px), na = atan2(py - ty, tx - px), d = hypot(x + 0.5 - px, y + 0.5 - py);
                int j, dup = 0;

                if (d < 10 || fabs(a - na) < 0.25)
                    continue;
                for (j = 0; j < n; j++)
                    if (fabs(ang[j] - a) < 0.3)
                        dup = 1;
                if (!dup && n < 16)
                    ang[n++] = a;
            }
    sb_printf(f, "<ellipse cx=\"20.5\" cy=\"16.5\" rx=\"15.5\" ry=\"11.5\" fill=\"#%06x\"/>", pal[1]);
    sb_printf(f, "<g stroke=\"#%06x\" stroke-width=\"1.3\" stroke-linecap=\"round\" fill=\"none\">", pal[accent]);
    for (x = 0; x < n; x++)
        sb_printf(f, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"/>", px + 13.0 * cos(ang[x]),
                16.5 - 9.6 * sin(ang[x]) + (py - 16.5) * 0, px + 11.2 * cos(ang[x]), 16.5 - 8.0 * sin(ang[x]));
    sb_printf(f, "</g><line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#%06x\" stroke-width=\"2\" stroke-linecap=\"round\"/>",
            px, py - 1, tx, ty, pal[accent]);
    sb_printf(f, "<ellipse cx=\"%.2f\" cy=\"%.2f\" rx=\"3\" ry=\"1.6\" fill=\"#%06x\"/>", px, py - 0.5, pal[accent]);
    return 1;
}

static void icon(struct sbuf *f, int k, int sel)
{
    const struct cg_art_image *im = sel ? &cg_art_gadgets[k].select : &cg_art_gadgets[k].render;
    int fw, fh;

    sb_printf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">", im->w, im->h,
            im->w, im->h);
    if (framed(im, &fw, &fh))
    {
        int face = im->px[4 * im->w + 4] & 7;

        frame(f, fw, fh, pal[im->px[(fh / 2) * im->w] & 7], pal[im->px[(fh - 1) * im->w + fw / 2] & 7], pal[face]);
        cur_id = cg_art_gadgets[k].id;
        if (!gauge(f, im, cg_art_gadgets[k].id, face))
        {
            erase(f, cg_art_gadgets[k].id, pal[1], pal[face]);
            layers(f, im, 2, 2, fw - 2, fh - 2, face);
        }
        cur_id = -1;
    }
    else
    {
        cur_id = cg_art_gadgets[k].id;
        layers(f, im, 0, 0, im->w, im->h, -1);
        cur_id = -1;
    }
    sb_printf(f, "</svg>");
}

void cg_trace_free(struct cg_icon *icons, int nicons, char **vfont, int nvfont)
{
    int i;

    for (i = 0; icons && i < nicons; i++)
        free((char *)icons[i].svg);
    free(icons);
    for (i = 0; vfont && i < nvfont; i++)
        free(vfont[i]);
    free(vfont);
}

int cg_trace(struct cg_icon **icons_out, int *nicons_out, char ***vfont_out, void (*note)(void *, int, int), void *ud)
{
    const struct cg_art_font *ft = &cg_art_font;
    int nvfont = ft->hi - ft->lo + 2, of = 0, done = 0, n = 0, k, sel, c;
    struct cg_icon *icons;
    char **vfont;

    for (npal = 0; npal < 8; npal++)
        pal[npal] = cg_art_palette[npal];
    for (k = 0; k < cg_art_ngadgets; k++)
        of += (cg_art_gadgets[k].render.px != NULL) + (cg_art_gadgets[k].select.px != NULL);
    icons = calloc((size_t)(of ? of : 1), sizeof(*icons));
    vfont = calloc((size_t)nvfont, sizeof(*vfont));
    if (!icons || !vfont)
        goto fail;
    of += nvfont;
    for (k = 0; k < cg_art_ngadgets; k++)
        for (sel = 0; sel < 2; sel++)
        {
            const struct cg_art_image *im = sel ? &cg_art_gadgets[k].select : &cg_art_gadgets[k].render;
            struct sbuf b = { NULL, 0, 0, 0 };

            if (!im->px || !im->w || !im->h)
                continue;
            if (note)
                note(ud, done, of);
            icon(&b, k, sel);
            if (b.failed || !b.s)
            {
                free(b.s);
                goto fail;
            }
            icons[n].gadget = k;
            icons[n].select = sel;
            icons[n].w = im->w;
            icons[n].h = im->h;
            icons[n].svg = b.s;
            n++;
            done++;
        }
    /* the font: each character's ink as outlines in its cell (width x height) */
    for (c = ft->lo; c <= ft->hi + 1; c++)
    {
        const struct cg_art_char *g = &ft->chars[c - ft->lo];
        uint8_t *mask = calloc((size_t)(g->width ? g->width : 1) * ft->height, 1);
        int x, y;

        if (!mask)
            goto fail;
        if (note)
            note(ud, done++, of);
        for (y = 0; y < ft->height; y++)
            for (x = 0; x < g->width; x++)
                mask[y * g->width + x] = (ft->strip[y * ft->stripw + g->loc + x] & 7) != 2;
        vfont[c - ft->lo] = g->width ? trace_mask(mask, g->width, ft->height, sigma, tol) : calloc(1, 1);
        free(mask);
        if (!vfont[c - ft->lo])
            goto fail;
    }
    *icons_out = icons;
    *nicons_out = n;
    *vfont_out = vfont;
    return 1;
fail:
    cg_trace_free(icons, n, vfont, nvfont);
    return 0;
}
