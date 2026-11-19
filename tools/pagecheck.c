/*
    pagecheck: a page file written by Save Page against the page of the
    book it was saved from. The picture at its head must be the page: a
    key with its alpha, a frame flattened, a scroll or crawl the strip
    the player moves; the TYPE must be the page's, and the Duration tag
    the fields the player takes for it.

    usage: pagecheck file book page fontroot
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgilbm.h"
#include "cgplay.h"

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* the value of a tag in the file's CrUD, -1 without it */
static long tag(const uint8_t *d, long len, uint32_t id, char *type)
{
    long i, at = 0;

    while (at + 12 <= len && memcmp(d + at + 8, "CrUD", 4))
        at += 8 + (long)((be32(d + at + 4) + 1) & ~1u);
    if (at + 12 > len)
        return -1;
    len = at + 8 + (long)be32(d + at + 4);
    for (at += 12; at + 8 <= len; at += 8 + (long)((be32(d + at + 4) + 1) & ~1u))
    {
        if (!memcmp(d + at, "TYPE", 4))
            memcpy(type, d + at + 8, 4);
        if (memcmp(d + at, "TAGS", 4))
            continue;
        for (i = at + 8; i + 8 <= at + 8 + (long)be32(d + at + 4); i += 8)
        {
            uint32_t t = be32(d + i);

            if (t == id)
                return (long)be32(d + i + 4);
            if (!t || !(t & 0x80000000))    /* the book: nothing of ours after it */
                break;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    static const char *types[] = { "", "KEYF", "FRAM", "SCRO", "CRAW" };
    struct cg_image *img;
    struct cg_book *book, *one;
    struct cg_renderer *r;
    struct cg_play *play;
    struct cg_page *pg;
    const char *err = NULL;
    const uint8_t *want;
    uint8_t *rgba, *file;
    char type[5] = "";
    long len, i, bad = 0, fields, hold, loop;
    int page, w = CG_FRAME_WIDTH, h = CG_FRAME_HEIGHT, top;
    FILE *f;

    if (argc != 5)
    {
        fprintf(stderr, "usage: pagecheck file book page fontroot\n");
        return 20;
    }
    page = atoi(argv[3]);
    if (!(img = cg_ilbm_load(argv[1], &err)) || !(book = cg_book_load(argv[2], &err)) ||
        !(one = cg_book_load(argv[1], &err)) || !(r = cg_render_new(argv[4])) || !(f = fopen(argv[1], "rb")) ||
        !(rgba = calloc((size_t)w * h, 4)) || !(file = malloc(1 << 24)) || !(play = cg_play_new(r, book, page, 100)))
    {
        fprintf(stderr, "pagecheck: %s\n", err ? err : "cannot start");
        return 20;
    }
    len = (long)fread(file, 1, 1 << 24, f);
    fclose(f);
    pg = &book->pages[page];
    fields = tag(file, len, 0x8000006c, type);
    hold = tag(file, len, 0x80000087, type);
    loop = tag(file, len, 0x80f70003, type);
    /* the file's book holds the one page, at its place in the book */
    if (pg->type < 1 || pg->type > 4 || strcmp(type, types[pg->type]) || one->pages[page].type != pg->type)
    {
        fprintf(stderr, "pagecheck: type %s for a page of type %d\n", type, pg->type);
        return 10;
    }
    if (pg->type == CG_PAGE_SCROLL || pg->type == CG_PAGE_CRAWL)
    {
        struct cg_play *q = cg_play_new(r, book, page, 100);
        long n = 0;

        want = cg_play_strip(play, &w, &h, &top);
        /* played once: the fields the player steps through before the page is over */
        if (q && !pg->playback)
            for (n = 1; n < 1000000 && cg_play_skip(q, 1); n++)
                ;
        if (!want || !q || fields != cg_play_fields(play) || (!pg->playback && n != fields) ||
            hold != (pg->type == CG_PAGE_SCROLL && pg->playback == 1) || (loop == 1) != (pg->playback < 0))
        {
            fprintf(stderr, "pagecheck: %ld fields in the file, %ld played, hold %ld, loop %ld\n", fields, n, hold, loop);
            return 10;
        }
        cg_play_free(q);
    }
    else
    {
        cg_render_page(r, book, page, rgba, w, h, 1.0);
        if (pg->type == CG_PAGE_BUFFER)
            cg_render_flatten(rgba, w, h);
        want = rgba;
    }
    if (img->w != w || img->h != h)
    {
        fprintf(stderr, "pagecheck: %dx%d, not %dx%d\n", img->w, img->h, w, h);
        return 10;
    }
    for (i = 0; i < (long)w * h; i++)
        bad += memcmp(want + i * 4, img->rgba + i * 4, 4) != 0;
    printf("%s: %s %dx%d, %ld fields, %ld pixels differ\n", argv[1], type, w, h, fields, bad);
    cg_play_free(play);
    cg_image_free(img);
    cg_book_free(book);
    cg_book_free(one);
    cg_render_free(r);
    free(rgba);
    free(file);
    return bad ? 10 : 0;
}
