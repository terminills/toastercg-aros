/*
    Pen drawing for the interface: NewTek's gadget imagery, the
    NewBorderBox() bevels and the CGInterface/14 font.
*/

#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "cgrender.h"
#include "vecart.h"

static int use_vector = 1;

void pc_set_vector(int on)
{
    use_vector = on;
}

int pc_init(struct pcanvas *c, int w, int h)
{
    memset(c, 0, sizeof(*c));
    c->w = w;
    c->h = h;
    c->vector = use_vector;
    c->px = malloc((size_t)w * h);
    c->covered = calloc((size_t)w, (size_t)h);
    c->over = calloc((size_t)w, (size_t)h);
    if (!c->px || !c->covered || !c->over)
    {
        pc_free(c);
        return 0;
    }
    memset(c->px, PEN_CLEAR, (size_t)w * h);
    return 1;
}

void pc_free(struct pcanvas *c)
{
    free(c->px);
    free(c->covered);
    free(c->over);
    free(c->vec);
    c->px = c->covered = c->over = NULL;
    c->vec = NULL;
    c->nvec = c->capvec = 0;
}

void pc_clear(struct pcanvas *c, int pen)
{
    memset(c->px, pen, (size_t)c->w * c->h);
    memset(c->covered, 0, (size_t)c->w * c->h);
    memset(c->over, 0, (size_t)c->w * c->h);
    c->nvec = 0;
    c->vector = use_vector;
}

static void record(struct pcanvas *c, const struct cg_art_image *img, int ch, int x, int y)
{
    if (c->nvec == c->capvec)
    {
        struct pc_vec *v = realloc(c->vec, sizeof(*v) * (size_t)(c->capvec ? c->capvec * 2 : 64));

        if (!v)
            return;
        c->vec = v;
        c->capvec = c->capvec ? c->capvec * 2 : 64;
    }
    c->vec[c->nvec].img = img;
    c->vec[c->nvec].ch = ch;
    c->vec[c->nvec].x = x;
    c->vec[c->nvec].y = y;
    c->nvec++;
}

/* a pen pixel written: over a redraw already recorded, it goes on top */
static void set_px(struct pcanvas *c, int x, int y, int pen)
{
    size_t i = (size_t)y * c->w + x;

    c->px[i] = (uint8_t)pen;
    if (c->covered[i])
        c->over[i] = 1;
}

void pc_fill(struct pcanvas *c, int x0, int y0, int x1, int y1, int pen)
{
    int x, y;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= c->w)
        x1 = c->w - 1;
    if (y1 >= c->h)
        y1 = c->h - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            set_px(c, x, y, pen);
}

void pc_image(struct pcanvas *c, const struct cg_art_image *img, int x, int y)
{
    int i, j;

    if (!img || !img->px)
        return;
    if (c->vector && vec_has_image(img))
    {
        /* its own pixels stay underneath; the redraw covers them */
        record(c, img, 0, x, y);
        x += img->x;
        y += img->y;
        for (j = 0; j < img->h; j++)
            for (i = 0; i < img->w; i++)
                if (x + i >= 0 && y + j >= 0 && x + i < c->w && y + j < c->h)
                {
                    size_t k = (size_t)(y + j) * c->w + x + i;

                    c->px[k] = img->px[j * img->w + i] & 7;
                    c->covered[k] = 1;
                    c->over[k] = 0;
                }
        return;
    }
    x += img->x;
    y += img->y;
    for (j = 0; j < img->h; j++)
        for (i = 0; i < img->w; i++)
            if (x + i >= 0 && y + j >= 0 && x + i < c->w && y + j < c->h)
                set_px(c, x + i, y + j, img->px[j * img->w + i] & 7);
}

/* raised (STANDARD) or recessed (REV) two pixel bevel */
void pc_box(struct pcanvas *c, int x0, int y0, int x1, int y1, int style)
{
    int light = style == BOX_STANDARD ? PEN_GREY : PEN_BLACK;
    int dark = style == BOX_STANDARD ? PEN_BLACK : PEN_GREY;

    pc_fill(c, x0, y0, x1, y0, light);
    pc_fill(c, x0, y0, x0, y1, light);
    pc_fill(c, x0 + 1, y1, x1, y1, dark);
    pc_fill(c, x1, y0 + 1, x1, y1, dark);
}

static const struct cg_art_char *glyph(unsigned char ch)
{
    const struct cg_art_font *f = &cg_art_font;

    if (ch < f->lo || ch > f->hi)
        return &f->chars[f->hi - f->lo + 1];
    return &f->chars[ch - f->lo];
}

int pc_text_width(const char *s)
{
    int w = 0;

    for (; *s; s++)
        w += glyph((unsigned char)*s)->space;
    return w;
}

int pc_text(struct pcanvas *c, const char *s, int x, int y, int maxw)
{
    const struct cg_art_font *f = &cg_art_font;
    int x0 = x, top = y - f->baseline, i, j;

    for (; *s; s++)
    {
        const struct cg_art_char *g = glyph((unsigned char)*s);

        if (maxw && x + g->space - x0 > maxw)
            break;
        if (c->vector)
        {
            /* the character from the font's redraw, in order with the rest */
            record(c, NULL, (unsigned char)*s, x + g->kern, top);
            x += g->space;
            continue;
        }
        for (j = 0; j < f->height; j++)
            for (i = 0; i < g->width; i++)
            {
                int px = x + g->kern + i, py = top + j;
                uint8_t pen = f->strip[j * f->stripw + g->loc + i];

                /* the font's own background pen (2) shows the bar through */
                if (pen != 2 && px >= 0 && py >= 0 && px < c->w && py < c->h)
                    set_px(c, px, py, pen);
            }
        x += g->space;
    }
    return x - x0;
}

void pc_blit(const struct pcanvas *c, uint8_t *rgba, int w, int h, int x0, int y0, int percent)
{
    int *xe = malloc(sizeof(*xe) * (size_t)(c->w + 1)), x, y, i, j;

    if (!xe)
        return;
    for (x = 0; x <= c->w; x++)
        xe[x] = cg_edge(x0 + x, percent);
    for (y = 0; y < c->h; y++)
    {
        int top = cg_edge(y0 + y, percent), bottom = cg_edge(y0 + y + 1, percent);

        if (top < 0)
            top = 0;
        if (bottom > h)
            bottom = h;
        for (x = 0; x < c->w; x++)
        {
            uint8_t pen = c->px[y * c->w + x];
            uint32_t col;
            int left = xe[x] < 0 ? 0 : xe[x], right = xe[x + 1] > w ? w : xe[x + 1];

            if (pen == PEN_CLEAR)
                continue;
            col = cg_art_palette[pen & 7];
            for (j = top; j < bottom; j++)
                for (i = left; i < right; i++)
                {
                    uint8_t *d = rgba + ((size_t)j * w + i) * 4;

                    d[0] = (uint8_t)(col >> 16);
                    d[1] = (uint8_t)(col >> 8);
                    d[2] = (uint8_t)col;
                    d[3] = 255;
                }
        }
    }
    /* the redraws in their order, then the pen pixels drawn over them */
    for (i = 0; i < c->nvec; i++)
    {
        const struct pc_vec *v = &c->vec[i];

        if (v->img)
            vec_image(rgba, w, h, v->img, x0 + v->x, y0 + v->y, percent);
        else
            vec_char(rgba, w, h, (unsigned char)v->ch, x0 + v->x, y0 + v->y, percent, cg_art_palette[PEN_BLACK]);
    }
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++)
        {
            uint8_t pen = c->px[y * c->w + x];
            uint32_t col;

            if (!c->over[y * c->w + x] || pen == PEN_CLEAR)
                continue;
            col = cg_art_palette[pen & 7];
            for (j = cg_edge(y0 + y, percent); j < cg_edge(y0 + y + 1, percent) && j < h; j++)
                for (i = xe[x] < 0 ? 0 : xe[x]; i < xe[x + 1] && i < w; i++)
                {
                    uint8_t *d;

                    if (j < 0)
                        continue;
                    d = rgba + ((size_t)j * w + i) * 4;
                    d[0] = (uint8_t)(col >> 16);
                    d[1] = (uint8_t)(col >> 8);
                    d[2] = (uint8_t)col;
                    d[3] = 255;
                }
        }
    free(xe);
}
