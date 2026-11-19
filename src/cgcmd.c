/*
    The CG_AREXX functions on top of the editor, following each case of
    GET_Func, SET_Func, PICKFunc, MAKEFunc, KILLFunc, LOADFunc, SAVEFunc,
    REQ_Func, REXXFunc and MACRFunc in original/src/cgrexx.c, including
    their quirks (SHAD SIZE in steps of two, BORD PRIO inverted).
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cgcmd.h"
#include "cgpage.h"

enum { V_GET, V_SET, V_PICK, V_MAKE, V_KILL, V_LOAD, V_SAVE, V_EXIT, V_FEP, V_REQ, V_REND, V_REXX, V_MACR };
static const char *const verbs[] = { "GET_", "SET_", "PICK", "MAKE", "KILL", "LOAD", "SAVE", "EXIT",
                                     "FEP_", "REQ_", "REND", "REXX", "MACR", NULL };

enum { O_FONT, O_CHAR, O_BRUS, O_LINE, O_PAGE, O_BOOK, O_PICT, O_RECT, O_SHAD, O_BORD, O_BACK, O_TEXT,
       O_DRAW, O_FILE };
static const char *const objects[] = { "FONT", "CHAR", "BRUS", "LINE", "PAGE", "BOOK", "PICT", "RECT",
                                       "SHAD", "BORD", "BACK", "TEXT", "DRAW", "FILE", NULL };

enum { A_FACE, A_TOPR, A_BOTR, A_TYPE, A_SIZE, A_JUST, A_PRIO, A_SPOT, A_VALU, A_RGBA, A_NEXT, A_PREV,
       A_FIRS, A_FILL, A_TALL, A_STAT, A_LAST };
static const char *const attrs[] = { "FACE", "TOPR", "BOTR", "TYPE", "SIZE", "JUST", "PRIO", "SPOT", "VALU",
                                     "RGBA", "NEXT", "PREV", "FIRS", "FILL", "TALL", "STAT", "LAST", NULL };

/* as cgrexx.c answers them, scripts compare with = (case matters);
   JustTypes there is in the wrong order ("Center" for left): here each
   mode has its own name */
static const char *const page_types[] = { "Empty", "Key", "Framestore", "Scroll", "Crawl" };
static const char *const shad_types[] = { "None", "Drop", "Cast" };
static const char *const just_types[] = { "None", "Left", "Center", "Right" };

static int match(const char *s, const char *const *list)
{
    int i;

    for (i = 0; s && list[i]; i++)
        if (!strncasecmp(s, list[i], 4))
            return i;
    return -1;
}

void cg_cmd_init(struct cg_cmd *c, struct cg_editor *e, struct cg_host *host)
{
    memset(c, 0, sizeof(*c));
    c->e = e;
    c->host = host;
}

#define ARG(n) ((n) < argc ? argv[n] : "")
#define NUM(n) atoi(ARG(n))

static void put_rgba(char *result, const struct cg_rgba *c)
{
    snprintf(result, CG_RESULT_MAX, "%d %d %d %d", c->r, c->g, c->b, c->a);
}

static void get_rgba(struct cg_rgba *c, int argc, const char *const *argv, int first)
{
    c->r = (uint8_t)atoi(ARG(first));
    c->g = (uint8_t)atoi(ARG(first + 1));
    c->b = (uint8_t)atoi(ARG(first + 2));
    c->a = (uint8_t)atoi(ARG(first + 3));
}

static const struct cg_attr *cur_attr(struct cg_editor *e)
{
    const struct cg_attr *a = e->line ? cg_char_attr(e->line, e->cursor) : NULL;

    return a ? a : &e->attr;
}

static void put_char(char *result, const struct cg_line *l, int i)
{
    result[0] = (l && i >= 0 && i < l->nchars) ? (char)l->chars[i].ascii : 0;
    result[1] = 0;
}

struct cg_data *cg_cmd_add_font(struct cg_cmd *c, const char *filename, int height)
{
    struct cg_editor *e = c->e;
    struct cg_data *d = cg_edit_find_data(e, filename, height), probe;
    int h, b;

    if (!d)
    {
        memset(&probe, 0, sizeof(probe));
        probe.type = CG_DATA_FONT;
        probe.height = (uint16_t)height;
        probe.filename = (char *)filename;
        if (!cg_render_font_metrics(e->r, &probe, &h, &b))
            return NULL;
        /* bitmap fonts come in their own height */
        if (!(d = cg_edit_find_data(e, filename, h)) && !(d = cg_edit_add_data(e, CG_DATA_FONT, filename, h)))
            return NULL;
    }
    e->attr.id = (int16_t)d->id;
    return d;
}

void cg_join_path(char *out, size_t len, const char *dir, const char *file)
{
    size_t n = strlen(dir);

    if (strchr(file, ':') || file[0] == '/' || !n)
        snprintf(out, len, "%s", file);
    else
        snprintf(out, len, "%.*s%s%s", (int)(len / 2), dir, dir[n - 1] != ':' && dir[n - 1] != '/' ? "/" : "", file);
}

static int ask(struct cg_cmd *c, const char *const *lines, int n, int okcancel);

static int data_loads(struct cg_editor *e, const struct cg_data *d)
{
    int w, h;

    return d->type == CG_DATA_FONT ? cg_render_font_type(e->r, d) != 0 : cg_render_brush_size(e->r, d, &w, &h);
}

int cg_cmd_check_book(struct cg_cmd *c)
{
    struct cg_editor *e = c->e;
    struct cg_host *h = c->host;
    struct cg_data **pp = &e->book->data, *d;
    int replaced = 0;

    while ((d = *pp))
    {
        char l1[CG_RESULT_MAX + 20], l2[64], file[CG_RESULT_MAX] = "", dir[CG_RESULT_MAX] = "";
        const char *lines[2] = { l1, l2 };
        const char *kind = d->type == CG_DATA_FONT ? "font" : "brush";

        if (d->type == CG_DATA_DRAW || data_loads(e, d))
        {
            pp = &d->next;
            continue;
        }
        snprintf(l1, sizeof(l1), "Unable to load %s", d->filename);
        snprintf(l2, sizeof(l2), "Would you like to replace it with another %s?", kind);
        snprintf(dir, sizeof(dir), "%s", c->paths[d->type == CG_DATA_FONT ? 0 : 3][0] ? c->paths[d->type == CG_DATA_FONT ? 0 : 3] :
                 d->type == CG_DATA_FONT ? "Toaster:PSFonts" : "Toaster:Brushes");
        if (ask(c, lines, 2, 1) && h && h->file && h->file(h->ud, d->filename, file, sizeof(file), dir, sizeof(dir), 0))
        {
            char path[2 * CG_RESULT_MAX + 2], *old = d->filename;
            uint16_t height = d->height;

            cg_join_path(path, sizeof(path), dir, file);
            if ((d->filename = strdup(path)))
            {
                /* a PostScript font is asked its height, as before */
                if (d->type == CG_DATA_FONT && cg_render_font_type(e->r, d) == 4 && h->number)
                {
                    long v = height ? height : 40;

                    if (h->number(h->ud, "Select height from 10 to 400", &v) && v >= 10 && v <= 400)
                        d->height = (uint16_t)v;
                }
                if (data_loads(e, d))
                {
                    free(old);
                    e->dirty = 1;
                    replaced++;
                    pp = &d->next;
                    continue;
                }
                free(d->filename);
                d->height = height;
            }
            d->filename = old;
        }
        if (d->type == CG_DATA_BRUSH)
        {
            /* its lines keep their size and draw as boxes */
            *pp = d->next;
            free(d->filename);
            free(d->drawdata);
            free(d);
            e->dirty = 1;
            continue;
        }
        pp = &d->next;
    }
    return replaced;
}

struct cg_data *cg_cmd_add_brush(struct cg_cmd *c, const char *filename)
{
    struct cg_editor *e = c->e;
    struct cg_data *d = cg_edit_find_data(e, filename, 0), probe;
    int w, h;

    if (!d)
    {
        memset(&probe, 0, sizeof(probe));
        probe.type = CG_DATA_BRUSH;
        probe.filename = (char *)filename;
        if (!cg_render_brush_size(e->r, &probe, &w, &h))
            return NULL;
        if (w > CG_FRAME_WIDTH || h > 400)
        {
            e->message = "Maximum brush size is 752 pixels by 400 lines";
            return NULL;
        }
        if (!(d = cg_edit_add_data(e, CG_DATA_BRUSH, filename, 0)))
            return NULL;
    }
    e->attr.id = (int16_t)d->id;
    return d;
}

static int get_func(struct cg_cmd *c, int obj, int argc, const char *const *argv, char *result)
{
    struct cg_editor *e = c->e;
    struct cg_line *l = e->line;
    struct cg_page *p = cg_edit_page(e);
    const struct cg_attr *a = cur_attr(e);
    const struct cg_data *d;
    int at = argc ? match(argv[0], attrs) : -1;

    switch (obj)
    {
    case O_CHAR:
        if (!argc)
        {
            put_char(result, l, e->cursor);
            return CG_RC_OK;
        }
        switch (at)
        {
        case A_RGBA:
        case A_TOPR:
            put_rgba(result, &a->face);
            return CG_RC_OK;
        case A_BOTR:
            put_rgba(result, &a->face_grad);
            return CG_RC_OK;
        case A_FILL:
            strcpy(result, a->special_fill & CG_FILL_FACE_GRAD ? "1" : "0");
            return CG_RC_OK;
        case A_SIZE:
        {
            struct cg_line tmp;
            const struct cg_line *t = cg_edit_cursor_line(e, &tmp);
            double *xs;
            int rc = CG_RC_WARN;

            if (t && e->cursor < cg_line_length(t) && (xs = cg_render_line_xs(e->r, e->book, t)))
            {
                snprintf(result, CG_RESULT_MAX, "%d", (int)(xs[e->cursor + 1] - xs[e->cursor] + 0.5));
                free(xs);
                rc = CG_RC_OK;
            }
            cg_edit_release(&tmp);
            return rc;
        }
        case A_FACE:
            if (!(d = cg_book_find_data(e->book, (uint16_t)a->id)))
                return CG_RC_WARN;
            snprintf(result, CG_RESULT_MAX, "%s", d->filename);
            return CG_RC_OK;
        case A_FIRS:
            put_char(result, l, 0);
            return CG_RC_OK;
        case A_STAT:
            strcpy(result, l && l->nchars && l->chars[0].select ? "1" : "0");
            return CG_RC_OK;
        case A_LAST:
            put_char(result, l, cg_line_length(l) - 1);
            return CG_RC_OK;
        case A_NEXT:
            put_char(result, l, e->cursor + 1);
            return CG_RC_OK;
        case A_PREV:
            put_char(result, l, e->cursor > 0 ? e->cursor - 1 : 0);
            return CG_RC_OK;
        case A_SPOT:
            snprintf(result, CG_RESULT_MAX, "%d", e->cursor);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_LINE:
        if (!argc)
        {
            struct cg_line tmp;
            const struct cg_line *t = cg_edit_cursor_line(e, &tmp);
            int i, n = cg_line_length(t);

            for (i = 0; i < n && i < CG_RESULT_MAX - 1; i++)
                result[i] = (char)t->chars[i].ascii;
            result[i] = 0;
            cg_edit_release(&tmp);
            return CG_RC_OK;
        }
        if (!l)
            return CG_RC_WARN;
        switch (at)
        {
        case A_TYPE:
            d = cg_book_find_data(e->book, (uint16_t)cg_char_attr(l, 0)->id);
            strcpy(result, !cg_line_is_graphic(e->book, l) ? "Text" : d && d->type == CG_DATA_BRUSH ? "Brush" : "Box");
            return CG_RC_OK;
        case A_SIZE:
            snprintf(result, CG_RESULT_MAX, "%d", cg_line_length(l));
            return CG_RC_OK;
        case A_JUST:
            strcpy(result, just_types[l->justify & 3]);
            return CG_RC_OK;
        case A_SPOT:
            snprintf(result, CG_RESULT_MAX, "%d %d", l->xoffset, l->yoffset);
            return CG_RC_OK;
        case A_TALL:
            snprintf(result, CG_RESULT_MAX, "%d", l->total_height);
            return CG_RC_OK;
        case A_STAT:
            strcpy(result, cg_edit_line_selected(l) ? "1" : "0");
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_PAGE:
        if (!argc)
        {
            snprintf(result, CG_RESULT_MAX, "%d", e->page);
            return CG_RC_OK;
        }
        if (at == A_TYPE)
        {
            strcpy(result, page_types[p->type <= CG_PAGE_CRAWL ? p->type : 0]);
            return CG_RC_OK;
        }
        if (at == A_SIZE)
        {
            snprintf(result, CG_RESULT_MAX, "%u", p->nlines);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_BOOK:
        return CG_RC_OK;

    case O_FONT:
        if (!(d = cg_book_find_data(e->book, (uint16_t)a->id)))
            return CG_RC_WARN;
        if (!argc)
        {
            snprintf(result, CG_RESULT_MAX, "%s", d->type == CG_DATA_FONT ? d->filename : "");
            return CG_RC_OK;
        }
        switch (at)
        {
        case A_FIRS:
        case A_NEXT:
        {
            unsigned i = 0;

            /* as cgrexx.c: FIRS returns nothing and restarts the list,
               NEXT then gives the first font (SaveState.Rexx counts on it) */
            if (at == A_FIRS)
            {
                c->font_list = 0;
                return CG_RC_OK;
            }
            for (d = e->book->data; d; d = d->next)
                if (d->type == CG_DATA_FONT && i++ == c->font_list)
                {
                    snprintf(result, CG_RESULT_MAX, "%s,%d", d->filename, d->height);
                    break;
                }
            c->font_list = d ? c->font_list + 1 : 0;
            return CG_RC_OK;
        }
        case A_TYPE:
            snprintf(result, CG_RESULT_MAX, "%d", cg_render_font_type(e->r, d));
            return CG_RC_OK;
        case A_SIZE:
            snprintf(result, CG_RESULT_MAX, "%d", d->height);
            return CG_RC_OK;
        }
        d = cg_edit_find_data(e, argv[0], argc > 1 ? NUM(1) : 0);
        snprintf(result, CG_RESULT_MAX, "%d", d ? d->height : 0);
        return CG_RC_OK;

    case O_RECT:
    case O_DRAW:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_TOPR:
        case A_RGBA:
            put_rgba(result, &a->face);
            return CG_RC_OK;
        case A_BOTR:
            put_rgba(result, &a->face_grad);
            return CG_RC_OK;
        case A_FILL:
            strcpy(result, a->special_fill & CG_FILL_FACE_GRAD ? "1" : "0");
            return CG_RC_OK;
        case A_SIZE:
            snprintf(result, CG_RESULT_MAX, "%d %d", l ? l->face_width : 0, l ? l->face_height : 0);
            return CG_RC_OK;
        case A_SPOT:
            snprintf(result, CG_RESULT_MAX, "%d %d", l ? l->xoffset : 0, l ? l->yoffset : 0);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_SHAD:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_TOPR:
        case A_RGBA:
        case A_BOTR:
            put_rgba(result, &a->shadow);
            return CG_RC_OK;
        case A_TYPE:
            strcpy(result, shad_types[a->shadow_type <= 2 ? a->shadow_type : 0]);
            return CG_RC_OK;
        case A_SIZE:
            snprintf(result, CG_RESULT_MAX, "%d", a->shadow_length >> 1);
            return CG_RC_OK;
        case A_SPOT:
            snprintf(result, CG_RESULT_MAX, "%d", a->shadow_direction);
            return CG_RC_OK;
        case A_PRIO:
            snprintf(result, CG_RESULT_MAX, "%d", a->shadow_priority);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_BORD:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_TOPR:
        case A_RGBA:
            put_rgba(result, &a->outline);
            return CG_RC_OK;
        case A_BOTR:
            put_rgba(result, &a->outline_grad);
            return CG_RC_OK;
        case A_FILL:
            strcpy(result, a->special_fill & CG_FILL_OUTLINE_GRAD ? "1" : "0");
            return CG_RC_OK;
        case A_SIZE:
            snprintf(result, CG_RESULT_MAX, "%d", a->outline_type);
            return CG_RC_OK;
        case A_PRIO:
            strcpy(result, a->shadow_priority ? "0" : "1");
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_BACK:
        if (!argc)
            return CG_RC_OK;
        switch (at)
        {
        case A_TOPR:
        case A_RGBA:
            snprintf(result, CG_RESULT_MAX, "%d %d %d", p->top.r, p->top.g, p->top.b);
            return CG_RC_OK;
        case A_BOTR:
            snprintf(result, CG_RESULT_MAX, "%d %d %d", p->bottom.r, p->bottom.g, p->bottom.b);
            return CG_RC_OK;
        case A_FILL:
            strcpy(result, p->background == CG_BACKGROUND_GRADATION ? "1" : "0");
            return CG_RC_OK;
        case A_TYPE:
            snprintf(result, CG_RESULT_MAX, "%d", p->background);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;
    }
    return CG_RC_ERROR;
}

static int set_func(struct cg_cmd *c, int obj, int argc, const char *const *argv, char *result)
{
    struct cg_editor *e = c->e;
    struct cg_line *l = e->line, *n;
    struct cg_page *p = cg_edit_page(e);
    struct cg_attr *a = &e->attr;
    const struct cg_data *d;
    int at = argc ? match(argv[0], attrs) : -1, v;

    switch (obj)
    {
    case O_CHAR:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_RGBA:
        case A_TOPR:
            if (argc < 4)
                return CG_RC_ERROR;
            get_rgba(&a->face, argc, argv, 1);
            cg_edit_set_attr(e, CG_ATTR_COLORS);
            return CG_RC_OK;
        case A_BOTR:
            if (argc < 4)
                return CG_RC_ERROR;
            get_rgba(&a->face_grad, argc, argv, 1);
            cg_edit_set_attr(e, CG_ATTR_COLORS);
            return CG_RC_OK;
        case A_FILL:
            if (argc < 2)
                return CG_RC_ERROR;
            if (NUM(1) == 1)
                a->special_fill |= CG_FILL_FACE_GRAD;
            else
                a->special_fill &= ~CG_FILL_FACE_GRAD;
            cg_edit_set_attr(e, CG_ATTR_FILL);
            return CG_RC_OK;
        case A_SPOT:
            if (argc < 2 || !(v = NUM(1)) || !l)
                return CG_RC_ERROR;
            cg_edit_set_line(e, l, v - 1);
            return CG_RC_OK;
        case A_NEXT:
            cg_edit_right(e);
            return CG_RC_OK;
        case A_PREV:
            cg_edit_left(e);
            return CG_RC_OK;
        case A_FIRS:
            if (l)
                cg_edit_set_line(e, l, 0);
            return CG_RC_OK;
        case A_LAST:
            if (l)
                cg_edit_set_line(e, l, cg_line_length(l) - 1);
            return CG_RC_OK;
        case A_FACE:
            if (argc < 2)
                return CG_RC_ERROR;
            if (!(d = cg_edit_find_data(e, argv[1], argc > 2 ? NUM(2) : 0)))
                return CG_RC_ERROR;
            a->id = (int16_t)d->id;
            cg_edit_set_attr(e, CG_ATTR_ID);
            return CG_RC_OK;
        case A_VALU:
            if (argc < 2 || !l || e->cursor >= cg_line_length(l) || !(v = NUM(1) & 0xffff))
                return CG_RC_ERROR;
            l->chars[e->cursor].ascii = (uint16_t)v;
            cg_edit_fix_line(e, l);
            e->dirty = 1;
            return CG_RC_OK;
        }
        if (!l || e->cursor >= cg_line_length(l) || !argv[0][0])
            return CG_RC_ERROR;
        l->chars[e->cursor].ascii = (unsigned char)argv[0][0];
        cg_edit_fix_line(e, l);
        e->dirty = 1;
        return CG_RC_OK;

    case O_LINE:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_SPOT:
            if (argc < 3 || !l)
                return CG_RC_ERROR;
            l->xoffset = (int16_t)NUM(1);
            l->yoffset = (int16_t)NUM(2);
            e->dirty = 1;
            return CG_RC_OK;
        case A_JUST:
            if (argc < 2 || !l)
                return CG_RC_ERROR;
            switch (toupper((unsigned char)argv[1][0]))
            {
            case 'C':
                v = CG_JUSTIFY_CENTER;
                break;
            case 'L':
                v = CG_JUSTIFY_LEFT;
                break;
            case 'R':
                v = CG_JUSTIFY_RIGHT;
                break;
            default:
                v = CG_JUSTIFY_NONE;
            }
            l->justify = (uint8_t)v;
            cg_edit_justify_line(e, l);
            e->dirty = 1;
            return CG_RC_OK;
        case A_FIRS:
            cg_edit_set_line(e, cg_edit_topmost(e), 0);
            return CG_RC_OK;
        case A_LAST:
            cg_edit_set_line(e, cg_edit_bottommost(e), 0);
            return CG_RC_OK;
        case A_NEXT:
            if (l && (n = cg_edit_line_below(e, l)))
                cg_edit_set_line(e, n, 0);
            return CG_RC_OK;
        case A_PREV:
            if (l && (n = cg_edit_line_above(e, l)))
                cg_edit_set_line(e, n, 0);
            return CG_RC_OK;
        }
        /* SetLine(): the n'th line in list order; none to go to is no error */
        if (argc == 1 && (v = NUM(0)) > 0)
        {
            if (p->type != CG_PAGE_EMPTY && p->lines)
            {
                for (n = p->lines; n && n->next && --v > 0; n = n->next)
                    ;
                cg_edit_set_line(e, n, 0);
            }
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_PAGE:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_TYPE:
            if (argc < 2)
                return CG_RC_ERROR;
            switch (toupper((unsigned char)argv[1][0]))
            {
            case 'C':
                cg_edit_set_page_type(e, CG_PAGE_CRAWL);
                break;
            case 'K':
                cg_edit_set_page_type(e, CG_PAGE_STATIC);
                break;
            case 'S':
                cg_edit_set_page_type(e, CG_PAGE_SCROLL);
                break;
            case 'F':
                cg_edit_set_page_type(e, CG_PAGE_BUFFER);
                break;
            }
            return CG_RC_OK;
        case A_NEXT:
            cg_edit_goto_page(e, e->page + 1);
            return CG_RC_OK;
        case A_PREV:
            cg_edit_goto_page(e, e->page - 1);
            return CG_RC_OK;
        }
        /* a page out of range is ignored, as in cgrexx.c */
        if (argc == 1)
        {
            if ((v = NUM(0)) >= 0 && v < CG_PAGES_PER_BOOK)
                cg_edit_goto_page(e, v);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_FONT:
        if (!argc || !(d = cg_edit_find_data(e, argv[0], argc >= 2 ? NUM(1) : 0)))
            return CG_RC_ERROR;
        a->id = (int16_t)d->id;
        cg_edit_set_attr(e, CG_ATTR_ID);
        return CG_RC_OK;

    case O_SHAD:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_RGBA:
        case A_TOPR:
            if (argc < 4)
                return CG_RC_ERROR;
            get_rgba(&a->shadow, argc, argv, 1);
            cg_edit_set_attr(e, CG_ATTR_COLORS);
            return CG_RC_OK;
        case A_PRIO:
            if (argc > 1)
            {
                a->shadow_priority = NUM(1) == 1;
                cg_edit_set_attr(e, CG_ATTR_SHADOW_PRIORITY);
            }
            return CG_RC_OK;
        case A_TYPE:
            if (argc > 1)
            {
                switch (toupper((unsigned char)argv[1][0]))
                {
                case 'N':
                    v = 0;
                    break;
                case 'D':
                    v = 1;
                    break;
                case 'C':
                    v = 2;
                    break;
                default:
                    v = NUM(1);
                }
                a->shadow_type = (uint8_t)v;
                cg_edit_set_attr(e, CG_ATTR_SHADOW_TYPE);
            }
            return CG_RC_OK;
        case A_SIZE:
            /* the length before, as cgrexx.c answers (stored doubled) */
            snprintf(result, CG_RESULT_MAX, "%d", a->shadow_length);
            if (argc > 1)
            {
                v = NUM(1);
                v = v < 1 ? 1 : v > 5 ? 5 : v;
                a->shadow_length = (uint8_t)(v << 1);
                cg_edit_set_attr(e, CG_ATTR_SHADOW_LENGTH);
            }
            return CG_RC_OK;
        case A_SPOT:
            if (argc > 1)
            {
                a->shadow_direction = (uint8_t)(NUM(1) & 7);
                cg_edit_set_attr(e, CG_ATTR_SHADOW_DIRECTION);
            }
            return CG_RC_OK;
        }
        if (argc == 1 && (v = NUM(0)))
        {
            a->shadow_length = (uint8_t)v;
            cg_edit_set_attr(e, CG_ATTR_SHADOW_LENGTH);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_BORD:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_RGBA:
        case A_TOPR:
            if (argc < 4)
                return CG_RC_ERROR;
            get_rgba(&a->outline, argc, argv, 1);
            cg_edit_set_attr(e, CG_ATTR_COLORS);
            return CG_RC_OK;
        case A_BOTR:
            get_rgba(&a->outline_grad, argc, argv, 1);
            cg_edit_set_attr(e, CG_ATTR_COLORS);
            return CG_RC_OK;
        case A_SIZE:
            if (argc > 1)
            {
                a->outline_type = (uint8_t)NUM(1);
                cg_edit_set_attr(e, CG_ATTR_OUTLINE_TYPE);
            }
            return CG_RC_OK;
        case A_PRIO:
            if (argc > 1)
            {
                a->shadow_priority = NUM(1) != 1;
                cg_edit_set_attr(e, CG_ATTR_SHADOW_PRIORITY);
            }
            return CG_RC_OK;
        case A_FILL:
            if (argc > 1)
            {
                if (NUM(1) == 1)
                    a->special_fill |= CG_FILL_OUTLINE_GRAD;
                else
                    a->special_fill &= ~CG_FILL_OUTLINE_GRAD;
                cg_edit_set_attr(e, CG_ATTR_FILL);
            }
            return CG_RC_OK;
        }
        if (argc == 1 && (v = NUM(0)))
        {
            a->outline_type = (uint8_t)v;
            cg_edit_set_attr(e, CG_ATTR_OUTLINE_TYPE);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;

    case O_BACK:
        if (!argc)
            return CG_RC_ERROR;
        switch (at)
        {
        case A_RGBA:
        case A_TOPR:
            if (argc < 4)
                return CG_RC_ERROR;
            p->top.r = (uint8_t)NUM(1);
            p->top.g = (uint8_t)NUM(2);
            p->top.b = (uint8_t)NUM(3);
            break;
        case A_BOTR:
            if (argc < 4)
                return CG_RC_ERROR;
            p->bottom.r = (uint8_t)NUM(1);
            p->bottom.g = (uint8_t)NUM(2);
            p->bottom.b = (uint8_t)NUM(3);
            break;
        case A_FILL:
        case A_TYPE:
            if (argc > 1)
                p->background = (uint8_t)NUM(1);
            break;
        default:
            p->background = (uint8_t)NUM(0);
        }
        e->dirty = 1;
        return CG_RC_OK;
    }
    return CG_RC_ERROR;
}

static int ask(struct cg_cmd *c, const char *const *lines, int n, int okcancel)
{
    return c->host && c->host->request ? c->host->request(c->host->ud, lines, n, okcancel) : 0;
}

static int req_func(struct cg_cmd *c, const char *obj, int argc, const char *const *argv, char *result)
{
    struct cg_host *h = c->host;
    char file[CG_RESULT_MAX] = "", dir[CG_RESULT_MAX - 2] = "";
    int o = toupper((unsigned char)obj[0]);
    long n;

    /* file, drawer and string requesters have titles of their own when
       given none (cgrexx.c); the others need their text */
    if (!argc && o != 'F' && o != 'D' && o != 'S')
        return CG_RC_ERROR;
    switch (o)
    {
    case 'A':
        strcpy(result, ask(c, argv, argc > 3 ? 3 : argc, 1) ? "1" : "0");
        return CG_RC_OK;
    case 'T':
        strcpy(result, ask(c, argv, argc > 4 ? 4 : argc, 0) ? "1" : "0");
        return CG_RC_OK;
    case 'F':
    case 'D':
        /* a drawer not given is the last one used, as cgrexx.c kept it */
        snprintf(dir, sizeof(dir), "%.*s", (int)sizeof(dir) - 1, c->req_dir);
        if (o == 'F')
        {
            if (argc > 1)
                snprintf(file, sizeof(file), "%s", argv[1]);
            if (argc > 2)
                snprintf(dir, sizeof(dir), "%s", argv[2]);
        }
        else if (argc > 1)
            snprintf(dir, sizeof(dir), "%s", argv[1]);
        if (!h || !h->file ||
            !h->file(h->ud, argc ? argv[0] : o == 'F' ? "Get File" : "Get Directory", file, sizeof(file), dir,
                     sizeof(dir), 0))
            return CG_RC_OK;
        snprintf(c->req_dir, sizeof(c->req_dir), "%s", dir);
        if (toupper((unsigned char)obj[0]) == 'F')
        {
            /* the whole path: scripts load what they are given */
            cg_join_path(result, CG_RESULT_MAX, dir, file);
        }
        else
        {
            size_t len = strlen(dir);

            snprintf(result, CG_RESULT_MAX, "%s%s", dir, len && dir[len - 1] != ':' && dir[len - 1] != '/' ? "/" : "");
        }
        return CG_RC_OK;
    case 'S':
        if (argc > 1)
            snprintf(result, CG_RESULT_MAX, "%s", argv[1]);
        if (!h || !h->string || !h->string(h->ud, argc ? argv[0] : "Enter Text", result, CG_RESULT_MAX))
            result[0] = 0;
        return CG_RC_OK;
    case 'N':
        n = argc > 1 ? atol(argv[1]) : 0;
        if (h && h->number && h->number(h->ud, argv[0], &n))
            snprintf(result, CG_RESULT_MAX, "%ld", n);
        return CG_RC_OK;
    case 'B':
        if (h && h->status)
            h->status(h->ud, argv[0]);
        return CG_RC_OK;
    }
    return CG_RC_ERROR;
}

static int load_func(struct cg_cmd *c, int obj, int argc, const char *const *argv, char *result)
{
    struct cg_editor *e = c->e;
    struct cg_host *h = c->host;

    /* no file: the requesters of the bar's gadgets, as cgrexx.c does */
    if ((!argc && (obj == O_PICT || obj == O_BRUS || obj == O_TEXT || obj == O_FILE)) || (obj == O_FONT && argc < 2))
    {
        int what = obj == O_PICT ? CG_ASK_PICT : obj == O_BRUS ? CG_ASK_BRUSH : obj == O_FONT ? CG_ASK_FONT : CG_ASK_TEXT;
        int ok = h && h->ask_load && h->ask_load(h->ud, what);

        if (obj != O_PICT)
            strcpy(result, ok ? "1" : "0");
        return CG_RC_OK;
    }
    switch (obj)
    {
    case O_PICT:
        if (!h || !h->load_background)
            return CG_RC_ERROR;
        return h->load_background(h->ud, argv[0]) ? CG_RC_OK : CG_RC_ERROR;
    case O_BRUS:
        strcpy(result, cg_cmd_add_brush(c, argv[0]) ? "1" : "0");
        return CG_RC_OK;
    case O_FONT:
        if (NUM(1) < 10)
            return CG_RC_ERROR;
        strcpy(result, cg_cmd_add_font(c, argv[0], NUM(1)) ? "1" : "0");
        return CG_RC_OK;
    case O_BOOK:
    case O_PAGE:
    {
        const char *err;

        /* both are LoadPage() onto the current page, as in cgrexx.c */
        if (!argc)
            return CG_RC_ERROR;
        if (!cg_page_load(e, argv[0], &err))
            strcpy(result, "0");
        return CG_RC_OK;
    }
    case O_TEXT: /* LOADTEXT is LOADFILE in cgrexx.c's table */
    case O_FILE:
        strcpy(result, cg_edit_load_text(e, argv[0]) ? "1" : "0");
        return CG_RC_OK;
    }
    return CG_RC_ERROR;
}

static int run(struct cg_cmd *c, const char *name, int argc, const char *const *argv, char *result);

int cg_cmd_run(struct cg_cmd *c, const char *name, int argc, const char *const *argv, char *result)
{
    int rc;

    c->err = 0;
    if (match(name, verbs) < 0)
    {
        /* not ours: ARexx goes on to the next function host */
        result[0] = 0;
        c->err = CG_ERR_MISSING_FN;
        return CG_RC_WARN;
    }
    rc = run(c, name, argc, argv, result);
    if (rc != CG_RC_OK && !c->err)
        c->err = CG_ERR_BADARG;
    return rc;
}

static int run(struct cg_cmd *c, const char *name, int argc, const char *const *argv, char *result)
{
    struct cg_editor *e = c->e;
    int verb = match(name, verbs), i, m;
    const char *on = strlen(name) > 4 ? name + 4 : "";
    int obj;
    const char *err;

    /* MAKELINE, and MAKE_LINE as well */
    if (*on == '_')
        on++;
    obj = match(on, objects);
    result[0] = 0;
    e->message = NULL;
    switch (verb)
    {
    case V_GET:
        return get_func(c, obj, argc, argv, result);
    case V_SET:
        return set_func(c, obj, argc, argv, result);
    case V_PICK:
        switch (obj)
        {
        case O_CHAR:
            if (e->line)
                cg_edit_select_char(e, e->line, e->cursor, !argc);
            return CG_RC_OK;
        case O_LINE:
            cg_edit_select_line(e, e->line, !argc);
            return CG_RC_OK;
        case O_PAGE:
            if (cg_edit_page(e)->type != CG_PAGE_EMPTY)
                cg_edit_select_page(e, !argc);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;
    case V_MAKE:
        switch (obj)
        {
        case O_CHAR:
            if (!argc || !argv[0][0])
                return CG_RC_ERROR;
            cg_edit_insert(e, (unsigned char)argv[0][0]);
            return CG_RC_OK;
        case O_LINE:
            for (i = 0; argc && argv[0][i]; i++)
                cg_edit_insert(e, (unsigned char)argv[0][i]);
            cg_edit_return(e);
            return CG_RC_OK;
        case O_RECT:
        {
            struct cg_page *p = cg_edit_page(e);

            if (p->type != CG_PAGE_STATIC && p->type != CG_PAGE_BUFFER)
                return CG_RC_ERROR;
            cg_edit_make_box(e, argc > 0 ? NUM(0) : CG_SAFE_LEFT, argc > 1 ? NUM(1) : CG_SAFE_TOP,
                             argc > 3 ? NUM(2) : 200, argc > 3 ? NUM(3) : 100);
            return CG_RC_OK;
        }
        }
        return CG_RC_ERROR;
    case V_KILL:
        switch (obj)
        {
        case O_CHAR:
            cg_edit_delete(e);
            return CG_RC_OK;
        case O_LINE:
            cg_edit_erase_line(e);
            return CG_RC_OK;
        case O_PAGE:
            cg_edit_erase_page(e);
            return CG_RC_OK;
        }
        return CG_RC_ERROR;
    case V_LOAD:
        return load_func(c, obj, argc, argv, result);
    case V_SAVE:
        if (!argc)
            return CG_RC_ERROR;
        if (obj == O_BOOK)
        {
            if (!cg_book_save(e->book, argv[0], &err))
                return CG_RC_ERROR;
            e->dirty = 0;
            return CG_RC_OK;
        }
        if (obj == O_PAGE)
            return cg_page_save(e, argv[0], &err) ? CG_RC_OK : CG_RC_ERROR;
        return CG_RC_ERROR;
    case V_EXIT:
        c->exit = 1;
        return CG_RC_OK;
    case V_REQ:
        return req_func(c, on, argc, argv, result);
    case V_REND:
        return c->host && c->host->render ? (c->host->render(c->host->ud), CG_RC_OK) : CG_RC_OK;
    case V_REXX:
    {
        char name[CG_RESULT_MAX] = "";

        /* with no name, a string requester asks for one (cgrexx.c) */
        if (!c->host || !c->host->macro)
            return CG_RC_WARN;
        if (argc)
            snprintf(name, sizeof(name), "%s", argv[0]);
        else if (!c->host->string || !c->host->string(c->host->ud, "ARexx Macro Name:", name, sizeof(name)) || !name[0])
            return CG_RC_WARN;
        c->host->macro(c->host->ud, name);
        return CG_RC_OK;
    }
    case V_MACR:
        if (!argc)
            return CG_RC_WARN;
        m = NUM(0);
        m = m < 0 ? 0 : m >= CG_MACROS ? CG_MACROS - 1 : m;
        if (argc > 1)
            snprintf(c->macros[m], CG_MACRO_MAX, "%s", argv[1]);
        else
            snprintf(result, CG_RESULT_MAX, "%s", c->macros[m]);
        return CG_RC_OK;
    }
    return CG_RC_ERROR;
}

int cg_cmd_line(struct cg_cmd *c, const char *line, char *result)
{
    char buf[1024], *argv[16], *s = buf, *name;
    int argc = 0;

    snprintf(buf, sizeof(buf), "%s", line);
    while (isspace((unsigned char)*s))
        s++;
    name = s;
    while (*s && !isspace((unsigned char)*s) && *s != '(')
        s++;
    if (*s)
        *s++ = 0;
    while (*s && argc < 16)
    {
        while (isspace((unsigned char)*s) || *s == ',' || *s == ')')
            s++;
        if (!*s)
            break;
        if (*s == '"' || *s == '\'')
        {
            char q = *s++;

            argv[argc++] = s;
            while (*s && *s != q)
                s++;
        }
        else
        {
            argv[argc++] = s;
            while (*s && !isspace((unsigned char)*s) && *s != ',' && *s != ')')
                s++;
        }
        if (*s)
            *s++ = 0;
    }
    return cg_cmd_run(c, name, argc, (const char *const *)argv, result);
}

/* "CG Config" then #Macros, #Startup, #Paths and #Devices sections */
int cg_cmd_read_config(struct cg_cmd *c, const char *path)
{
    char buf[CG_MACRO_MAX], *s;
    FILE *f = fopen(path, "r");
    int section = -1, n = 0;

    if (!f)
        return 0;
    if (!fgets(buf, sizeof(buf), f) || strncmp(buf, "CG Config", 9))
    {
        fclose(f);
        return 0;
    }
    while (fgets(buf, sizeof(buf), f))
    {
        if (buf[0] == '#')
        {
            section = !strncasecmp(buf, "#Macros", 7) ? 0 : !strncasecmp(buf, "#Startup", 8) ? 1 :
                      !strncasecmp(buf, "#Paths", 6) ? 2 : 3;
            n = 0;
            continue;
        }
        for (s = buf; *s > 30 && *s != '#'; s++)
            ;
        *s = 0;
        while (s > buf && isspace((unsigned char)s[-1]))
            *--s = 0;
        if (!buf[0])
            continue;
        if (section == 0 && n < CG_MACROS)
            snprintf(c->macros[n++], CG_MACRO_MAX, "%s", buf);
        else if (section == 1)
            snprintf(c->startup, CG_MACRO_MAX, "%s", buf);
        else if (section == 2 && n < 5)
            snprintf(c->paths[n++], CG_MACRO_MAX, "%s", buf);
    }
    fclose(f);
    return 1;
}
