/*
    Page files as the CG's SaveCrouton() leaves them on the 4.3 CD:

    FORM ILBM   the page
    FORM ILBM   the 80x50 icon
    FORM CrUD   TYPE (KEYF FRAM SCRO CRAW), LIBS effects.library, TAGS
                with the book (tag 0x69, 0x10000069 for frames)

    The Toaster wrote the page in its own framestore planes, and a
    scroll or crawl as a sheet of glyphs with their places. Here the
    page is a deep ILBM, for this CG and the Switcher for AROS: a frame
    in 24 planes; a key in 32, the last eight its alpha; a scroll or
    crawl as the strip of its lines alone, in 32 planes, which the
    Switcher moves through the frame (BMHD's y is a crawl's line).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgpage.h"
#include "cgilbm.h"
#include "cgplay.h"

#define ICON_W 80
#define ICON_H 50

/* the Toaster's tags, and this CG's own for a page that plays forever */
#define TAG_DURATION     0x8000006c
#define TAG_SPEED        0x8000006a
#define TAG_HOLDFIELDS   0x80000087
#define TAG_FADEOUT      0x80000033
#define TAG_PAGE         0x69
#define TAG_FRAMEPAGE    0x10000069
#define TAG_LOOP         0x80f70003

static const uint8_t libs[24] = { 0xff, 0xff, 0xfc, 0xd0, 0x00, 0x10, 'e', 'f', 'f', 'e', 'c', 't', 's', '.',
                                  'l', 'i', 'b', 'r', 'a', 'r', 'y', 0, 0, 0 };

struct out
{
    FILE *f;
    int   bad;
};

static void put(struct out *o, const void *p, size_t n)
{
    if (n && fwrite(p, 1, n, o->f) != n)
        o->bad = 1;
}

static void put32(struct out *o, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put(o, b, 4);
}

/* start a chunk; returns where its length goes */
static long begin(struct out *o, const char *id)
{
    long at;

    put(o, id, 4);
    at = ftell(o->f);
    put32(o, 0);
    return at;
}

static void end(struct out *o, long at)
{
    long now = ftell(o->f);
    uint32_t len = (uint32_t)(now - at - 4);

    if (len & 1)
        put(o, "", 1);
    fseek(o->f, at, SEEK_SET);
    put32(o, len);
    fseek(o->f, 0, SEEK_END);
}

static const char *crud_type(int type)
{
    switch (type)
    {
    case CG_PAGE_BUFFER:
        return "FRAM";
    case CG_PAGE_SCROLL:
        return "SCRO";
    case CG_PAGE_CRAWL:
        return "CRAW";
    }
    return "KEYF";
}

int cg_page_save(struct cg_editor *e, const char *path, const char **err)
{
    struct cg_page *p = cg_edit_page(e);
    int moves = p->type == CG_PAGE_SCROLL || p->type == CG_PAGE_CRAWL;
    struct cg_play *play = NULL;
    const uint8_t *strip = NULL;
    uint8_t *rgba, *icon;
    struct out o = { NULL, 0 };
    long form, chunk, tags, tag, fields = 180;
    int x, y, sw = 0, sh = 0, top = 0;

    if (p->type == CG_PAGE_EMPTY)
    {
        *err = "Not allowed on this page type.";
        return 0;
    }
    if (moves)
    {
        if ((play = cg_play_new(e->r, e->book, e->page, 100)))
            strip = cg_play_strip(play, &sw, &sh, &top);
        if (!strip || sw > 65535 || sh > 65535)
        {
            cg_play_free(play);
            *err = strip ? "Page too long to save" : "Unable to save page";
            return 0;
        }
        fields = cg_play_fields(play);
    }
    rgba = malloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT * 4);
    icon = malloc((size_t)ICON_W * ICON_H * 4);
    if (!rgba || !icon || !(o.f = fopen(path, "wb")))
    {
        free(rgba);
        free(icon);
        cg_play_free(play);
        *err = "Unable to save page";
        return 0;
    }
    cg_render_page(e->r, e->book, e->page, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, 1.0);
    for (y = 0; y < ICON_H; y++)
        for (x = 0; x < ICON_W; x++)
            memcpy(icon + ((size_t)y * ICON_W + x) * 4,
                   rgba + ((size_t)(y * CG_FRAME_HEIGHT / ICON_H) * CG_FRAME_WIDTH + x * CG_FRAME_WIDTH / ICON_W) * 4, 4);
    cg_render_flatten(icon, ICON_W, ICON_H);
    if (p->type == CG_PAGE_BUFFER)
        cg_render_flatten(rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);
    if (!(moves ? cg_ilbm_write(o.f, strip, sw, sh, 32, top) :
          cg_ilbm_write(o.f, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, p->type == CG_PAGE_BUFFER ? 24 : 32, 0)) ||
        !cg_ilbm_write(o.f, icon, ICON_W, ICON_H, 24, 0))
        o.bad = 1;
    cg_play_free(play);

    form = begin(&o, "FORM");
    put(&o, "CrUD", 4);
    chunk = begin(&o, "TYPE");
    put(&o, crud_type(p->type), 4);
    put32(&o, 0);
    end(&o, chunk);
    chunk = begin(&o, "LIBS");
    put(&o, libs, sizeof(libs));
    end(&o, chunk);
    tags = begin(&o, "TAGS");
    if (p->type != CG_PAGE_BUFFER)
    {
        put32(&o, TAG_DURATION);
        put32(&o, (uint32_t)fields);
        put32(&o, TAG_SPEED);
        put32(&o, p->speed);
        put32(&o, TAG_HOLDFIELDS);      /* the Switcher freezes a scroll that has any */
        put32(&o, p->type == CG_PAGE_SCROLL && p->playback == 1);
        put32(&o, TAG_FADEOUT);
        put32(&o, 0);
        if (moves && p->playback < 0)
        {
            put32(&o, TAG_LOOP);
            put32(&o, 1);
        }
        put32(&o, TAG_PAGE);
    }
    else
        put32(&o, TAG_FRAMEPAGE);
    tag = ftell(o.f);
    put32(&o, 0);
    if (!cg_book_write(e->book, o.f, e->page))
        o.bad = 1;
    end(&o, tag);
    put32(&o, 0);
    put32(&o, 0);
    end(&o, tags);
    end(&o, form);
    free(rgba);
    free(icon);
    if (fclose(o.f) || o.bad)
    {
        *err = "Unable to save page";
        return 0;
    }
    return 1;
}

int cg_page_load(struct cg_editor *e, const char *path, const char **err)
{
    struct cg_book *src = cg_book_load(path, err);
    struct cg_page *from = NULL, *to;
    struct cg_line *l;
    int i;

    if (!src)
        return 0;
    for (i = 0; i < CG_PAGES_PER_BOOK && !from; i++)
        if (src->pages[i].used && src->pages[i].type)
            from = &src->pages[i];
    if (!from)
    {
        cg_book_free(src);
        *err = "No page in file";
        return 0;
    }
    /* the page's fonts and brushes join this book under its IDs */
    for (l = from->lines; l; l = l->next)
        for (i = 0; i < l->nchars; i++)
        {
            struct cg_attr *a = l->chars[i].attr;
            const struct cg_data *d;
            struct cg_data *n;

            if (!a || a->id == CG_BOX_ID || !(d = cg_book_find_data(src, (uint16_t)a->id)))
                continue;
            if ((n = cg_edit_add_data(e, d->type, d->filename, d->height)))
            {
                if (d->drawdata && !n->drawdata && (n->drawdata = malloc(CG_DRAWDATA_SIZE)))
                    memcpy(n->drawdata, d->drawdata, CG_DRAWDATA_SIZE);
                n->flags = d->flags;
                a->id = (int16_t)n->id;
            }
        }
    cg_edit_erase_page(e);
    to = cg_edit_page(e);
    *to = *from;
    from->lines = NULL;
    from->nlines = 0;
    cg_book_free(src);
    e->dirty = 1;
    cg_edit_set_line(e, cg_edit_topmost(e), 0);
    return 1;
}
