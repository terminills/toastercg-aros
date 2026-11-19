/*
    trace: a pixel mask as smooth vector outlines. The mask is upscaled
    U times, blurred with a Gaussian of sigma (in source pixels), and
    its 0.5 level traced with marching squares: every cell emits the
    segments between the crossings on its edges, each crossing is shared
    by the two cells either side of it, and the segments are linked
    through them into closed rings, which are then simplified
    (Douglas-Peucker, tol in source pixels). Output: SVG path data in
    source pixel coordinates, for an even-odd fill.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trace.h"

#define U 16

struct pt
{
    double x, y;
};

/* blurred field, W x H samples, sample (i,j) at source (i/U - 1, j/U - 1) */
static float *field(const uint8_t *mask, int w, int h, double sigma, int *fw, int *fh)
{
    int W = w * U + 2 * U + 1, H = h * U + 2 * U + 1, r = (int)ceil(3 * sigma * U), x, y, k;
    float *a = calloc((size_t)W * H, sizeof(float)), *b = calloc((size_t)W * H, sizeof(float));
    float *kern = malloc(sizeof(float) * (size_t)(2 * r + 1)), sum = 0;

    for (k = -r; k <= r; k++)
        sum += kern[k + r] = sigma > 0 ? (float)exp(-(k * k) / (2.0 * sigma * U * sigma * U)) : (k == 0);
    for (k = 0; k <= 2 * r; k++)
        kern[k] /= sum;
    /* sample at the centre of each sub-pixel, a source pixel of margin all round */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
        {
            double sx = (x + 0.5) / U - 1, sy = (y + 0.5) / U - 1;
            int ix = (int)floor(sx), iy = (int)floor(sy);

            a[(size_t)y * W + x] = (ix >= 0 && iy >= 0 && ix < w && iy < h && mask[iy * w + ix]) ? 1.0f : 0.0f;
        }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
        {
            float s = 0;

            for (k = -r; k <= r; k++)
                if (x + k >= 0 && x + k < W)
                    s += kern[k + r] * a[(size_t)y * W + x + k];
            b[(size_t)y * W + x] = s;
        }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
        {
            float s = 0;

            for (k = -r; k <= r; k++)
                if (y + k >= 0 && y + k < H)
                    s += kern[k + r] * b[(size_t)(y + k) * W + x];
            a[(size_t)y * W + x] = s;
        }
    free(b);
    free(kern);
    *fw = W;
    *fh = H;
    return a;
}



/* ---- contour smoothing ------------------------------------------------ */

double trace_smooth = 0.6;     /* arc-length sigma of the contour smoothing, source px */
double trace_corner = 55;      /* turn (degrees over about a pixel) kept as a corner */

/* v with `places` decimals (0..3), exactly rounded; the digits back to front in d */
static int dec_digits(double v, int places, int strip, char *d)
{
    static const uint64_t ten[] = {1, 10, 100, 1000};
    uint64_t q = 0;
    int      e = 0, n = 0, i;
    double   m = frexp(fabs(v), &e);

    if (m != 0 && e <= 53)
    {
        uint64_t num = (uint64_t)ldexp(m, 53) * ten[places];
        int      s = 53 - e;

        if (s == 0)
            q = num;
        else if (s < 64)
        {
            uint64_t rem = num & (((uint64_t)1 << s) - 1), half = (uint64_t)1 << (s - 1);

            q = num >> s;
            if (rem > half || (rem == half && (q & 1)))
                q++;
        }
    }
    else if (m != 0)
        q = UINT64_MAX;
    for (i = 0; i < places; i++, q /= 10)
        if (!strip || n || q % 10)
            d[n++] = (char)('0' + q % 10);
    if (n)
        d[n++] = '.';
    do
        d[n++] = (char)('0' + q % 10);
    while (q /= 10);
    if (signbit(v))
        d[n++] = '-';
    return n;
}

int trace_vfmt(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t len = 0;

#define PUT(c) do { char c_ = (c); if (len + 1 < cap) out[len] = c_; len++; } while (0)
    for (; *fmt; fmt++)
    {
        char        d[40];
        const char *s;
        int         n = 0, zero = 0, width = 0, prec = -1;

        if (*fmt != '%')
        {
            PUT(*fmt);
            continue;
        }
        if (*++fmt == '0')
            zero = 1;
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + *fmt - '0';
        if (*fmt == '.')
            for (prec = 0, fmt++; *fmt >= '0' && *fmt <= '9'; fmt++)
                prec = prec * 10 + *fmt - '0';
        switch (*fmt)
        {
        case 'd':
        {
            int v = va_arg(ap, int);
            unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;

            do
                d[n++] = (char)('0' + u % 10);
            while (u /= 10);
            if (v < 0)
                d[n++] = '-';
            break;
        }
        case 'x':
        {
            unsigned u = va_arg(ap, unsigned);

            do
                d[n++] = "0123456789abcdef"[u & 15];
            while (u >>= 4);
            break;
        }
        case 'f':
            n = dec_digits(va_arg(ap, double), prec < 0 || prec > 3 ? 3 : prec, 0, d);
            break;
        case 'g':
            n = dec_digits(va_arg(ap, double), 3, 1, d);
            break;
        case 's':
            for (s = va_arg(ap, const char *); *s; s++)
                PUT(*s);
            continue;
        case 0:
            fmt--;
            continue;
        default:
            PUT(*fmt);
            continue;
        }
        for (; width > n; width--)
            PUT(zero ? '0' : ' ');
        while (n)
            PUT(d[--n]);
    }
#undef PUT
    if (cap)
        out[len < cap ? len : cap - 1] = 0;
    return (int)len;
}

static void outp(char **out, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;

    if (*len + 128 > *cap)
        *out = realloc(*out, *cap *= 2);
    va_start(ap, fmt);
    *len += (size_t)trace_vfmt(*out + *len, *cap - *len, fmt, ap);
    va_end(ap);
}

/* resample a closed ring every `step` along its length */
static struct pt *resample(const struct pt *p, int n, double step, int *m)
{
    double total = 0, acc = 0;
    int i, k = 0, cap;
    struct pt *q;

    for (i = 0; i < n; i++)
        total += hypot(p[(i + 1) % n].x - p[i].x, p[(i + 1) % n].y - p[i].y);
    cap = (int)(total / step) + 4;
    q = malloc(sizeof(*q) * (size_t)cap);
    for (i = 0; i < n && k < cap; i++)
    {
        const struct pt *a = &p[i], *b = &p[(i + 1) % n];
        double l = hypot(b->x - a->x, b->y - a->y), t;

        for (t = acc; t < l && k < cap; t += step)
        {
            q[k].x = a->x + (b->x - a->x) * t / l;
            q[k].y = a->y + (b->y - a->y) * t / l;
            k++;
        }
        acc = t - l;
    }
    *m = k;
    return q;
}

/* ---- cubic fitting (Schneider, Graphics Gems I) ----------------------- */

struct fitout
{
    char  **out;
    size_t *len, *cap;
};

static struct pt sub(struct pt a, struct pt b) { struct pt r = { a.x - b.x, a.y - b.y }; return r; }
static struct pt add(struct pt a, struct pt b) { struct pt r = { a.x + b.x, a.y + b.y }; return r; }
static struct pt mul(struct pt a, double k) { struct pt r = { a.x * k, a.y * k }; return r; }
static double dot(struct pt a, struct pt b) { return a.x * b.x + a.y * b.y; }
static struct pt unit(struct pt a) { double l = hypot(a.x, a.y); return l > 1e-12 ? mul(a, 1 / l) : a; }

static struct pt bez(const struct pt *b, double t)
{
    double u = 1 - t;

    return add(add(mul(b[0], u * u * u), mul(b[1], 3 * u * u * t)), add(mul(b[2], 3 * u * t * t), mul(b[3], t * t * t)));
}

static void outp(char **out, size_t *len, size_t *cap, const char *fmt, ...);

static void fit_cubic(const struct pt *d, int first, int last, struct pt t1, struct pt t2, double err, struct fitout *o)
{
    int n = last - first + 1, i, it, split = first;
    double *u = malloc(sizeof(double) * (size_t)n), maxe;
    struct pt b[4];

    if (n == 2)
    {
        double dist = hypot(d[last].x - d[first].x, d[last].y - d[first].y) / 3;

        b[0] = d[first];
        b[3] = d[last];
        b[1] = add(b[0], mul(t1, dist));
        b[2] = add(b[3], mul(t2, dist));
        outp(o->out, o->len, o->cap, "C%.2f %.2f %.2f %.2f %.2f %.2f", b[1].x, b[1].y, b[2].x, b[2].y, b[3].x, b[3].y);
        free(u);
        return;
    }
    /* chord-length parameters */
    u[0] = 0;
    for (i = 1; i < n; i++)
        u[i] = u[i - 1] + hypot(d[first + i].x - d[first + i - 1].x, d[first + i].y - d[first + i - 1].y);
    for (i = 1; i < n; i++)
        u[i] /= u[n - 1] > 0 ? u[n - 1] : 1;
    for (it = 0; it < 5; it++)
    {
        /* least squares for the two inner control points along t1, t2 */
        double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0, det, a1, a2, seg;

        for (i = 0; i < n; i++)
        {
            double t = u[i], v = 1 - t;
            struct pt A1 = mul(t1, 3 * t * v * v), A2 = mul(t2, 3 * t * t * v);
            struct pt tmp = sub(d[first + i], add(mul(d[first], v * v * v + 3 * t * v * v), mul(d[last], 3 * t * t * v + t * t * t)));

            c00 += dot(A1, A1);
            c01 += dot(A1, A2);
            c11 += dot(A2, A2);
            x0 += dot(A1, tmp);
            x1 += dot(A2, tmp);
        }
        det = c00 * c11 - c01 * c01;
        a1 = fabs(det) > 1e-12 ? (x0 * c11 - x1 * c01) / det : 0;
        a2 = fabs(det) > 1e-12 ? (c00 * x1 - c01 * x0) / det : 0;
        seg = hypot(d[last].x - d[first].x, d[last].y - d[first].y);
        if (a1 < 1e-6 * seg || a2 < 1e-6 * seg)
            a1 = a2 = seg / 3;
        b[0] = d[first];
        b[3] = d[last];
        b[1] = add(b[0], mul(t1, a1));
        b[2] = add(b[3], mul(t2, a2));
        /* the worst point */
        maxe = 0;
        split = first + n / 2;
        for (i = 1; i < n - 1; i++)
        {
            struct pt p = bez(b, u[i]);
            double e = (p.x - d[first + i].x) * (p.x - d[first + i].x) + (p.y - d[first + i].y) * (p.y - d[first + i].y);

            if (e > maxe)
            {
                maxe = e;
                split = first + i;
            }
        }
        if (maxe < err * err)
        {
            outp(o->out, o->len, o->cap, "C%.2f %.2f %.2f %.2f %.2f %.2f", b[1].x, b[1].y, b[2].x, b[2].y, b[3].x, b[3].y);
            free(u);
            return;
        }
        if (maxe > 16 * err * err)
            break;
        /* Newton step on each parameter towards its point */
        for (i = 1; i < n - 1; i++)
        {
            double t = u[i], v = 1 - t;
            struct pt q1[3], q2[2], p = bez(b, t), dp, ddp;
            double num, den;

            q1[0] = mul(sub(b[1], b[0]), 3);
            q1[1] = mul(sub(b[2], b[1]), 3);
            q1[2] = mul(sub(b[3], b[2]), 3);
            q2[0] = mul(sub(q1[1], q1[0]), 2);
            q2[1] = mul(sub(q1[2], q1[1]), 2);
            dp = add(add(mul(q1[0], v * v), mul(q1[1], 2 * v * t)), mul(q1[2], t * t));
            ddp = add(mul(q2[0], v), mul(q2[1], t));
            num = dot(sub(p, d[first + i]), dp);
            den = dot(dp, dp) + dot(sub(p, d[first + i]), ddp);
            if (fabs(den) > 1e-12)
                u[i] = t - num / den;
        }
    }
    free(u);
    /* split at the worst point, with the tangent through it */
    {
        struct pt tc = unit(sub(d[split - 1], d[split + 1]));

        fit_cubic(d, first, split, t1, tc, err, o);
        fit_cubic(d, split, last, mul(tc, -1), t2, err, o);
    }
}

static void emit_ring(const struct pt *p0, int n0, double tol, char **out, size_t *len, size_t *cap)
{
    /* a speck (under a quarter of a pixel) is not drawn */
    {
        double area = 0;
        int k;

        for (k = 0; k < n0; k++)
            area += p0[k].x * p0[(k + 1) % n0].y - p0[(k + 1) % n0].x * p0[k].y;
        if (fabs(area) / 2 < 0.25)
            return;
    }
    const double step = 0.125;
    int n, i, j, w = (int)(1.0 / step), sw = (int)(trace_smooth * 3 / step);
    struct pt *p = resample(p0, n0, step, &n), *q;
    char *corner;

    if (n < 8)
    {
        free(p);
        return;
    }
    q = malloc(sizeof(*q) * (size_t)n);
    corner = calloc((size_t)n, 1);
    /* corners: the direction turns by more than trace_corner over a pixel */
    for (i = 0; i < n; i++)
    {
        const struct pt *a = &p[(i - w + n) % n], *b = &p[i], *c = &p[(i + w) % n];
        double a1 = atan2(b->y - a->y, b->x - a->x), a2 = atan2(c->y - b->y, c->x - b->x), d = fabs(a2 - a1);

        if (d > M_PI)
            d = 2 * M_PI - d;
        corner[i] = d * 180 / M_PI > trace_corner;
    }
    /* keep only the sharpest point of each run of corner samples */
    for (i = 0; i < n; i++)
        if (corner[i] && corner[(i + 1) % n])
        {
            int s = i, e = i, best = i;
            double bd = -1;

            while (corner[(e + 1) % n] && (e + 1) % n != s)
                e = (e + 1) % n;
            for (j = s;; j = (j + 1) % n)
            {
                const struct pt *a = &p[(j - w + n) % n], *b = &p[j], *c = &p[(j + w) % n];
                double a1 = atan2(b->y - a->y, b->x - a->x), a2 = atan2(c->y - b->y, c->x - b->x), d = fabs(a2 - a1);

                if (d > M_PI)
                    d = 2 * M_PI - d;
                if (d > bd)
                {
                    bd = d;
                    best = j;
                }
                if (j == e)
                    break;
            }
            for (j = s;; j = (j + 1) % n)
            {
                corner[j] = j == best;
                if (j == e)
                    break;
            }
            i = e;
        }
    /* Gaussian smoothing along the ring, not across a corner */
    for (i = 0; i < n; i++)
    {
        double sx = 0, sy = 0, ws = 0;

        if (corner[i])
        {
            q[i] = p[i];
            continue;
        }
        for (j = -sw; j <= sw; j++)
        {
            int k = (i + j + n) % n, blocked = 0, t;
            double wt = exp(-(j * step) * (j * step) / (2 * trace_smooth * trace_smooth));

            for (t = 1; t <= abs(j); t++)
                if (corner[(i + (j > 0 ? t : -t) + n) % n])
                    blocked = 1;
            if (blocked)
                continue;
            sx += wt * p[k].x;
            sy += wt * p[k].y;
            ws += wt;
        }
        q[i].x = sx / ws;
        q[i].y = sy / ws;
    }
    /* fit cubics span by span between corners (or round the whole ring
       when it has none), lines where a span is straight */
    {
        struct fitout o = { out, len, cap };
        int c0 = -1;

        for (i = 0; i < n; i++)
            if (corner[i])
            {
                c0 = i;
                break;
            }
        if (c0 < 0)
            c0 = 0;
        /* the ring unrolled from c0, closed back onto it */
        {
            struct pt *r = malloc(sizeof(*r) * (size_t)(n + 1));
            int s0, k;

            for (k = 0; k <= n; k++)
                r[k] = q[(c0 + k) % n];
            outp(out, len, cap, "M%.2f %.2f", r[0].x, r[0].y);
            s0 = 0;
            for (k = 1; k <= n; k++)
                if (k == n || corner[(c0 + k) % n])
                {
                    int has_corner_ends = corner[(c0 + s0) % n] || corner[(c0 + k) % n];
                    struct pt t1, t2;
                    int m = k - s0;

                    if (has_corner_ends)
                    {
                        int a2 = s0 + (m > 4 ? 3 : 1), b2 = k - (m > 4 ? 3 : 1);

                        t1 = unit(sub(r[a2], r[s0]));
                        t2 = unit(sub(r[b2], r[k]));
                    }
                    else
                    {
                        /* a smooth closed ring: tangents through the join */
                        t1 = unit(sub(r[1], r[n - 1]));
                        t2 = mul(t1, -1);
                    }
                    if (m >= 2)
                    {
                        /* a straight span is one line */
                        double maxd = 0, dx = r[k].x - r[s0].x, dy = r[k].y - r[s0].y, l = hypot(dx, dy);
                        int j;

                        for (j = s0 + 1; j < k && l > 1e-9; j++)
                        {
                            double dd = fabs(dy * (r[j].x - r[s0].x) - dx * (r[j].y - r[s0].y)) / l;

                            if (dd > maxd)
                                maxd = dd;
                        }
                        if (l > 1e-9 && maxd < tol)
                            outp(out, len, cap, "L%.2f %.2f", r[k].x, r[k].y);
                        else
                            fit_cubic(r, s0, k, t1, t2, tol, &o);
                    }
                    s0 = k;
                }
            outp(out, len, cap, "Z");
            free(r);
        }
    }
    free(p);
    free(q);
    free(corner);
}

struct seg
{
    long e[2];      /* edge ids of its two ends */
    int  done;
};

char *trace_mask(const uint8_t *mask, int w, int h, double sigma, double tol)
{
    int W, H, x, y, nseg = 0, capseg = 1024, i;
    float *f = field(mask, w, h, sigma, &W, &H);
    struct seg *seg = malloc(sizeof(*seg) * (size_t)capseg);
    long nedge = (long)W * H * 2;
    int *owner = malloc(sizeof(int) * (size_t)nedge * 2);
    size_t cap = 4096, len = 0;
    char *out = malloc(cap);

    for (i = 0; i < nedge * 2; i++)
        owner[i] = -1;
    out[0] = 0;
#define IN(X, Y) (f[(size_t)(Y) * W + (X)] > 0.5f)
    /* edge ids: top of cell (x,y) = horizontal edge (x,y); left = vertical edge (x,y) */
#define HE(X, Y) (((long)(Y) * W + (X)) * 2)
#define VE(X, Y) (((long)(Y) * W + (X)) * 2 + 1)
    for (y = 0; y < H - 1; y++)
        for (x = 0; x < W - 1; x++)
        {
            int c = IN(x, y) | IN(x + 1, y) << 1 | IN(x + 1, y + 1) << 2 | IN(x, y + 1) << 3;
            long top = HE(x, y), right = VE(x + 1, y), bottom = HE(x, y + 1), left = VE(x, y);
            long s[4];
            int n = 0, k;

            switch (c)
            {
            case 1: case 14: s[0] = left; s[1] = top; n = 2; break;
            case 2: case 13: s[0] = top; s[1] = right; n = 2; break;
            case 3: case 12: s[0] = left; s[1] = right; n = 2; break;
            case 4: case 11: s[0] = right; s[1] = bottom; n = 2; break;
            case 6: case 9: s[0] = top; s[1] = bottom; n = 2; break;
            case 7: case 8: s[0] = left; s[1] = bottom; n = 2; break;
            case 5: case 10:
            {
                /* saddle: the centre decides which corners join */
                float m = (f[(size_t)y * W + x] + f[(size_t)y * W + x + 1] + f[(size_t)(y + 1) * W + x] +
                           f[(size_t)(y + 1) * W + x + 1]) / 4;
                int centre_in = m > 0.5f;

                if ((c == 5) == centre_in)
                {
                    s[0] = left; s[1] = bottom; s[2] = top; s[3] = right;
                }
                else
                {
                    s[0] = left; s[1] = top; s[2] = right; s[3] = bottom;
                }
                n = 4;
                break;
            }
            default: break;
            }
            for (k = 0; k < n; k += 2)
            {
                int e;

                if (nseg == capseg)
                    seg = realloc(seg, sizeof(*seg) * (size_t)(capseg *= 2));
                seg[nseg].e[0] = s[k];
                seg[nseg].e[1] = s[k + 1];
                seg[nseg].done = 0;
                for (e = 0; e < 2; e++)
                    owner[s[k + e] * 2 + (owner[s[k + e] * 2] >= 0)] = nseg;
                nseg++;
            }
        }
    /* link into rings */
    for (i = 0; i < nseg; i++)
    {
        struct pt *p = NULL;
        int n = 0, capp = 0, cur = i;
        long at;

        if (seg[i].done)
            continue;
        at = seg[i].e[0];
        while (!seg[cur].done)
        {
            long next = seg[cur].e[0] == at ? seg[cur].e[1] : seg[cur].e[0];
            long ex = next / 2, ey = ex / W, exx = ex % W;
            double px, py, t;
            int o0, o1;

            seg[cur].done = 1;
            /* the crossing point on edge `next` */
            if (next % 2 == 0)
            {
                float a0 = f[(size_t)ey * W + exx], a1 = f[(size_t)ey * W + exx + 1];

                t = (0.5 - a0) / (a1 - a0);
                px = exx + t;
                py = ey;
            }
            else
            {
                float a0 = f[(size_t)ey * W + exx], a1 = f[(size_t)(ey + 1) * W + exx];

                t = (0.5 - a0) / (a1 - a0);
                px = exx;
                py = ey + t;
            }
            if (n == capp)
                p = realloc(p, sizeof(*p) * (size_t)(capp = capp ? capp * 2 : 256));
            p[n].x = (px + 0.5) / U - 1;
            p[n].y = (py + 0.5) / U - 1;
            n++;
            o0 = owner[next * 2];
            o1 = owner[next * 2 + 1];
            at = next;
            cur = o0 == cur ? o1 : o0;
            if (cur < 0)
                break;
        }
        if (n >= 3)
            emit_ring(p, n, tol, &out, &len, &cap);
        free(p);
    }
#undef IN
#undef HE
#undef VE
    free(f);
    free(seg);
    free(owner);
    return out;
}

/* ---- polygon tracing ---------------------------------------------------

   The mask's pixel boundary, as closed paths on the pixel corners, is
   approximated by the polygon of fewest sides whose every side stays
   within a pixel-wide band of the corners it replaces; a vertex that
   sits far off the line through its neighbours' midpoints is a corner,
   one close to it is rounded (the method of P. Selinger, "Potrace: a
   polygon-based tracing algorithm", 2003, written here from the paper).
   Straight edges stay straight; steps that stand for a curve become
   one. */

double trace_alphamax = 1.0;    /* above this a vertex is a corner */
double trace_poly_corner = 60;  /* so is one turning this many degrees */
double trace_round = 1.0;       /* the most of a side a rounding takes, px */

static int poly_ink(const uint8_t *m, int w, int h, int x, int y)
{
    return x >= 0 && y >= 0 && x < w && y < h && m[y * w + x];
}

/* the pixels right and left of the edge leaving corner (x,y) in
   direction d (0 east, 1 south, 2 west, 3 north) */
static const int right_dx[4] = { 0, -1, -1, 0 }, right_dy[4] = { 0, 0, -1, -1 };
static const int left_dx[4] = { 0, 0, -1, -1 }, left_dy[4] = { -1, 0, 0, -1 };
static const int step_dx[4] = { 1, 0, -1, 0 }, step_dy[4] = { 0, 1, 0, -1 };

/* whether the steps i..i+len of the closed path (n corners) are a
   straight line as pixel art draws one: a single direction, or two where
   one always moves a single pixel and the runs of the other, the end ones
   aside, differ by at most one; a lone step between two runs is a step,
   not a slope */
static int poly_straight(const int *px, const int *py, int n, int i, int len)
{
    int dir[2] = { -1, -1 }, runs[2][64], nr[2] = { 0, 0 }, k, prev = -1, cur = 0, a, mn, mj, lo, hi;

    for (k = 0; k < len; k++)
    {
        int p0 = (i + k) % n, p1 = (i + k + 1) % n,
            d = px[p1] > px[p0] ? 0 : py[p1] > py[p0] ? 1 : px[p1] < px[p0] ? 2 : 3, s2;

        s2 = d == dir[0] ? 0 : d == dir[1] ? 1 : dir[0] < 0 ? (dir[0] = d, 0) : dir[1] < 0 ? (dir[1] = d, 1) : -1;
        if (s2 < 0 || (dir[1] >= 0 && (dir[0] ^ dir[1]) == 2))
            return 0;
        if (s2 != prev && prev >= 0)
        {
            if (nr[prev] == 64)
                return 0;
            runs[prev][nr[prev]++] = cur;
            cur = 0;
        }
        prev = s2;
        cur++;
    }
    if (nr[prev] == 64)
        return 0;
    runs[prev][nr[prev]++] = cur;
    if (dir[1] < 0)
        return 1;
    /* the minor direction moves a pixel at a time */
    for (mn = 0; mn < 2; mn++)
    {
        for (a = 0; a < nr[mn] && runs[mn][a] == 1; a++)
            ;
        if (a == nr[mn])
            break;
    }
    if (mn == 2)
        return 0;
    mj = 1 - mn;
    /* one step across a pixel's corner would cut the pixel off */
    if (nr[mn] == 1)
        return nr[mj] == 2 && runs[mj][0] == 1 && runs[mj][1] == 1;
    /* the major direction's runs: interior ones within one of each other,
       end ones no longer */
    lo = 1 << 30;
    hi = 0;
    {
        int start_major = ((px[(i + 1) % n] - px[i]) != 0) == (dir[mj] == 0 || dir[mj] == 2), interior = 0;

        for (a = 0; a < nr[mj]; a++)
        {
            int is_end = (a == 0 && start_major) || (a == nr[mj] - 1 && prev == mj);

            if (is_end)
                continue;
            interior++;
            if (runs[mj][a] < lo)
                lo = runs[mj][a];
            if (runs[mj][a] > hi)
                hi = runs[mj][a];
        }
        if (hi == 0)
            return 0;
        /* a long run with a step at each end is an edge with two notches,
           not a slope: a shallow slope shows at least two whole runs */
        if (interior < 2 && hi > 2)
            return 0;
        if (hi - lo > 1)
            return 0;
        for (a = 0; a < nr[mj]; a++)
            if (runs[mj][a] > hi)
                return 0;
    }
    return 1;
}

/* squared distances of corners i..i+len from their chord */
static double poly_penalty(const int *px, const int *py, int n, int i, int len)
{
    double dx = px[(i + len) % n] - px[i], dy = py[(i + len) % n] - py[i], l2 = dx * dx + dy * dy, s = 0;
    int k;

    for (k = 1; k < len; k++)
    {
        int a = (i + k) % n;
        double c = dx * (py[a] - py[i]) - dy * (px[a] - px[i]);

        s += c * c / l2;
    }
    return s;
}

/* least squares line through corners i..i+len: centre and direction */
static void poly_line(const int *px, const int *py, int n, int i, int len, struct pt *c, struct pt *dir)
{
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, a, b, d, t;
    int k;

    for (k = 0; k <= len; k++)
    {
        int j = (i + k) % n;

        sx += px[j];
        sy += py[j];
    }
    c->x = sx / (len + 1);
    c->y = sy / (len + 1);
    for (k = 0; k <= len; k++)
    {
        int j = (i + k) % n;
        double x = px[j] - c->x, y = py[j] - c->y;

        sxx += x * x;
        syy += y * y;
        sxy += x * y;
    }
    /* the principal axis of the scatter */
    a = sxx - syy;
    b = 2 * sxy;
    t = 0.5 * atan2(b, a);
    dir->x = cos(t);
    dir->y = sin(t);
    d = (px[(i + len) % n] - px[i]) * dir->x + (py[(i + len) % n] - py[i]) * dir->y;
    if (d < 0)
        *dir = mul(*dir, -1);
}

static void poly_ring(const int *px, const int *py, int n, char **out, size_t *len, size_t *cap)
{
    int *lon = malloc(sizeof(int) * (size_t)n), *best = NULL, *from = malloc(sizeof(int) * (size_t)(n + 1));
    double *pen = malloc(sizeof(double) * (size_t)(n + 1)), bestpen = 0, *pc;
    char *ok;
    int *cnt = malloc(sizeof(int) * (size_t)(n + 1)), nbest = 0, i, k, s, m, maxl = 1;
    struct pt *v, *lc, *ld;

    /* how far from each corner a side could reach: until a third
       direction (a run of two steps is not straight but one of four
       can be) */
    for (i = 0; i < n; i++)
    {
        int l = 1, dirs = 0, d;

        for (l = 0; l + 1 < n; l++)
        {
            int a = (i + l) % n, b = (i + l + 1) % n;

            d = px[b] > px[a] ? 0 : py[b] > py[a] ? 1 : px[b] < px[a] ? 2 : 3;
            dirs |= 1 << d;
            if (__builtin_popcount((unsigned)dirs) > 2)
                break;
        }
        lon[i] = l < 1 ? 1 : l;
        if (lon[i] > maxl)
            maxl = lon[i];
    }
    pc = malloc(sizeof(double) * (size_t)n * (size_t)(maxl + 1));
    ok = calloc((size_t)n * (size_t)(maxl + 1), 1);
    for (i = 0; i < n; i++)
        for (k = 1; k <= lon[i]; k++)
            if (k == 1 || poly_straight(px, py, n, i, k))
            {
                ok[(size_t)i * (maxl + 1) + k] = 1;
                pc[(size_t)i * (maxl + 1) + k] = poly_penalty(px, py, n, i, k);
            }
    /* the fewest sides (least penalty among those) over every start */
    for (s = 0; s < n; s++)
    {
        cnt[0] = 0;
        pen[0] = 0;
        for (k = 1; k <= n; k++)
            cnt[k] = n + 1;
        for (k = 0; k < n; k++)
        {
            int l;

            if (cnt[k] > n)
                continue;
            for (l = 1; l <= lon[(s + k) % n] && k + l <= n; l++)
            {
                int c = cnt[k] + 1;

                if (!ok[(size_t)((s + k) % n) * (maxl + 1) + l])
                    continue;
                double p = pen[k] + pc[(size_t)((s + k) % n) * (maxl + 1) + l];

                if (c < cnt[k + l] || (c == cnt[k + l] && p < pen[k + l]))
                {
                    cnt[k + l] = c;
                    pen[k + l] = p;
                    from[k + l] = k;
                }
            }
        }
        if (cnt[n] <= n && (!best || cnt[n] < nbest || (cnt[n] == nbest && pen[n] < bestpen - 1e-9)))
        {
            free(best);
            best = malloc(sizeof(int) * (size_t)(cnt[n] + 1));
            nbest = cnt[n];
            bestpen = pen[n];
            for (k = n, m = nbest; m >= 0; m--)
            {
                best[m] = (s + k) % n;
                if (m)
                    k = from[k];
            }
        }
    }
    free(pc);
    free(ok);
    /* a pixel or two: its corners are the polygon */
    if (!best || nbest < 3)
    {
        free(best);
        best = malloc(sizeof(int) * (size_t)(n + 1));
        for (nbest = 0, k = 0; k < n; k++)
        {
            int a = (k + n - 1) % n, b = (k + 1) % n;

            if ((px[b] - px[k]) * (py[k] - py[a]) != (py[b] - py[k]) * (px[k] - px[a]))
                best[nbest++] = k;
        }
        best[nbest] = best[0];
        if (nbest < 3)
        {
            free(best);
            goto done;
        }
    }
    m = nbest;
    /* each vertex where the fitted lines of its two sides meet, kept
       within half a pixel of its corner */
    v = malloc(sizeof(*v) * (size_t)m);
    lc = malloc(sizeof(*lc) * (size_t)m);
    ld = malloc(sizeof(*ld) * (size_t)m);
    for (k = 0; k < m; k++)
    {
        int a = best[k], b = best[k + 1], l = (b - a + n) % n;

        poly_line(px, py, n, a, l ? l : n, &lc[k], &ld[k]);
    }
    for (k = 0; k < m; k++)
    {
        int j = (k + m - 1) % m, c = best[k];
        double den = ld[j].x * ld[k].y - ld[j].y * ld[k].x;
        struct pt q = { px[c], py[c] };

        if (fabs(den) > 1e-6)
        {
            struct pt d = sub(lc[k], lc[j]);
            double t = (d.x * ld[k].y - d.y * ld[k].x) / den;

            q = add(lc[j], mul(ld[j], t));
        }
        if (q.x < px[c] - 0.5)
            q.x = px[c] - 0.5;
        if (q.x > px[c] + 0.5)
            q.x = px[c] + 0.5;
        if (q.y < py[c] - 0.5)
            q.y = py[c] - 0.5;
        if (q.y > py[c] + 0.5)
            q.y = py[c] + 0.5;
        v[k] = q;
    }
    /* each vertex rounded over at most trace_round of its sides, or a
       corner; straight between */
    {
        struct pt *in = malloc(sizeof(*in) * (size_t)m), *ou = malloc(sizeof(*ou) * (size_t)m);

        for (k = 0; k < m; k++)
        {
            struct pt a = v[(k + m - 1) % m], b = v[k], c = v[(k + 1) % m];
            double la = hypot(b.x - a.x, b.y - a.y), lb = hypot(c.x - b.x, c.y - b.y);

            in[k] = sub(b, mul(unit(sub(b, a)), la / 2 < trace_round ? la / 2 : trace_round));
            ou[k] = add(b, mul(unit(sub(c, b)), lb / 2 < trace_round ? lb / 2 : trace_round));
        }
        outp(out, len, cap, "M%.2f %.2f", in[0].x, in[0].y);
        for (k = 0; k < m; k++)
        {
            struct pt a = v[(k + m - 1) % m], b = v[k], c = v[(k + 1) % m];
            double la = hypot(b.x - a.x, b.y - a.y), lb = hypot(c.x - b.x, c.y - b.y),
                   l1 = fabs(c.x - a.x) + fabs(c.y - a.y),
                   dd = l1 > 1e-9 ? fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / l1 : 0,
                   alpha = (dd > 1 ? 1 - 1 / dd : 0) / 0.75,
                   turn = fabs(atan2((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x),
                                     (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y))) * 180 / M_PI;

            if (turn >= trace_poly_corner && la >= 1.5 && lb >= 1.5)
                outp(out, len, cap, "L%.2f %.2fL%.2f %.2f", b.x, b.y, ou[k].x, ou[k].y);
            else
            {
                struct pt c1, c2;

                if (alpha < 0.55)
                    alpha = 0.55;
                if (alpha > 1)
                    alpha = 1;
                c1 = add(in[k], mul(sub(b, in[k]), alpha));
                c2 = add(ou[k], mul(sub(b, ou[k]), alpha));
                outp(out, len, cap, "C%.2f %.2f %.2f %.2f %.2f %.2f", c1.x, c1.y, c2.x, c2.y, ou[k].x, ou[k].y);
            }
            if (k + 1 < m)
                outp(out, len, cap, "L%.2f %.2f", in[k + 1].x, in[k + 1].y);
        }
        outp(out, len, cap, "Z");
        free(in);
        free(ou);
    }
    free(v);
    free(lc);
    free(ld);
    free(best);
done:
    free(lon);
    free(from);
    free(pen);
    free(cnt);
}

char *trace_poly(const uint8_t *mask, int w, int h)
{
    size_t len = 0, cap = 4096;
    char *out = malloc(cap), *seen = calloc((size_t)(w + 1) * (h + 1) * 4, 1);
    int x, y;

    out[0] = 0;
    /* every boundary is entered once along the top edge of an ink pixel */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (mask[y * w + x] && !poly_ink(mask, w, h, x, y - 1) && !seen[((size_t)y * (w + 1) + x) * 4])
            {
                int *px = NULL, *py = NULL, n = 0, capn = 0, cx = x, cy = y, d = 0;

                do
                {
                    int nd, t;

                    seen[((size_t)cy * (w + 1) + cx) * 4 + d] = 1;
                    if (n == capn)
                    {
                        capn = capn ? capn * 2 : 256;
                        px = realloc(px, sizeof(int) * (size_t)capn);
                        py = realloc(py, sizeof(int) * (size_t)capn);
                    }
                    px[n] = cx;
                    py[n] = cy;
                    n++;
                    cx += step_dx[d];
                    cy += step_dy[d];
                    /* ink stays on the right; where two ink pixels meet
                       only at this corner the boundary keeps them joined */
                    for (t = 0, nd = (d + 3) % 4; t < 3; t++, nd = (nd + 1) % 4)
                        if (poly_ink(mask, w, h, cx + right_dx[nd], cy + right_dy[nd]) &&
                            !poly_ink(mask, w, h, cx + left_dx[nd], cy + left_dy[nd]))
                            break;
                    d = nd;
                } while (cx != x || cy != y || d != 0);
                poly_ring(px, py, n, &out, &len, &cap);
                free(px);
                free(py);
            }
    free(seen);
    return out;
}
