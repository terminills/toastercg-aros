/*
    playcheck: a scroll's fields against the page itself. Field k of a
    scroll shows the page at view offset top + pos(k), over black, so
    every field must be what cg_render_page_view() draws there,
    flattened; rounding may differ by one.

    usage: playcheck fontroot book page percent

    Checks every 150th of the first 900 fields; exits 0 when none is off
    by more than one, and a second player that reaches each of them by
    cg_play_skip() alone shows exactly the same field.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgplay.h"

static long floordiv(long a, long b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int main(int argc, char **argv)
{
    const char *err;
    struct cg_book *b;
    struct cg_renderer *r;
    struct cg_play *p, *q;
    struct cg_line *l;
    uint8_t *f, *v, *g;
    int page, pc, w, h, top = 32767, speed, k, worst = 0;

    if (argc != 5 || !(b = cg_book_load(argv[2], &err)) || !(r = cg_render_new(argv[1])))
    {
        fprintf(stderr, "usage: playcheck fontroot book page percent\n");
        return 20;
    }
    page = atoi(argv[3]);
    pc = atoi(argv[4]);
    w = cg_edge(CG_FRAME_WIDTH, pc);
    h = cg_edge(CG_FRAME_HEIGHT, pc);
    for (l = b->pages[page].lines; l; l = l->next)
        if (l->yoffset < top)
            top = l->yoffset;
    speed = cg_play_scroll_speed(b->pages[page].speed);
    p = cg_play_new(r, b, page, pc);
    q = cg_play_new(r, b, page, pc);
    f = malloc((size_t)w * h * 4);
    v = malloc((size_t)w * h * 4);
    g = malloc((size_t)w * h * 4);
    if (b->pages[page].type != CG_PAGE_SCROLL || !p || !q || !f || !v || !g)
        return 20;
    for (k = 0; k < 900; k++)
    {
        long pos = floordiv(-(long)h * 200 + (long)k * speed * pc, 200);
        size_t i;
        int m = 0;

        cg_play_field(p, f, w, h);
        if (k % 150)
            continue;
        memset(v, 0, (size_t)w * h * 4);
        cg_render_page_view(r, b, page, v, w, h, pc / 100.0, 0, cg_edge(top, pc) + (int)pos);
        cg_render_flatten(v, w, h);
        for (i = 0; i < (size_t)w * h * 4; i++)
            if (abs(f[i] - v[i]) > m)
                m = abs(f[i] - v[i]);
        /* q skips to field k, draws it, and is then at k + 1 */
        cg_play_skip(q, k ? 149 : 0);
        cg_play_field(q, g, w, h);
        if (memcmp(f, g, (size_t)w * h * 4))
        {
            printf("%d%% field %d: skipping to it shows a different field\n", pc, k);
            worst = 99;
        }
        printf("%d%% field %d (pos %ld): off by at most %d\n", pc, k, pos, m);
        if (m > worst)
            worst = m;
    }
    free(f);
    free(v);
    free(g);
    cg_play_free(p);
    cg_play_free(q);
    cg_render_free(r);
    cg_book_free(b);
    return worst > 1;
}
