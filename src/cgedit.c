/*
    The CG's editing operations on a book, reconstructed from what the
    released code calls them for (NewFunction.c, cgrexx.c, NewBook.c)
    and the behaviour CGBetaChanges describes. The editor itself lived in
    edita/editb/editc.a, which are not in the release.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cgedit.h"

static const int shadow_dy[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };

static const char msg_change_type[] = "Change page type to add text.";
static const char msg_no_boxes[] = "Boxes not allowed on scroll and crawl pages.";
static const char msg_no_brushes[] = "Brushes not allowed on scroll and crawl pages.";
static const char msg_no_colorfonts[] = "Toaster ColorFonts not allowed on scroll and crawl pages.";
static const char msg_crawl_height[] = "Only fonts 300 lines and under allowed on crawl pages.";
static const char msg_crawl_trim[] = "Maximum crawl height is 300 lines.\nShadow and outline trimmed.";
static const char msg_line_full[] = "Unable to append line to page.";

static int moving(int type)
{
    return type == CG_PAGE_SCROLL || type == CG_PAGE_CRAWL;
}

struct cg_page *cg_edit_page(struct cg_editor *e)
{
    return &e->book->pages[e->page];
}

int cg_line_length(const struct cg_line *l)
{
    if (!l || (l->nchars == 1 && !l->chars[0].ascii))
        return 0;
    return l->nchars;
}

const struct cg_attr *cg_char_attr(const struct cg_line *l, int i)
{
    if (!l || !l->nchars)
        return NULL;
    if (i >= l->nchars)
        i = l->nchars - 1;
    for (; i > 0 && !l->chars[i].attr; i--)
        ;
    return l->chars[i].attr;
}

static struct cg_attr *attr_copy(const struct cg_attr *a)
{
    struct cg_attr *n = malloc(sizeof(*n));

    if (n)
        *n = *a;
    return n;
}

static int attr_same(const struct cg_attr *a, const struct cg_attr *b)
{
    return a && b && !memcmp(a, b, sizeof(*a));
}

/* every character after the first carries an attribute only where it changes */
static void normalize(struct cg_line *l)
{
    const struct cg_attr *prev;
    int i;

    if (!l->nchars)
        return;
    prev = l->chars[0].attr;
    for (i = 1; i < l->nchars; i++)
    {
        if (!l->chars[i].attr)
            continue;
        if (attr_same(l->chars[i].attr, prev))
        {
            free(l->chars[i].attr);
            l->chars[i].attr = NULL;
        }
        else
            prev = l->chars[i].attr;
    }
}

/* give character i its own attribute, without changing what follows */
static struct cg_attr *own_attr(struct cg_line *l, int i)
{
    const struct cg_attr *eff;

    if (l->chars[i].attr)
        return l->chars[i].attr;
    eff = cg_char_attr(l, i);
    if (i + 1 < l->nchars && !l->chars[i + 1].attr && eff)
        l->chars[i + 1].attr = attr_copy(eff);
    l->chars[i].attr = eff ? attr_copy(eff) : NULL;
    return l->chars[i].attr;
}

static void free_line(struct cg_line *l)
{
    int i;

    for (i = 0; i < l->nchars; i++)
        free(l->chars[i].attr);
    free(l->chars);
    free(l);
}

static void unlink_line(struct cg_page *p, struct cg_line *l)
{
    struct cg_line **pp;

    for (pp = &p->lines; *pp; pp = &(*pp)->next)
        if (*pp == l)
        {
            *pp = l->next;
            p->nlines--;
            return;
        }
}

static void append_line(struct cg_page *p, struct cg_line *l)
{
    struct cg_line **pp;

    for (pp = &p->lines; *pp; pp = &(*pp)->next)
        ;
    l->next = NULL;
    *pp = l;
    p->nlines++;
}

static void clear_page(struct cg_page *p)
{
    struct cg_line *l, *n;

    for (l = p->lines; l; l = n)
    {
        n = l->next;
        free_line(l);
    }
    p->lines = NULL;
    p->nlines = 0;
}

static int crawling(struct cg_editor *e)
{
    return cg_edit_page(e)->type == CG_PAGE_CRAWL;
}

const struct cg_line *cg_edit_cursor_line(struct cg_editor *e, struct cg_line *tmp)
{
    memset(tmp, 0, sizeof(*tmp));
    if (crawling(e) && e->line)
        return cg_crawl_merge(cg_edit_page(e), tmp) ? tmp : NULL;
    return e->line;
}

void cg_edit_release(struct cg_line *tmp)
{
    if (tmp->chars)
        cg_crawl_free(tmp);
}

int cg_edit_length(struct cg_editor *e)
{
    struct cg_line t;
    int n = cg_line_length(cg_edit_cursor_line(e, &t));

    cg_edit_release(&t);
    return n;
}

/* a crawl's text back into its lines, CG_CRAWL_CHUNK to a line: the
   first keeps its place and attributes, the rest go where NewTek's CG
   left them, off the frame at 56,16717 */
static void crawl_put(struct cg_editor *e, const struct cg_char *chars, int n)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *first = p->lines, *l, *next, **tail;
    struct cg_attr *attr;
    int i, k, len;

    if (!first)
        return;
    attr = first->chars[0].attr;
    first->chars[0].attr = NULL;
    for (l = first->next; l; l = next)
    {
        next = l->next;
        free_line(l);
    }
    first->next = NULL;
    p->nlines = 1;
    for (i = 1; i < first->nchars; i++)
        free(first->chars[i].attr);
    len = n < CG_CRAWL_CHUNK ? n : CG_CRAWL_CHUNK;
    free(first->chars);
    first->chars = calloc(len ? len : 1, sizeof(*first->chars));
    first->nchars = (uint16_t)(len ? len : 1);
    for (i = 0; i < len; i++)
    {
        first->chars[i] = chars[i];
        first->chars[i].attr = NULL;
    }
    first->chars[0].attr = attr;
    tail = &first->next;
    for (k = len; k < n; k += CG_CRAWL_CHUNK)
    {
        struct cg_char *c;

        len = n - k < CG_CRAWL_CHUNK ? n - k : CG_CRAWL_CHUNK;
        c = calloc(len, sizeof(*c));

        if (!c || !(l = malloc(sizeof(*l))))
        {
            free(c);
            break;
        }
        *l = *first;
        l->chars = c;
        l->next = NULL;
        l->nchars = (uint16_t)len;
        l->xoffset = 56;
        l->yoffset = 16717;
        for (i = 0; i < len; i++)
        {
            l->chars[i] = chars[k + i];
            l->chars[i].attr = NULL;
        }
        *tail = l;
        tail = &l->next;
        p->nlines++;
    }
}

/* delete del characters at at, then insert ascii there (if any) */
static int crawl_edit(struct cg_editor *e, int at, int del, uint16_t ascii)
{
    struct cg_line merged;
    struct cg_char *c;
    int n, i, j;

    if (!cg_crawl_merge(cg_edit_page(e), &merged))
        return 0;
    n = cg_line_length(&merged);
    if (at < 0 || at > n || at + del > n || !(c = calloc((size_t)n + 1, sizeof(*c))))
    {
        cg_crawl_free(&merged);
        return 0;
    }
    for (i = j = 0; i < n; i++)
    {
        if (i == at && ascii)
            c[j++].ascii = ascii;
        if (i >= at && i < at + del)
            continue;
        c[j++] = merged.chars[i];
    }
    if (at == n && ascii)
        c[j++].ascii = ascii;
    cg_crawl_free(&merged);
    crawl_put(e, c, j);
    free(c);
    e->dirty = 1;
    return 1;
}

static void default_attr(struct cg_editor *e)
{
    const struct cg_data *d;

    memset(&e->attr, 0, sizeof(e->attr));
    e->attr.face.r = e->attr.face.g = e->attr.face.b = e->attr.face.a = 255;
    e->attr.face_grad = e->attr.face;
    e->attr.shadow.a = 255;
    e->attr.outline.a = 255;
    e->attr.outline_grad = e->attr.outline;
    e->attr.shadow_type = 1;
    e->attr.shadow_length = 4;
    e->attr.shadow_direction = 7;
    e->attr.id = 0;
    for (d = e->book->data; d; d = d->next)
        if (d->type == CG_DATA_FONT)
        {
            e->attr.id = (int16_t)d->id;
            break;
        }
}

struct cg_editor *cg_edit_new(struct cg_book *book, struct cg_renderer *r)
{
    struct cg_editor *e = calloc(1, sizeof(*e));

    if (!e)
        return NULL;
    e->book = book;
    e->r = r;
    default_attr(e);
    cg_edit_goto_page(e, 0);
    return e;
}

void cg_edit_free(struct cg_editor *e)
{
    free(e);
}

void cg_edit_set_book(struct cg_editor *e, struct cg_book *book)
{
    e->book = book;
    e->dirty = 0;
    default_attr(e);
    cg_edit_goto_page(e, 0);
}

/* the cursor takes on the attributes of the text it sits in */
void cg_edit_take_attr(struct cg_editor *e)
{
    const struct cg_attr *a;

    if (!e->line)
        return;
    a = cg_char_attr(e->line, e->cursor > 0 ? e->cursor - 1 : 0);
    if (a)
        e->attr = *a;
    e->justify = e->line->justify;
}

/* moving the cursor leaves the defaults alone; only a click takes them
   (CGBetaChanges, Beta13) */
void cg_edit_set_line(struct cg_editor *e, struct cg_line *l, int cursor)
{
    int len;

    if (crawling(e) && l)
        l = cg_edit_page(e)->lines;
    e->line = l;
    len = cg_edit_length(e);
    e->cursor = cursor < 0 ? 0 : cursor > len ? len : cursor;
}

/************************************************************ lines by height */

struct cg_line *cg_edit_topmost(struct cg_editor *e)
{
    struct cg_line *l, *best = NULL;

    for (l = cg_edit_page(e)->lines; l; l = l->next)
        if (!best || l->yoffset < best->yoffset)
            best = l;
    return best;
}

struct cg_line *cg_edit_bottommost(struct cg_editor *e)
{
    struct cg_line *l, *best = NULL;

    for (l = cg_edit_page(e)->lines; l; l = l->next)
        if (!best || l->yoffset >= best->yoffset)
            best = l;
    return best;
}

/* the next line up or down; lines at the same height go by list order */
static int before(struct cg_page *p, const struct cg_line *a, const struct cg_line *b)
{
    const struct cg_line *l;

    if (a->yoffset != b->yoffset)
        return a->yoffset < b->yoffset;
    for (l = p->lines; l; l = l->next)
    {
        if (l == a)
            return 1;
        if (l == b)
            return 0;
    }
    return 0;
}

struct cg_line *cg_edit_line_above(struct cg_editor *e, const struct cg_line *cur)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l, *best = NULL;

    for (l = p->lines; l; l = l->next)
        if (l != cur && before(p, l, cur) && (!best || before(p, best, l)))
            best = l;
    return best;
}

struct cg_line *cg_edit_line_below(struct cg_editor *e, const struct cg_line *cur)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l, *best = NULL;

    for (l = p->lines; l; l = l->next)
        if (l != cur && before(p, cur, l) && (!best || before(p, l, best)))
            best = l;
    return best;
}

/************************************************************ metrics */

void cg_edit_justify_line(struct cg_editor *e, struct cg_line *l)
{
    double *xs;
    int w;

    if (!l->justify || cg_edit_page(e)->type == CG_PAGE_CRAWL)
        return;
    if (cg_line_is_graphic(e->book, l))
        w = l->face_width;
    else
    {
        if (!cg_line_length(l) || !(xs = cg_render_line_xs(e->r, e->book, l)))
            return;
        w = (int)(xs[l->nchars] + 0.5);
        free(xs);
    }
    switch (l->justify)
    {
    case CG_JUSTIFY_LEFT:
        l->xoffset = CG_SAFE_LEFT;
        break;
    case CG_JUSTIFY_CENTER:
        l->xoffset = (int16_t)((CG_FRAME_WIDTH - w) / 2);
        break;
    case CG_JUSTIFY_RIGHT:
        l->xoffset = (int16_t)(CG_SAFE_RIGHT - w);
        break;
    }
}

/* TotalHeight and Baseline from the fonts, outlines and shadows, as the
   books on the CD have them: the outline adds its width above and below,
   a shadow its length on the side it falls */
void cg_edit_fix_line(struct cg_editor *e, struct cg_line *l)
{
    int asc = 0, desc = 0, i, n;

    if (cg_line_is_graphic(e->book, l))
    {
        const struct cg_attr *a = cg_char_attr(l, 0);
        int o = a ? cg_outline_y(a->outline_type) : 0, s = a && a->shadow_type ? a->shadow_length : 0;
        int dy = a ? shadow_dy[a->shadow_direction & 7] : 0;

        l->total_height = (uint16_t)(l->face_height + 2 * o + (dy ? s : 0));
        /* a box's baseline is its face's last row, not the row below */
        l->baseline = (uint16_t)(l->face_height - 1 + o + (dy < 0 ? s : 0));
        cg_edit_justify_line(e, l);
        return;
    }
    n = l->nchars ? l->nchars : 0;
    for (i = 0; i < n; i++)
    {
        const struct cg_attr *a = cg_char_attr(l, i);
        int h, b, o, s, dy;

        if (!a || !cg_render_font_metrics(e->r, cg_book_find_data(e->book, (uint16_t)a->id), &h, &b))
            continue;
        o = cg_outline_y(a->outline_type);
        s = a->shadow_type ? a->shadow_length : 0;
        dy = shadow_dy[a->shadow_direction & 7];
        if (b + o + (dy < 0 ? s : 0) > asc)
            asc = b + o + (dy < 0 ? s : 0);
        if (h - b + o + (dy > 0 ? s : 0) > desc)
            desc = h - b + o + (dy > 0 ? s : 0);
    }
    /* a crawl is at most CG_CRAWL_HEIGHT lines: what a shadow or outline
       adds beyond is cut off (NewFunction.c's UpdateFixPage) */
    if (cg_edit_page(e)->type == CG_PAGE_CRAWL && asc + desc > CG_CRAWL_HEIGHT)
    {
        if (asc >= CG_CRAWL_HEIGHT)
            asc = CG_CRAWL_HEIGHT - 1;
        desc = CG_CRAWL_HEIGHT - asc;
        e->message = msg_crawl_trim;
    }
    if (asc + desc)
    {
        l->total_height = (uint16_t)(asc + desc);
        l->baseline = (uint16_t)asc;
    }
    cg_edit_justify_line(e, l);
}

/************************************************************ pages */

void cg_edit_goto_page(struct cg_editor *e, int page)
{
    if (page < 0)
        page = 0;
    if (page >= CG_PAGES_PER_BOOK)
        page = CG_PAGES_PER_BOOK - 1;
    e->page = page;
    cg_edit_set_line(e, cg_edit_topmost(e), 0);
}

static void init_page(struct cg_page *p, int type)
{
    p->used = 1;
    p->type = (uint8_t)type;
    p->speed = 0;
    p->playback = 0;
    p->background = CG_BACKGROUND_SOLID;
    memset(&p->top, 0, sizeof(p->top));
    memset(&p->bottom, 0, sizeof(p->bottom));
    if (type != CG_PAGE_STATIC)
        p->top.a = p->bottom.a = 255;
}

/* a page's text lines into one crawl, top to bottom, a space between
   each; the topmost keeps its place and lends the crawl its attributes */
static void join_to_crawl(struct cg_editor *e)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *first = NULL, *l, *n;
    struct cg_char *c;
    int total = 0, k = 0, i;

    for (l = p->lines; l; l = l->next)
        if (!cg_line_is_graphic(e->book, l))
            total += cg_line_length(l) + 1;
    for (l = cg_edit_topmost(e); l; l = cg_edit_line_below(e, l))
        if (!cg_line_is_graphic(e->book, l))
        {
            first = l;
            break;
        }
    if (!first || !(c = calloc((size_t)total + 1, sizeof(*c))))
        return;
    for (l = first; l; l = cg_edit_line_below(e, l))
    {
        if (cg_line_is_graphic(e->book, l) || !cg_line_length(l))
            continue;
        if (k)
            c[k++].ascii = ' ';
        for (i = 0; i < cg_line_length(l); i++)
        {
            c[k] = l->chars[i];
            c[k++].attr = NULL;
        }
    }
    own_attr(first, 0);
    for (l = p->lines; l; l = n)
    {
        n = l->next;
        if (l != first)
            free_line(l);
    }
    first->next = NULL;
    p->lines = first;
    p->nlines = 1;
    crawl_put(e, c, k);
    free(c);
}

/* a crawl's text into lines that fit the safe area, broken at spaces,
   stacked down from where the crawl ran */
static void split_crawl(struct cg_editor *e)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line merged, *first = p->lines, *l, *n, **tail;
    struct cg_attr *attr;
    double *xs;
    int start = 0, len, i, brk, y;

    if (!first || !cg_crawl_merge(p, &merged))
        return;
    len = cg_line_length(&merged);
    if (!(xs = cg_render_line_xs(e->r, e->book, &merged)))
    {
        cg_crawl_free(&merged);
        return;
    }
    attr = first->chars[0].attr;
    first->chars[0].attr = NULL;
    y = first->yoffset;
    tail = &p->lines;
    for (l = p->lines; l; l = n)
    {
        n = l->next;
        free_line(l);
    }
    p->lines = NULL;
    p->nlines = 0;
    while (start < len || (!len && !p->lines))
    {
        while (start < len && merged.chars[start].ascii == ' ')
            start++;
        for (i = start, brk = -1; i < len && (xs[i + 1] - xs[start] <= CG_SAFE_RIGHT - CG_SAFE_LEFT || i == start); i++)
            if (merged.chars[i].ascii == ' ')
                brk = i;
        if (i < len && brk > start)
            i = brk;
        if (!(l = calloc(1, sizeof(*l))) || !(l->chars = calloc(i > start ? i - start : 1, sizeof(*l->chars))))
        {
            free(l);
            break;
        }
        l->nchars = (uint16_t)(i > start ? i - start : 1);
        memcpy(l->chars, merged.chars + start, (size_t)(i - start) * sizeof(*l->chars));
        for (brk = 0; brk < l->nchars; brk++)
            l->chars[brk].attr = NULL;
        l->chars[0].attr = attr ? attr_copy(attr) : NULL;
        l->xoffset = CG_SAFE_LEFT;
        l->yoffset = (int16_t)y;
        l->justify = merged.justify;
        cg_edit_fix_line(e, l);
        y += l->total_height;
        *tail = l;
        tail = &l->next;
        p->nlines++;
        if (start >= len)
            break;
        start = i;
    }
    free(attr);
    free(xs);
    cg_crawl_free(&merged);
}

/* on a scroll, every character takes its line's first attributes but
   for its font (CGBetaChanges, Beta13) */
static void scroll_attributes(struct cg_page *p)
{
    struct cg_line *l;
    int i;

    for (l = p->lines; l; l = l->next)
    {
        if (!l->nchars || !l->chars[0].attr)
            continue;
        for (i = 1; i < l->nchars; i++)
            if (l->chars[i].attr)
            {
                int16_t id = l->chars[i].attr->id;

                *l->chars[i].attr = *l->chars[0].attr;
                l->chars[i].attr->id = id;
            }
        normalize(l);
    }
}

/* ChangePageType(): moving pages keep only text; to a crawl the text is
   joined, from a crawl it is split back into lines */
int cg_edit_set_page_type(struct cg_editor *e, int type)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l, *n;
    int was = p->type;

    if (type == p->type)
        return 1;
    if (type == CG_PAGE_EMPTY)
    {
        cg_edit_erase_page(e);
        return 1;
    }
    if (was == CG_PAGE_CRAWL)
        split_crawl(e);
    if (was == CG_PAGE_EMPTY || !p->used)
        init_page(p, type);
    else
    {
        p->type = (uint8_t)type;
        if (type == CG_PAGE_STATIC)
            p->top.a = p->bottom.a = 0;
        else
            p->top.a = p->bottom.a = 255;
    }
    if (moving(type))
        for (l = p->lines; l; l = n)
        {
            n = l->next;
            if (cg_line_is_graphic(e->book, l))
            {
                unlink_line(p, l);
                free_line(l);
            }
        }
    if (type == CG_PAGE_CRAWL)
        join_to_crawl(e);
    if (type == CG_PAGE_SCROLL)
        scroll_attributes(p);
    for (l = p->lines; l; l = l->next)
        cg_edit_fix_line(e, l);
    e->dirty = 1;
    e->line = NULL;
    cg_edit_set_line(e, cg_edit_topmost(e), 0);
    return 1;
}

void cg_edit_erase_page(struct cg_editor *e)
{
    struct cg_page *p = cg_edit_page(e);

    clear_page(p);
    p->used = 0;
    p->type = CG_PAGE_EMPTY;
    e->line = NULL;
    e->cursor = 0;
    e->dirty = 1;
}

void cg_edit_erase_book(struct cg_editor *e)
{
    int i;

    for (i = 0; i < CG_PAGES_PER_BOOK; i++)
    {
        clear_page(&e->book->pages[i]);
        e->book->pages[i].used = 0;
        e->book->pages[i].type = CG_PAGE_EMPTY;
    }
    cg_edit_remove_unused_data(e);
    e->line = NULL;
    e->cursor = 0;
    e->dirty = 1;
}

static struct cg_line *copy_line(const struct cg_line *src)
{
    struct cg_line *l = malloc(sizeof(*l));
    int i;

    if (!l)
        return NULL;
    *l = *src;
    l->next = NULL;
    l->chars = src->nchars ? calloc(src->nchars, sizeof(*l->chars)) : NULL;
    if (src->nchars && !l->chars)
    {
        free(l);
        return NULL;
    }
    for (i = 0; i < src->nchars; i++)
    {
        l->chars[i] = src->chars[i];
        l->chars[i].select = 0;
        if (src->chars[i].attr)
            l->chars[i].attr = attr_copy(src->chars[i].attr);
    }
    return l;
}

int cg_edit_copy_page(struct cg_editor *e, int to)
{
    struct cg_page *src = cg_edit_page(e), *dst;
    const struct cg_line *l;

    if (to < 0 || to >= CG_PAGES_PER_BOOK || to == e->page)
        return 0;
    dst = &e->book->pages[to];
    clear_page(dst);
    *dst = *src;
    dst->lines = NULL;
    dst->nlines = 0;
    for (l = src->lines; l; l = l->next)
    {
        struct cg_line *c = copy_line(l);

        if (c)
            append_line(dst, c);
    }
    e->dirty = 1;
    return 1;
}

/************************************************************ cursor */

static double cursor_x(struct cg_editor *e, const struct cg_line *l, int i)
{
    double *xs, x;

    if (!l || cg_line_is_graphic(e->book, l) || !cg_line_length(l) || !(xs = cg_render_line_xs(e->r, e->book, l)))
        return l ? l->xoffset : 0;
    x = l->xoffset + xs[i < l->nchars ? i : l->nchars];
    free(xs);
    return x;
}

static int nearest_char(struct cg_editor *e, const struct cg_line *l, double x)
{
    double *xs;
    int i, best = 0, len = cg_line_length(l);

    if (!len || cg_line_is_graphic(e->book, l) || !(xs = cg_render_line_xs(e->r, e->book, l)))
        return 0;
    for (i = 0; i <= len; i++)
        if (abs((int)(l->xoffset + xs[i] - x)) < abs((int)(l->xoffset + xs[best] - x)))
            best = i;
    free(xs);
    return best;
}

void cg_edit_left(struct cg_editor *e)
{
    if (e->line && e->cursor > 0)
        cg_edit_set_line(e, e->line, e->cursor - 1);
}

void cg_edit_right(struct cg_editor *e)
{
    if (e->line && e->cursor < cg_edit_length(e))
        cg_edit_set_line(e, e->line, e->cursor + 1);
}

void cg_edit_up(struct cg_editor *e)
{
    struct cg_line *l;

    if (crawling(e))
        return;
    if (e->line && (l = cg_edit_line_above(e, e->line)))
        cg_edit_set_line(e, l, nearest_char(e, l, cursor_x(e, e->line, e->cursor)));
}

void cg_edit_down(struct cg_editor *e)
{
    struct cg_line *l;

    if (crawling(e))
        return;
    if (e->line && (l = cg_edit_line_below(e, e->line)))
        cg_edit_set_line(e, l, nearest_char(e, l, cursor_x(e, e->line, e->cursor)));
}

void cg_edit_home(struct cg_editor *e)
{
    cg_edit_set_line(e, cg_edit_topmost(e), 0);
}

void cg_edit_end(struct cg_editor *e)
{
    struct cg_line *l = crawling(e) ? cg_edit_page(e)->lines : cg_edit_bottommost(e);

    cg_edit_set_line(e, l, 1 << 30);
}

/* Tab: a new line at the next 128 pixel column, on the cursor's row
   (CGBetaChanges: "TAB now goes over to the next 128 pixel column ...
   It creates a new line as well") */
void cg_edit_tab(struct cg_editor *e)
{
    struct cg_page *p = cg_edit_page(e);
    int x, y;

    if (p->type == CG_PAGE_EMPTY || p->type == CG_PAGE_CRAWL)
        return;
    x = ((int)cursor_x(e, e->line, e->cursor) / 128 + 1) * 128;
    y = e->line ? e->line->yoffset : CG_SAFE_TOP;
    if (x >= CG_SAFE_RIGHT)
        return;
    if (e->line)
        e->justify = CG_JUSTIFY_NONE;
    cg_edit_new_line(e, x, y);
}

/* Return: the next line down, or a new one under the current line */
void cg_edit_return(struct cg_editor *e)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l, *n;

    if (p->type == CG_PAGE_EMPTY || p->type == CG_PAGE_CRAWL)
        return;
    if (!e->line)
    {
        cg_edit_new_line(e, CG_SAFE_LEFT, CG_SAFE_TOP);
        return;
    }
    l = e->line;
    if ((n = cg_edit_line_below(e, l)))
    {
        cg_edit_set_line(e, n, 0);
        return;
    }
    if (p->type != CG_PAGE_SCROLL && l->yoffset + 2 * l->total_height > CG_FRAME_HEIGHT)
    {
        e->message = msg_line_full;
        return;
    }
    e->justify = l->justify;
    cg_edit_new_line(e, l->xoffset, l->yoffset + l->total_height);
}

/************************************************************ text */

static void set_line_id(struct cg_editor *e, struct cg_line *l);

struct cg_line *cg_edit_new_line(struct cg_editor *e, int x, int y)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l;

    if (p->type == CG_PAGE_EMPTY)
    {
        e->message = msg_change_type;
        return NULL;
    }
    if (!(l = calloc(1, sizeof(*l))) || !(l->chars = calloc(1, sizeof(*l->chars))))
    {
        free(l);
        return NULL;
    }
    l->nchars = 1;
    l->chars[0].attr = attr_copy(&e->attr);
    if (cg_book_find_data(e->book, (uint16_t)e->attr.id) == NULL || e->attr.id == CG_BOX_ID)
        l->chars[0].attr->id = 0;
    l->xoffset = (int16_t)x;
    l->yoffset = (int16_t)y;
    l->justify = e->justify;
    append_line(p, l);
    /* with a brush or a graphic current, the new line is one */
    {
        const struct cg_data *d = cg_book_find_data(e->book, (uint16_t)e->attr.id);

        if (d && d->type != CG_DATA_FONT)
            set_line_id(e, l);
    }
    cg_edit_fix_line(e, l);
    if (!l->total_height)
        l->total_height = l->baseline = 40;
    e->line = l;
    e->cursor = 0;
    e->dirty = 1;
    return l;
}

static int font_allowed(struct cg_editor *e, const struct cg_attr *a)
{
    const struct cg_page *p = cg_edit_page(e);
    const struct cg_data *d = cg_book_find_data(e->book, (uint16_t)a->id);
    int h, b;

    if (!moving(p->type) || !d)
        return 1;
    if (d->type == CG_DATA_BRUSH)
    {
        e->message = msg_no_brushes;
        return 0;
    }
    if (cg_render_font_metrics(e->r, d, &h, &b))
    {
        if (p->type == CG_PAGE_CRAWL && h > CG_CRAWL_HEIGHT)
        {
            e->message = msg_crawl_height;
            return 0;
        }
        if (strstr(d->filename, "olorfonts") || strstr(d->filename, "olorFonts"))
        {
            e->message = msg_no_colorfonts;
            return 0;
        }
    }
    return 1;
}

int cg_edit_insert(struct cg_editor *e, uint16_t ascii)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l;
    struct cg_char *chars;
    int len;

    if (p->type == CG_PAGE_EMPTY)
    {
        e->message = msg_change_type;
        return 0;
    }
    if (!font_allowed(e, &e->attr))
        return 0;
    if (crawling(e) && p->lines)
    {
        if (!crawl_edit(e, e->cursor, 0, ascii))
            return 0;
        e->line = p->lines;
        e->cursor++;
        cg_edit_fix_line(e, e->line);
        return 1;
    }
    if (!e->line || cg_line_is_graphic(e->book, e->line))
    {
        int x = e->line ? e->line->xoffset : CG_SAFE_LEFT, y = e->line ? e->line->yoffset : CG_SAFE_TOP;

        if (!cg_edit_new_line(e, x, y))
            return 0;
    }
    l = e->line;
    len = cg_line_length(l);
    if (len >= CG_MAX_LINE_CHARS - 1)
    {
        e->message = msg_line_full;
        return 0;
    }
    if (len == 0)
    {
        l->chars[0].ascii = ascii;
        free(l->chars[0].attr);
        l->chars[0].attr = attr_copy(&e->attr);
    }
    else
    {
        if (!(chars = realloc(l->chars, (l->nchars + 1) * sizeof(*chars))))
            return 0;
        l->chars = chars;
        if (e->cursor == 0)
        {
            /* the old first character keeps its attribute */
            if (!chars[0].attr)
                chars[0].attr = attr_copy(cg_char_attr(l, 0));
        }
        else if (e->cursor < l->nchars && !chars[e->cursor].attr)
            chars[e->cursor].attr = attr_copy(cg_char_attr(l, e->cursor));
        memmove(&chars[e->cursor + 1], &chars[e->cursor], (l->nchars - e->cursor) * sizeof(*chars));
        l->nchars++;
        memset(&chars[e->cursor], 0, sizeof(*chars));
        chars[e->cursor].ascii = ascii;
        chars[e->cursor].attr = attr_copy(&e->attr);
    }
    e->cursor++;
    normalize(l);
    cg_edit_fix_line(e, l);
    e->dirty = 1;
    return 1;
}

static void delete_char(struct cg_line *l, int i)
{
    if (cg_line_length(l) == 1)
    {
        /* keep the attribute: the line stays, empty */
        l->chars[0].ascii = 0;
        l->chars[0].kerning = 0;
        l->chars[0].select = 0;
        return;
    }
    if (l->chars[i].attr)
    {
        if (i + 1 < l->nchars && !l->chars[i + 1].attr)
            l->chars[i + 1].attr = l->chars[i].attr;
        else
            free(l->chars[i].attr);
    }
    memmove(&l->chars[i], &l->chars[i + 1], (l->nchars - i - 1) * sizeof(*l->chars));
    l->nchars--;
    normalize(l);
}

void cg_edit_backspace(struct cg_editor *e)
{
    struct cg_line *l = e->line, *up;

    if (!l || cg_line_is_graphic(e->book, l))
        return;
    if (crawling(e))
    {
        if (e->cursor > 0 && crawl_edit(e, e->cursor - 1, 1, 0))
            e->cursor--;
        e->line = cg_edit_page(e)->lines;
        return;
    }
    if (e->cursor > 0)
    {
        delete_char(l, e->cursor - 1);
        e->cursor--;
        cg_edit_fix_line(e, l);
        e->dirty = 1;
    }
    else if (!cg_line_length(l) && (up = cg_edit_line_above(e, l)))
    {
        unlink_line(cg_edit_page(e), l);
        free_line(l);
        cg_edit_set_line(e, up, cg_line_length(up));
        e->dirty = 1;
    }
}

void cg_edit_delete(struct cg_editor *e)
{
    struct cg_line *l = e->line;

    if (!l)
        return;
    if (crawling(e))
    {
        if (e->cursor < cg_edit_length(e))
            crawl_edit(e, e->cursor, 1, 0);
        e->line = cg_edit_page(e)->lines;
        return;
    }
    if (cg_line_is_graphic(e->book, l) || !cg_line_length(l))
    {
        cg_edit_erase_line(e);
        return;
    }
    if (e->cursor < cg_line_length(l))
    {
        delete_char(l, e->cursor);
        cg_edit_fix_line(e, l);
        e->dirty = 1;
    }
}

void cg_edit_erase_line(struct cg_editor *e)
{
    struct cg_line *l = e->line, *next;

    if (!l)
        return;
    next = cg_edit_line_below(e, l);
    if (!next)
        next = cg_edit_line_above(e, l);
    unlink_line(cg_edit_page(e), l);
    free_line(l);
    cg_edit_set_line(e, next, 0);
    e->dirty = 1;
}

/************************************************************ selection */

void cg_edit_select_char(struct cg_editor *e, struct cg_line *l, int i, int on)
{
    (void)e;
    if (l && i >= 0 && i < l->nchars)
        l->chars[i].select = (uint8_t)on;
}

void cg_edit_select_line(struct cg_editor *e, struct cg_line *l, int on)
{
    int i;

    if (crawling(e) && l)
    {
        cg_edit_select_page(e, on);
        return;
    }
    for (i = 0; l && i < l->nchars; i++)
        l->chars[i].select = (uint8_t)on;
}

void cg_edit_select_page(struct cg_editor *e, int on)
{
    struct cg_line *l;
    int i;

    for (l = cg_edit_page(e)->lines; l; l = l->next)
        for (i = 0; i < l->nchars; i++)
            l->chars[i].select = (uint8_t)on;
}

int cg_edit_line_selected(const struct cg_line *l)
{
    int i;

    for (i = 0; l && i < l->nchars; i++)
        if (l->chars[i].select)
            return 1;
    return 0;
}

int cg_edit_any_selected(struct cg_editor *e)
{
    struct cg_line *l;

    for (l = cg_edit_page(e)->lines; l; l = l->next)
        if (cg_edit_line_selected(l))
            return 1;
    return 0;
}

/************************************************************ attributes */

static int field_equal(const struct cg_attr *a, const struct cg_attr *b, enum cg_attr_field f)
{
    switch (f)
    {
    case CG_ATTR_ID:
        return a->id == b->id;
    case CG_ATTR_FACE:
        return !memcmp(&a->face, &b->face, sizeof(a->face));
    case CG_ATTR_FACE_GRAD:
        return !memcmp(&a->face_grad, &b->face_grad, sizeof(a->face));
    case CG_ATTR_SHADOW:
        return !memcmp(&a->shadow, &b->shadow, sizeof(a->face));
    case CG_ATTR_OUTLINE:
        return !memcmp(&a->outline, &b->outline, sizeof(a->face));
    case CG_ATTR_OUTLINE_GRAD:
        return !memcmp(&a->outline_grad, &b->outline_grad, sizeof(a->face));
    case CG_ATTR_SHADOW_TYPE:
        return a->shadow_type == b->shadow_type;
    case CG_ATTR_SHADOW_LENGTH:
        return a->shadow_length == b->shadow_length;
    case CG_ATTR_SHADOW_DIRECTION:
        return a->shadow_direction == b->shadow_direction;
    case CG_ATTR_SHADOW_PRIORITY:
        return a->shadow_priority == b->shadow_priority;
    case CG_ATTR_OUTLINE_TYPE:
        return a->outline_type == b->outline_type;
    case CG_ATTR_FILL:
        return a->special_fill == b->special_fill;
    case CG_ATTR_COLORS:
        return !memcmp(&a->face, &b->face, 5 * sizeof(a->face));
    }
    return 1;
}

static void field_copy(struct cg_attr *dst, const struct cg_attr *src, enum cg_attr_field f)
{
    switch (f)
    {
    case CG_ATTR_ID:
        dst->id = src->id;
        break;
    case CG_ATTR_FACE:
        dst->face = src->face;
        break;
    case CG_ATTR_FACE_GRAD:
        dst->face_grad = src->face_grad;
        break;
    case CG_ATTR_SHADOW:
        dst->shadow = src->shadow;
        break;
    case CG_ATTR_OUTLINE:
        dst->outline = src->outline;
        break;
    case CG_ATTR_OUTLINE_GRAD:
        dst->outline_grad = src->outline_grad;
        break;
    case CG_ATTR_SHADOW_TYPE:
        dst->shadow_type = src->shadow_type;
        break;
    case CG_ATTR_SHADOW_LENGTH:
        dst->shadow_length = src->shadow_length;
        break;
    case CG_ATTR_SHADOW_DIRECTION:
        dst->shadow_direction = src->shadow_direction;
        break;
    case CG_ATTR_SHADOW_PRIORITY:
        dst->shadow_priority = src->shadow_priority;
        break;
    case CG_ATTR_OUTLINE_TYPE:
        dst->outline_type = src->outline_type;
        break;
    case CG_ATTR_FILL:
        dst->special_fill = src->special_fill;
        break;
    case CG_ATTR_COLORS:
        dst->face = src->face;
        dst->face_grad = src->face_grad;
        dst->shadow = src->shadow;
        dst->outline = src->outline;
        dst->outline_grad = src->outline_grad;
        break;
    }
}

/* AllSelectSame(): nothing selected, or everything selected already has
   the default's value - then a click steps the default on */
int cg_edit_all_select_same(struct cg_editor *e, enum cg_attr_field f)
{
    struct cg_line *l;
    int i;

    for (l = cg_edit_page(e)->lines; l; l = l->next)
        for (i = 0; i < l->nchars; i++)
            if (l->chars[i].select && !field_equal(cg_char_attr(l, i), &e->attr, f))
                return 0;
    return 1;
}

/* a graphic line takes a brush's size; a text line can only become one
   while it is empty */
static void set_line_id(struct cg_editor *e, struct cg_line *l)
{
    const struct cg_data *d = cg_book_find_data(e->book, (uint16_t)e->attr.id);
    int w, h;

    if (e->attr.id == CG_BOX_ID || (d && (d->type == CG_DATA_BRUSH || d->type == CG_DATA_DRAW)))
    {
        if (moving(cg_edit_page(e)->type))
        {
            e->message = e->attr.id == CG_BOX_ID ? msg_no_boxes : msg_no_brushes;
            return;
        }
        if (!cg_line_is_graphic(e->book, l) && cg_line_length(l))
            return;
        if (d && (d->type == CG_DATA_DRAW ? cg_render_drawing_size(e->r, d, &w, &h) : cg_render_brush_size(e->r, d, &w, &h)))
        {
            l->face_width = (uint16_t)w;
            l->face_height = (uint16_t)h;
        }
        else if (!cg_line_is_graphic(e->book, l))
        {
            l->face_width = 200;
            l->face_height = 100;
        }
        l->chars[0].ascii = ' ';
    }
    else if (cg_line_is_graphic(e->book, l))
    {
        if (!d)
            return;
        l->face_width = l->face_height = 0;
        l->chars[0].ascii = 0;
    }
    own_attr(l, 0)->id = e->attr.id;
}

/* SetSelectAttrib(): the default's field onto every selected character */
void cg_edit_set_attr(struct cg_editor *e, enum cg_attr_field f)
{
    struct cg_line *l;
    int i, any = 0;

    if (crawling(e) && (l = cg_edit_page(e)->lines))
    {
        if (cg_edit_any_selected(e) || !cg_edit_length(e))
        {
            if (f == CG_ATTR_ID && !font_allowed(e, &e->attr))
                return;
            field_copy(own_attr(l, 0), &e->attr, f);
            cg_edit_fix_line(e, l);
            e->dirty = 1;
        }
        return;
    }

    for (l = cg_edit_page(e)->lines; l; l = l->next)
    {
        int changed = 0;

        if (f == CG_ATTR_ID && cg_edit_line_selected(l) && (cg_line_is_graphic(e->book, l) || !cg_line_length(l)))
        {
            set_line_id(e, l);
            changed = 1;
        }
        else
            for (i = 0; i < l->nchars; i++)
                if (l->chars[i].select && !field_equal(cg_char_attr(l, i), &e->attr, f))
                {
                    if (f == CG_ATTR_ID && !font_allowed(e, &e->attr))
                        return;
                    field_copy(own_attr(l, i), &e->attr, f);
                    changed = 1;
                }
        if (changed)
        {
            normalize(l);
            cg_edit_fix_line(e, l);
            any = 1;
        }
    }
    /* an empty current line takes the change too, as typing would */
    if (!any && e->line && !cg_line_length(e->line) && !cg_line_is_graphic(e->book, e->line))
    {
        if (f == CG_ATTR_ID && cg_book_find_data(e->book, (uint16_t)e->attr.id) &&
            cg_book_find_data(e->book, (uint16_t)e->attr.id)->type != CG_DATA_FONT)
            set_line_id(e, e->line);
        else
            field_copy(e->line->chars[0].attr, &e->attr, f);
        cg_edit_fix_line(e, e->line);
        any = 1;
    }
    if (any)
        e->dirty = 1;
}

void cg_edit_set_justify(struct cg_editor *e, int mode)
{
    struct cg_line *l;

    e->justify = (uint8_t)mode;
    for (l = cg_edit_page(e)->lines; l; l = l->next)
        if (cg_edit_line_selected(l) || (l == e->line && !cg_edit_any_selected(e)))
        {
            l->justify = (uint8_t)mode;
            cg_edit_justify_line(e, l);
            e->dirty = 1;
        }
}

/************************************************************ graphics */

int cg_edit_make_box(struct cg_editor *e, int x, int y, int w, int h)
{
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l;
    int16_t id = e->attr.id;

    if (p->type != CG_PAGE_STATIC && p->type != CG_PAGE_BUFFER)
    {
        e->message = p->type == CG_PAGE_EMPTY ? msg_change_type : msg_no_boxes;
        return 0;
    }
    if (!(l = cg_edit_new_line(e, x, y)))
        return 0;
    l->chars[0].ascii = ' ';
    l->chars[0].attr->id = CG_BOX_ID;
    l->face_width = (uint16_t)(w > 0 ? w : 1);
    l->face_height = (uint16_t)(h > 0 ? h : 1);
    l->justify = CG_JUSTIFY_NONE;
    e->attr.id = id;
    cg_edit_fix_line(e, l);
    return 1;
}

/* Depth: the current box or brush to the front or the back */
void cg_edit_depth(struct cg_editor *e, int front)
{
    struct cg_line *l, *cur = e->line;
    int max = 0;

    if (!cur || !cg_line_is_graphic(e->book, cur))
        return;
    for (l = cg_edit_page(e)->lines; l; l = l->next)
        if (cg_line_is_graphic(e->book, l) && l->render_pri > max)
            max = l->render_pri;
    if (front)
        cur->render_pri = (uint8_t)(max < 255 ? max + 1 : 255);
    else
    {
        for (l = cg_edit_page(e)->lines; l; l = l->next)
            if (cg_line_is_graphic(e->book, l) && l != cur && l->render_pri < 255)
                l->render_pri++;
        cur->render_pri = 0;
    }
    e->dirty = 1;
}

/* a line's width as drawn: a graphic's face, a text line's characters */
static int line_width(struct cg_editor *e, const struct cg_line *l)
{
    if (cg_line_is_graphic(e->book, l))
        return l->face_width;
    return (int)(cursor_x(e, l, l->nchars) - l->xoffset);
}

void cg_edit_move_selected(struct cg_editor *e, int dx, int dy)
{
    struct cg_line *l;
    /* on a scroll page one line moves at a time (CGBetaChanges: "Dragging
       more than one line never worked") */
    int any = cg_edit_any_selected(e) && cg_edit_page(e)->type != CG_PAGE_SCROLL;

    /* none goes off the left edge, nor puts characters off the right
       (CGBetaChanges); the lines that move keep their places together */
    if (dx && !crawling(e))
        for (l = cg_edit_page(e)->lines; l; l = l->next)
            if (any ? cg_edit_line_selected(l) : l == e->line)
            {
                /* only moves that take a line further out are stopped:
                   one already out (an old book's) can come back */
                int room_left = l->xoffset > 0 ? l->xoffset : 0,
                    room_right = CG_FRAME_WIDTH - line_width(e, l) - l->xoffset;

                if (room_right < 0)
                    room_right = 0;
                if (dx < -room_left)
                    dx = -room_left;
                if (dx > room_right)
                    dx = room_right;
            }
    /* a crawl moves as its first line; the rest stay parked */
    for (l = cg_edit_page(e)->lines; l; l = crawling(e) ? NULL : l->next)
        if (any ? cg_edit_line_selected(l) : l == e->line)
        {
            l->xoffset = (int16_t)(l->xoffset + dx);
            l->yoffset = (int16_t)(l->yoffset + dy);
            if (dx)
                l->justify = CG_JUSTIFY_NONE;
            e->dirty = 1;
        }
}

/************************************************************ data */

struct cg_data *cg_edit_find_data(struct cg_editor *e, const char *filename, int height)
{
    struct cg_data *d;

    /* graphics carry their font's name but are never looked up by it */
    for (d = e->book->data; d; d = d->next)
        if (d->type != CG_DATA_DRAW && d->filename && !strcasecmp(d->filename, filename) &&
            (height <= 0 || d->type == CG_DATA_BRUSH || d->height == height))
            return d;
    return NULL;
}

/* a PostScript graphic of text in font, rotated and sheared (degrees):
   always a new entry (graphics share their font's name) */
struct cg_data *cg_edit_add_draw(struct cg_editor *e, const struct cg_data *font, const char *text, int rot, int shear)
{
    struct cg_data *d, **pp;
    uint16_t id;
    size_t n = strlen(text);
    uint8_t *x;

    if (!font || font->type != CG_DATA_FONT || !n || n > CG_DRAW_TEXT_MAX)
        return NULL;
    for (id = 0;; id++)
    {
        for (d = e->book->data; d && d->id != id; d = d->next)
            ;
        if (!d)
            break;
    }
    if (!(d = calloc(1, sizeof(*d))) || !(d->filename = strdup(font->filename)) ||
        !(d->drawdata = calloc(1, CG_DRAWDATA_SIZE)))
    {
        if (d)
            free(d->filename);
        free(d);
        return NULL;
    }
    d->type = CG_DATA_DRAW;
    d->id = id;
    d->height = font->height;
    memcpy(d->drawdata + CG_DRAW_TEXT, text, n);
    x = d->drawdata + CG_DRAW_TEXT + n + 1;
    memcpy(x, "CGrs", 4);
    x[4] = (uint8_t)((rot >> 8) & 0xff);
    x[5] = (uint8_t)(rot & 0xff);
    x[6] = (uint8_t)((shear >> 8) & 0xff);
    x[7] = (uint8_t)(shear & 0xff);
    for (pp = &e->book->data; *pp && strcasecmp((*pp)->filename, d->filename) <= 0; pp = &(*pp)->next)
        ;
    d->next = *pp;
    *pp = d;
    e->dirty = 1;
    return d;
}

/* a new entry gets the lowest free ID and goes in name order */
struct cg_data *cg_edit_add_data(struct cg_editor *e, enum cg_data_type type, const char *filename, int height)
{
    struct cg_data *d, **pp;
    uint16_t id;

    if ((d = cg_edit_find_data(e, filename, type == CG_DATA_BRUSH ? 0 : height)))
        return d;
    for (id = 0;; id++)
    {
        for (d = e->book->data; d && d->id != id; d = d->next)
            ;
        if (!d)
            break;
    }
    if (!(d = calloc(1, sizeof(*d))) || !(d->filename = strdup(filename)))
    {
        free(d);
        return NULL;
    }
    d->type = type;
    d->id = id;
    d->height = (uint16_t)(type == CG_DATA_BRUSH ? 0 : height);
    for (pp = &e->book->data; *pp && strcasecmp((*pp)->filename, filename) < 0; pp = &(*pp)->next)
        ;
    d->next = *pp;
    *pp = d;
    e->dirty = 1;
    return d;
}

int cg_edit_data_used(struct cg_editor *e, uint16_t id)
{
    struct cg_line *l;
    int p, i;

    for (p = 0; p < CG_PAGES_PER_BOOK; p++)
        for (l = e->book->pages[p].lines; l; l = l->next)
            for (i = 0; i < l->nchars; i++)
                if (l->chars[i].attr && (uint16_t)l->chars[i].attr->id == id)
                    return 1;
    return 0;
}

/* Clear fonts: drop whatever no page uses, keeping the current one */
int cg_edit_remove_unused_data(struct cg_editor *e)
{
    struct cg_data **pp = &e->book->data, *d;
    int n = 0;

    while ((d = *pp))
        if (d->id != (uint16_t)e->attr.id && !cg_edit_data_used(e, d->id))
        {
            *pp = d->next;
            free(d->filename);
            free(d->drawdata);
            free(d);
            n++;
        }
        else
            pp = &d->next;
    if (n)
        e->dirty = 1;
    return n;
}

/* a text file onto the page: a line per line of text below the current
   one, a crawl's text all on its one line */
int cg_edit_load_text(struct cg_editor *e, const char *path)
{
    struct cg_page *p = cg_edit_page(e);
    char buf[CG_MAX_LINE_CHARS];
    FILE *f;
    int first = 1;

    if (p->type == CG_PAGE_EMPTY)
    {
        e->message = msg_change_type;
        return 0;
    }
    if (!(f = fopen(path, "r")))
        return 0;
    while (fgets(buf, sizeof(buf), f))
    {
        char *c = buf + strlen(buf);

        while (c > buf && (c[-1] == '\n' || c[-1] == '\r'))
            *--c = 0;
        if (p->type == CG_PAGE_CRAWL)
        {
            if (!first && !cg_edit_insert(e, ' '))
                break;
        }
        else if (!first || (e->line && cg_line_length(e->line)))
        {
            cg_edit_end(e);
            cg_edit_return(e);
            if (e->message == msg_line_full)
                break;
        }
        for (c = buf; *c; c++)
            if ((unsigned char)*c >= 32 && !cg_edit_insert(e, (unsigned char)*c))
                break;
        first = 0;
    }
    fclose(f);
    return 1;
}
