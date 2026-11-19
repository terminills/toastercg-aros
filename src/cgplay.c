/*
    Scroll and crawl playback. The page is rendered once into a strip
    (the whole height of a scroll, the whole length of a crawl) and each
    field shows a window of it; the background stays put.

    The speeds are the Toaster's (SCSpeed and CRSpeed of the CG's
    newscroll.c): a scroll moves half a line a field at speed 0 and
    1 to 8 lines at speeds 1 to 8, a crawl 2, 4, 8 or 16 pixels.
*/

#include <stdlib.h>
#include <string.h>

#include "cgplay.h"

struct cg_play
{
    int      type, playback, percent;
    int      speed;     /* half Toaster pixels per field */
    long     pos200;    /* pos in two hundredths of an output pixel */
    uint8_t *strip, *back;
    int      sw, sh;    /* strip size */
    int      pos, end, held, done;
    int      fw, fh;    /* the frame */
    int      top;       /* crawl: where its line sits */
};

int cg_play_scroll_speed(int speed)
{
    return speed ? 2 * (speed > 8 ? 8 : speed) : 1;
}

int cg_play_crawl_speed(int speed)
{
    return 4 << (speed > 3 ? 3 : speed);
}

static int content_extent(const struct cg_page *pg, int *top, int *bottom)
{
    const struct cg_line *l;

    *top = 32767;
    *bottom = -32768;
    for (l = pg->lines; l; l = l->next)
    {
        if (l->yoffset < *top)
            *top = l->yoffset;
        if (l->yoffset + l->total_height > *bottom)
            *bottom = l->yoffset + l->total_height;
    }
    return *top <= *bottom;
}

struct cg_play *cg_play_new(struct cg_renderer *r, const struct cg_book *book, int page, int percent)
{
    const struct cg_page *pg = &book->pages[page];
    struct cg_play *p = calloc(1, sizeof(*p));
    struct cg_book *strip_book;
    double scale = percent / 100.0;
    int top, bottom;

    if (!p)
        return NULL;
    p->type = pg->type;
    p->playback = pg->playback;
    p->percent = percent;
    p->fw = cg_edge(CG_FRAME_WIDTH, percent);
    p->fh = cg_edge(CG_FRAME_HEIGHT, percent);
    p->back = malloc((size_t)p->fw * p->fh * 4);
    if (!p->back)
    {
        free(p);
        return NULL;
    }
    /* a moving page's background alone, without its lines; a still page whole */
    strip_book = malloc(sizeof(*strip_book));
    if (strip_book)
    {
        memcpy(strip_book, book, sizeof(*strip_book));
        if (p->type == CG_PAGE_SCROLL || p->type == CG_PAGE_CRAWL)
            strip_book->pages[page].lines = NULL;
        cg_render_page(r, strip_book, page, p->back, p->fw, p->fh, scale);
        free(strip_book);
    }
    /* fields go out over black: the background once, here, so a field
       is the strip straight over it */
    cg_render_flatten(p->back, p->fw, p->fh);
    if (p->type == CG_PAGE_SCROLL && content_extent(pg, &top, &bottom))
    {
        p->sw = p->fw;
        p->sh = cg_edge(bottom - top, percent);
        p->speed = cg_play_scroll_speed(pg->speed);
        p->strip = calloc((size_t)p->sw * p->sh, 4);
        p->top = cg_edge(top, percent);
        /* the content starts just under the frame and leaves over the top */
        p->pos = -p->fh;
        p->end = p->sh;
    }
    else if (p->type == CG_PAGE_CRAWL && pg->lines)
    {
        struct cg_line l;
        double *xs = NULL;

        /* the crawl's whole text, from all its lines */
        if (!cg_crawl_merge(pg, &l) || !(xs = cg_render_line_xs(r, book, &l)))
        {
            cg_crawl_free(&l);
            return p;
        }
        p->sw = cg_edge((int)(xs[l.nchars] + 16), percent);
        free(xs);
        cg_crawl_free(&l);
        p->sh = cg_edge(pg->lines->total_height, percent);
        p->top = cg_edge(pg->lines->yoffset, percent);
        p->speed = cg_play_crawl_speed(pg->speed);
        p->strip = calloc((size_t)p->sw * p->sh, 4);
        p->pos = -p->fw;
        p->end = p->sw;
    }
    p->pos200 = (long)p->pos * 200;
    if (p->strip)
    {
        /* render the lines alone over a clear strip, then keep their alpha */
        struct cg_book *b = malloc(sizeof(*b));

        if (b)
        {
            memcpy(b, book, sizeof(*b));
            b->pages[page].background = CG_BACKGROUND_SOLID;
            memset(&b->pages[page].top, 0, sizeof(b->pages[page].top));
            memset(&b->pages[page].bottom, 0, sizeof(b->pages[page].bottom));
            if (p->type == CG_PAGE_SCROLL)
                cg_render_page_view(r, b, page, p->strip, p->sw, p->sh, scale, 0, p->top);
            else
                cg_render_page_view(r, b, page, p->strip, p->sw, p->sh, scale,
                                    cg_edge(pg->lines->xoffset, percent), p->top);
            free(b);
        }
    }
    return p;
}

const uint8_t *cg_play_strip(const struct cg_play *p, int *w, int *h, int *top)
{
    *w = p->sw;
    *h = p->sh;
    *top = p->type == CG_PAGE_CRAWL ? p->top : 0;
    return p->strip;
}

long cg_play_fields(const struct cg_play *p)
{
    long dist, step = (long)p->speed * p->percent;

    if (!p->strip || step <= 0)
        return 0;
    if (p->type == CG_PAGE_SCROLL)
        dist = p->playback == 1 ? p->sh - cg_edge(CG_SAFE_BOTTOM, p->percent) + p->fh : p->sh + p->fh;
    else
        dist = p->sw + p->fw;
    return dist <= 0 ? 0 : (dist * 200 + step - 1) / step;
}

void cg_play_free(struct cg_play *p)
{
    if (!p)
        return;
    free(p->strip);
    free(p->back);
    free(p);
}

/* v / 255, rounded, for v up to 255 * 255 */
static inline unsigned div255(unsigned v)
{
    v += 128;
    return (v + (v >> 8)) >> 8;
}

/* n pixels of the strip s straight over the opaque background b, into d */
static void over_row(uint8_t *d, const uint8_t *b, const uint8_t *s, int n)
{
    int i;

    for (i = 0; i < n; i++, d += 4, b += 4, s += 4)
    {
        unsigned a = s[3];

        if (!a)
            memcpy(d, b, 4);
        else if (a == 255)
        {
            memcpy(d, s, 3);
            d[3] = 255;
        }
        else
        {
            d[0] = (uint8_t)div255(s[0] * a + b[0] * (255 - a));
            d[1] = (uint8_t)div255(s[1] * a + b[1] * (255 - a));
            d[2] = (uint8_t)div255(s[2] * a + b[2] * (255 - a));
            d[3] = 255;
        }
    }
}

/* the position one field on; 0 once the page is over */
static int step(struct cg_play *p)
{
    if (p->done)
        return 0;
    if (p->held)
        return 1;
    p->pos200 += (long)p->speed * p->percent;
    /* floor, not towards zero: the strip starts off the frame, at a
       negative position, and must step evenly through 0 */
    p->pos = (int)(p->pos200 >= 0 ? p->pos200 / 200 : -((-p->pos200 + 199) / 200));
    /* a frozen scroll stops with its last line on the safe bottom */
    if (p->type == CG_PAGE_SCROLL && p->playback == 1 && p->pos >= p->sh - cg_edge(CG_SAFE_BOTTOM, p->percent))
    {
        p->pos = p->sh - cg_edge(CG_SAFE_BOTTOM, p->percent);
        p->held = 1;
    }
    else if (p->pos >= p->end)
    {
        if (p->playback < 0)
        {
            p->pos = p->type == CG_PAGE_SCROLL ? -p->fh : -p->fw;
            p->pos200 = (long)p->pos * 200;
        }
        else
            p->done = 1;
    }
    return 1;
}

/* one pass: each row is the background, or the strip over it where the
   strip covers the frame */
int cg_play_field(struct cg_play *p, uint8_t *rgba, int w, int h)
{
    size_t row = (size_t)w * 4;
    int y;

    if (w != p->fw || h != p->fh)
        return 0;
    if (!p->strip)
    {
        memcpy(rgba, p->back, row * h);
        return 1;
    }
    for (y = 0; y < h; y++)
    {
        uint8_t *d = rgba + row * y;
        const uint8_t *b = p->back + row * y;

        if (p->type == CG_PAGE_SCROLL)
        {
            int sy = y + p->pos;

            if (sy < 0 || sy >= p->sh)
                memcpy(d, b, row);
            else
                over_row(d, b, p->strip + (size_t)sy * p->sw * 4, w);
        }
        else
        {
            int sy = y - p->top, x0 = -p->pos, x1 = p->sw - p->pos;

            /* the crawl's line: its strip runs from x0 to x1 in the frame */
            if (x0 < 0)
                x0 = 0;
            if (x1 > w)
                x1 = w;
            if (sy < 0 || sy >= p->sh || x0 >= x1)
                memcpy(d, b, row);
            else
            {
                memcpy(d, b, (size_t)x0 * 4);
                over_row(d + (size_t)x0 * 4, b + (size_t)x0 * 4,
                         p->strip + ((size_t)sy * p->sw + x0 + p->pos) * 4, x1 - x0);
                memcpy(d + (size_t)x1 * 4, b + (size_t)x1 * 4, (size_t)(w - x1) * 4);
            }
        }
    }
    return step(p);
}

int cg_play_skip(struct cg_play *p, int fields)
{
    while (fields-- > 0)
        if (!step(p))
            return 0;
    return !p->done;
}
