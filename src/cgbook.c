/*
    ToasterCG book loader: the version 4 format of NewTek's
    LoadNewBook()/SaveBook() (original/src/NewBook.c), read byte by byte
    in big-endian order so it works on any host.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgbook.h"

#define ID(a, b, c, d)  ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define ID_FONT         ID('F', 'O', 'N', 'T')
#define ID_BRSH         ID('B', 'R', 'S', 'H')
#define ID_DRAW         ID('D', 'R', 'A', 'W')
#define ID_PAGE         ID('P', 'A', 'G', 'E')
#define ID_LINE         ID('L', 'I', 'N', 'E')
#define ID_END          ID('E', 'N', 'D', '!')

#define BOOK_HEAD       "ToasterBookFile"
#define BOOK_HEAD_SIZE  16
#define ATTR_SIZE       32
#define LINE_SIZE       18
#define PAGE_SIZE       12

struct reader
{
    const uint8_t *p, *end;
    int            bad;
};

static const uint8_t *take(struct reader *r, size_t n)
{
    const uint8_t *q = r->p;

    if (r->bad || (size_t)(r->end - r->p) < n)
    {
        r->bad = 1;
        return NULL;
    }
    r->p += n;
    return q;
}

static uint8_t get8(struct reader *r)
{
    const uint8_t *q = take(r, 1);
    return q ? q[0] : 0;
}

static uint16_t get16(struct reader *r)
{
    const uint8_t *q = take(r, 2);
    return q ? (uint16_t)(q[0] << 8 | q[1]) : 0;
}

static uint32_t get32(struct reader *r)
{
    const uint8_t *q = take(r, 4);
    return q ? (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3] : 0;
}

static void get_rgba(struct reader *r, struct cg_rgba *c)
{
    c->r = get8(r);
    c->g = get8(r);
    c->b = get8(r);
    c->a = get8(r);
}

static void read_attr(struct reader *r, struct cg_attr *a)
{
    get_rgba(r, &a->face);
    get_rgba(r, &a->shadow);
    get_rgba(r, &a->outline);
    get_rgba(r, &a->face_grad);
    get_rgba(r, &a->outline_grad);
    a->id = (int16_t)get16(r);
    a->raw0 = get8(r);
    a->shadow_length = get8(r);
    a->shadow_type = get8(r);
    a->outline_type = get8(r);
    a->shadow_direction = get8(r);
    a->shadow_priority = get8(r);
    a->raw1[0] = get8(r);
    a->raw1[1] = get8(r);
    a->raw1[2] = get8(r);
    a->special_fill = get8(r);
}

static int read_data(struct reader *r, struct cg_book *book, uint32_t type)
{
    struct cg_data *d, **tail;
    const uint8_t *name, *draw;
    uint16_t len;

    if (!(d = calloc(1, sizeof(*d))))
        return 0;
    d->type = (type == ID_FONT) ? CG_DATA_FONT : (type == ID_DRAW) ? CG_DATA_DRAW : CG_DATA_BRUSH;
    d->id = get16(r);
    len = get16(r);
    if (type != ID_BRSH)
        d->height = get16(r);
    if (type == ID_FONT)
        d->flags = get32(r);
    if (type == ID_DRAW)
    {
        if (!(draw = take(r, CG_DRAWDATA_SIZE)) || !(d->drawdata = malloc(CG_DRAWDATA_SIZE)))
        {
            free(d);
            return 0;
        }
        memcpy(d->drawdata, draw, CG_DRAWDATA_SIZE);
    }
    name = take(r, len);
    if (!name || !(d->filename = malloc(len + 1)))
    {
        free(d->drawdata);
        free(d);
        return 0;
    }
    memcpy(d->filename, name, len);
    d->filename[len] = 0;

    for (tail = &book->data; *tail; tail = &(*tail)->next)
        ;
    *tail = d;
    return 1;
}

static int read_line(struct reader *r, struct cg_page *page)
{
    struct cg_line *line, **tail;
    uint16_t n, i;

    n = get16(r);
    if (r->bad || n > CG_MAX_LINE_CHARS)
        return 0;
    if (!(line = calloc(1, sizeof(*line))))
        return 0;
    line->nchars = n;
    if (n && !(line->chars = calloc(n, sizeof(*line->chars))))
    {
        free(line);
        return 0;
    }
    for (i = 0; i < n; i++)
    {
        struct cg_char *c = &line->chars[i];

        c->ascii = get16(r);
        c->kerning = (int8_t)get8(r);
        if (get8(r))
        {
            if (!(c->attr = calloc(1, sizeof(*c->attr))))
                r->bad = 1;
            else
                read_attr(r, c->attr);
        }
    }
    line->total_height = get16(r);
    line->baseline = get16(r);
    line->xoffset = (int16_t)get16(r);
    line->yoffset = (int16_t)get16(r);
    line->raw_flags = get8(r);
    line->justify = get8(r);
    line->face_width = get16(r);
    line->face_height = get16(r);
    line->raw[0] = get8(r);
    line->raw[1] = get8(r);
    line->render_pri = get8(r);
    line->raw3 = get8(r);

    for (tail = &page->lines; *tail; tail = &(*tail)->next)
        ;
    *tail = line;
    page->nlines++;
    return !r->bad;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc(n)) && fread(buf, 1, n, f) != (size_t)n)
    {
        free(buf);
        buf = NULL;
    }
    else if (buf)
        *len = (size_t)n;
    fclose(f);
    return buf;
}

/* a page file (an IFF crouton) carries a one page book in its CrUD tags */
static const uint8_t *find_book(const uint8_t *buf, size_t *len)
{
    size_t i;

    for (i = 0; i + BOOK_HEAD_SIZE <= *len; i++)
        if (!memcmp(buf + i, BOOK_HEAD, BOOK_HEAD_SIZE - 1))
        {
            *len -= i;
            return buf + i;
        }
    return NULL;
}

struct cg_book *cg_book_parse(const uint8_t *buf, size_t len, const char **err)
{
    struct cg_book *book = NULL;
    struct cg_page *page = NULL;
    struct reader r;
    uint32_t type;
    const char *why = "not a ToasterCG book";

    if (len >= 4 && !memcmp(buf, "FORM", 4) && !(buf = find_book(buf, &len)))
        goto fail;
    if (len < BOOK_HEAD_SIZE || memcmp(buf, BOOK_HEAD, BOOK_HEAD_SIZE - 1))
        goto fail;
    if (buf[BOOK_HEAD_SIZE - 1] != '4')
    {
        why = "only version 4 books are supported";
        goto fail;
    }
    if (!(book = calloc(1, sizeof(*book))))
    {
        why = "out of memory";
        goto fail;
    }
    book->version = buf[BOOK_HEAD_SIZE - 1];

    r.p = buf + BOOK_HEAD_SIZE;
    r.end = buf + len;
    r.bad = 0;
    why = "damaged book";
    do
    {
        type = get32(&r);
        if (r.bad)
            goto fail;
        switch (type)
        {
        case ID_FONT:
        case ID_BRSH:
        case ID_DRAW:
            if (!read_data(&r, book, type))
                goto fail;
            break;
        case ID_PAGE:
        {
            uint16_t num = get16(&r);

            if (num >= CG_PAGES_PER_BOOK)
                goto fail;
            page = &book->pages[num];
            page->used = 1;
            page->type = get8(&r);
            page->speed = get8(&r);
            page->playback = (int8_t)get8(&r);
            page->background = get8(&r);
            get_rgba(&r, &page->top);
            get_rgba(&r, &page->bottom);
            break;
        }
        case ID_LINE:
            if (!page || !read_line(&r, page))
                goto fail;
            break;
        case ID_END:
            break;
        default:
            goto fail;
        }
    } while (type != ID_END);

    return book;

fail:
    cg_book_free(book);
    if (err)
        *err = why;
    return NULL;
}

struct cg_book *cg_book_load(const char *path, const char **err)
{
    struct cg_book *book;
    uint8_t *buf;
    size_t len = 0;

    if (!(buf = slurp(path, &len)))
    {
        if (err)
            *err = "cannot read file";
        return NULL;
    }
    book = cg_book_parse(buf, len, err);
    free(buf);
    return book;
}

void cg_book_free(struct cg_book *book)
{
    struct cg_data *d, *dn;
    int i;

    if (!book)
        return;
    for (d = book->data; d; d = dn)
    {
        dn = d->next;
        free(d->filename);
        free(d->drawdata);
        free(d);
    }
    for (i = 0; i < CG_PAGES_PER_BOOK; i++)
    {
        struct cg_line *l, *ln;

        for (l = book->pages[i].lines; l; l = ln)
        {
            uint16_t c;

            ln = l->next;
            for (c = 0; c < l->nchars; c++)
                free(l->chars[c].attr);
            free(l->chars);
            free(l);
        }
    }
    free(book);
}

const struct cg_data *cg_book_find_data(const struct cg_book *book, uint16_t id)
{
    const struct cg_data *d;

    for (d = book->data; d; d = d->next)
        if (d->id == id)
            return d;
    return NULL;
}

int cg_crawl_merge(const struct cg_page *p, struct cg_line *out)
{
    const struct cg_line *l;
    size_t n = 0;
    int i;

    memset(out, 0, sizeof(*out));
    if (!p->lines)
        return 0;
    for (l = p->lines; l; l = l->next)
        for (i = 0; i < l->nchars && i < CG_CRAWL_CHUNK && l->chars[i].ascii; i++)
            n++;
    *out = *p->lines;
    out->next = NULL;
    out->nchars = 0;
    if (!(out->chars = calloc(n ? n : 1, sizeof(*out->chars))))
        return 0;
    for (l = p->lines; l; l = l->next)
        for (i = 0; i < l->nchars && i < CG_CRAWL_CHUNK && l->chars[i].ascii; i++)
        {
            out->chars[out->nchars] = l->chars[i];
            if (out->nchars)
                out->chars[out->nchars].attr = NULL;
            out->nchars++;
        }
    /* the whole crawl wears the first character's attributes */
    if (p->lines->nchars)
        out->chars[0].attr = p->lines->chars[0].attr;
    if (!out->nchars)
    {
        out->chars[0] = p->lines->nchars ? p->lines->chars[0] : out->chars[0];
        out->nchars = p->lines->nchars ? 1 : 0;
    }
    return 1;
}

void cg_crawl_free(struct cg_line *merged)
{
    free(merged->chars);
    merged->chars = NULL;
    merged->nchars = 0;
}

int cg_line_is_graphic(const struct cg_book *book, const struct cg_line *l)
{
    const struct cg_attr *a = l && l->nchars ? l->chars[0].attr : NULL;
    const struct cg_data *d;

    if (!a)
        return 0;
    if (a->id == -1)
        return 1;
    d = cg_book_find_data(book, (uint16_t)a->id);
    return d && d->type != CG_DATA_FONT;
}
