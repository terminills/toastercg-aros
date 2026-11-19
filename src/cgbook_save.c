/*
    ToasterCG book writer: SaveBook() of original/src/NewBook.c in the
    version 4 format, written byte by byte in big-endian order.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgbook.h"

#define BOOK_HEAD       "ToasterBookFile"

struct writer
{
    FILE *f;
    int   bad;
};

static void put(struct writer *w, const void *p, size_t n)
{
    if (!w->bad && n && fwrite(p, 1, n, w->f) != n)
        w->bad = 1;
}

static void put8(struct writer *w, uint8_t v)
{
    put(w, &v, 1);
}

static void put16(struct writer *w, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    put(w, b, 2);
}

static void put32(struct writer *w, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put(w, b, 4);
}

static void put_id(struct writer *w, const char *id)
{
    put(w, id, 4);
}

static void put_rgba(struct writer *w, const struct cg_rgba *c)
{
    put8(w, c->r);
    put8(w, c->g);
    put8(w, c->b);
    put8(w, c->a);
}

static void write_attr(struct writer *w, const struct cg_attr *a)
{
    put_rgba(w, &a->face);
    put_rgba(w, &a->shadow);
    put_rgba(w, &a->outline);
    put_rgba(w, &a->face_grad);
    put_rgba(w, &a->outline_grad);
    put16(w, (uint16_t)a->id);
    put8(w, a->raw0);
    put8(w, a->shadow_length);
    put8(w, a->shadow_type);
    put8(w, a->outline_type);
    put8(w, a->shadow_direction);
    put8(w, a->shadow_priority);
    put(w, a->raw1, 3);
    put8(w, a->special_fill);
}

static void write_data(struct writer *w, const struct cg_data *d)
{
    static const uint8_t zero[CG_DRAWDATA_SIZE];
    size_t len = d->filename ? strlen(d->filename) : 0;

    /* SaveBook() skips entries without a name */
    if (!len || len > 0xffff)
        return;
    put_id(w, d->type == CG_DATA_FONT ? "FONT" : d->type == CG_DATA_DRAW ? "DRAW" : "BRSH");
    put16(w, d->id);
    put16(w, (uint16_t)len);
    if (d->type != CG_DATA_BRUSH)
        put16(w, d->height);
    if (d->type == CG_DATA_FONT)
        put32(w, d->flags);
    if (d->type == CG_DATA_DRAW)
        put(w, d->drawdata ? d->drawdata : zero, CG_DRAWDATA_SIZE);
    put(w, d->filename, len);
}

static void write_line(struct writer *w, const struct cg_line *l)
{
    static const struct cg_char empty;
    uint16_t n = l->nchars ? l->nchars : 1, i;

    /* an empty line still carries its first character's attributes */
    put_id(w, "LINE");
    put16(w, n);
    for (i = 0; i < n; i++)
    {
        const struct cg_char *c = l->nchars ? &l->chars[i] : &empty;

        put16(w, c->ascii);
        put8(w, (uint8_t)c->kerning);
        put8(w, c->attr != NULL);
        if (c->attr)
            write_attr(w, c->attr);
    }
    put16(w, l->total_height);
    put16(w, l->baseline);
    put16(w, (uint16_t)l->xoffset);
    put16(w, (uint16_t)l->yoffset);
    put8(w, l->raw_flags);
    put8(w, l->justify);
    put16(w, l->face_width);
    put16(w, l->face_height);
    put(w, l->raw, 2);
    put8(w, l->render_pri);
    put8(w, l->raw3);
}

/* SaveBook() writes typed pages with more than one line or a first character */
static int page_worth_saving(const struct cg_page *p)
{
    if (!p->used || !p->type || !p->lines)
        return 0;
    return p->lines->next || (p->lines->nchars && p->lines->chars[0].ascii);
}

static int data_in_page(const struct cg_page *p, uint16_t id)
{
    const struct cg_line *l;
    uint16_t i;

    for (l = p->lines; l; l = l->next)
        for (i = 0; i < l->nchars; i++)
            if (l->chars[i].attr && (uint16_t)l->chars[i].attr->id == id)
                return 1;
    return 0;
}

int cg_book_write(const struct cg_book *book, FILE *f, int only)
{
    struct writer w = { f, 0 };
    const struct cg_data *d;
    const struct cg_line *l;
    int i;

    put(&w, BOOK_HEAD, strlen(BOOK_HEAD));
    put8(&w, '4');
    for (d = book->data; d; d = d->next)
        if (only < 0 || data_in_page(&book->pages[only], d->id))
            write_data(&w, d);
    for (i = 0; i < CG_PAGES_PER_BOOK; i++)
    {
        const struct cg_page *p = &book->pages[i];

        if ((only >= 0 && i != only) || !page_worth_saving(p))
            continue;
        put_id(&w, "PAGE");
        put16(&w, (uint16_t)i);
        put8(&w, p->type);
        put8(&w, p->speed);
        put8(&w, (uint8_t)p->playback);
        put8(&w, p->background);
        put_rgba(&w, &p->top);
        put_rgba(&w, &p->bottom);
        for (l = p->lines; l; l = l->next)
            write_line(&w, l);
    }
    put_id(&w, "END!");
    return !w.bad;
}

int cg_book_save(const struct cg_book *book, const char *path, const char **err)
{
    FILE *f;
    int ok;

    if (!(f = fopen(path, "wb")))
    {
        if (err)
            *err = "cannot create file";
        return 0;
    }
    ok = cg_book_write(book, f, -1);
    if (fclose(f) || !ok)
    {
        if (err)
            *err = "cannot write file";
        return 0;
    }
    return 1;
}
