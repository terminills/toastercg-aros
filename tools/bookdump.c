/*
    bookdump: print a ToasterCG book - fonts, pages, lines and attributes.
*/

#include <stdio.h>

#include "cgbook.h"

static const char *data_kind(enum cg_data_type t)
{
    return t == CG_DATA_FONT ? "FONT" : t == CG_DATA_DRAW ? "DRAW" : "BRSH";
}

int main(int argc, char **argv)
{
    struct cg_book *book;
    const struct cg_data *d;
    const char *err = NULL;
    int i;

    if (argc != 2)
    {
        fprintf(stderr, "usage: bookdump file.Book\n");
        return 20;
    }
    if (!(book = cg_book_load(argv[1], &err)))
    {
        fprintf(stderr, "bookdump: %s: %s\n", argv[1], err);
        return 20;
    }

    printf("book version %c\n", book->version);
    for (d = book->data; d; d = d->next)
        printf("  %s %3u height %3u  %s\n", data_kind(d->type), d->id, d->height, d->filename);

    for (i = 0; i < CG_PAGES_PER_BOOK; i++)
    {
        const struct cg_page *p = &book->pages[i];
        const struct cg_line *l;

        if (!p->used)
            continue;
        printf("page %d: type %u mode %u background %u top #%02x%02x%02x/%u bottom #%02x%02x%02x/%u, %u lines\n",
               i, p->type, p->speed, p->background,
               p->top.r, p->top.g, p->top.b, p->top.a, p->bottom.r, p->bottom.g, p->bottom.b, p->bottom.a,
               p->nlines);
        for (l = p->lines; l; l = l->next)
        {
            uint16_t c;

            printf("  @%4d,%4d h%3u base%3u justify%u", l->xoffset, l->yoffset,
                   l->total_height, l->baseline, l->justify);
            if (cg_line_is_graphic(book, l))
                printf(" %ux%u pri%u", l->face_width, l->face_height, l->render_pri);
            printf("  \"");
            for (c = 0; c < l->nchars; c++)
            {
                uint16_t a = l->chars[c].ascii;
                putchar(a >= 32 && a < 127 ? a : (a >= 160 && a < 256 ? a : '?'));
            }
            printf("\"");
            for (c = 0; c < l->nchars; c++)
            {
                const struct cg_attr *at = l->chars[c].attr;

                if (!at)
                    continue;
                d = cg_book_find_data(book, (uint16_t)at->id);
                printf("\n      [%u] font %d (%s) face #%02x%02x%02x/%u shadow t%u l%u d%u outline t%u",
                       c, at->id, d ? d->filename : "?", at->face.r, at->face.g, at->face.b, at->face.a,
                       at->shadow_type, at->shadow_length, at->shadow_direction, at->outline_type);
            }
            putchar('\n');
        }
    }
    cg_book_free(book);
    return 0;
}
