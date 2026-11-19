/*
    fuzzload: the loaders against damaged files. Each round mutates a real
    file (bit flips, magic values, huge lengths, truncation, a region
    copied over another), loads it as the CG would and renders what
    loaded. Built with the sanitizers, any memory error is a failure.

    usage: fuzzload book|page|ilbm|tfont|picture file rounds seed fontroot [book]
           fuzzload tfontdraw file rounds seed fontroot book name

    page loads into page 30 of book (a page file needs a book to go into).
    tfontdraw draws the pages of book that use the font called name (as
    the book has it, under Toaster:) with the damaged font in its place:
    a font root is made in build/fuzz/root linking fontroot's drawers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "cgbook.h"
#include "cgedit.h"
#include "cgilbm.h"
#include "cgpage.h"
#include "cgrender.h"
#include "cgtfont.h"

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;

    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) ||
        !(b = malloc((size_t)n + 1)) || fread(b, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "fuzzload: cannot read %s\n", path);
        exit(20);
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static size_t mutate(uint8_t *b, size_t len, size_t cap)
{
    int k, n = 1 + rand() % 4;
    static const uint8_t magic[] = { 0x00, 0xff, 0x7f, 0x80, 0x01, 0xfe };

    for (k = 0; k < n && len; k++)
        switch (rand() % 6)
        {
        case 0: /* bit flips */
            b[rand() % len] ^= (uint8_t)(1 << (rand() % 8));
            break;
        case 1: /* a magic byte */
            b[rand() % len] = magic[rand() % sizeof(magic)];
            break;
        case 2: /* a huge or negative 32-bit length */
            if (len >= 4)
            {
                size_t at = (size_t)rand() % (len - 3);
                uint32_t v = rand() % 2 ? 0x7fffffffu : (uint32_t)rand() * 2654435761u;

                b[at] = (uint8_t)(v >> 24);
                b[at + 1] = (uint8_t)(v >> 16);
                b[at + 2] = (uint8_t)(v >> 8);
                b[at + 3] = (uint8_t)v;
            }
            break;
        case 3: /* truncation */
            len = (size_t)rand() % len;
            break;
        case 4: /* a region copied over another */
        {
            size_t a = (size_t)rand() % len, c = (size_t)rand() % len, m = 1 + (size_t)rand() % 64;

            if (a + m > len)
                m = len - a;
            if (c + m > len)
                m = len - c;
            memmove(b + c, b + a, m);
            break;
        }
        case 5: /* bytes inserted */
            if (len + 16 <= cap)
            {
                size_t at = (size_t)rand() % len;

                memmove(b + at + 16, b + at, len - at);
                for (int i = 0; i < 16; i++)
                    b[at + i] = (uint8_t)rand();
                len += 16;
            }
            break;
        }
    return len;
}

/* the pages of book with a character in font data id */
static int uses_font(const struct cg_page *pg, int id)
{
    const struct cg_line *l;
    int i;

    for (l = pg->lines; l; l = l->next)
        for (i = 0; i < l->nchars; i++)
            if (l->chars[i].attr && l->chars[i].attr->id == id)
                return 1;
    return 0;
}

static void render_some(struct cg_renderer *r, const struct cg_book *book)
{
    static uint8_t rgba[CG_FRAME_WIDTH / 2 * (CG_FRAME_HEIGHT / 2) * 4];
    int p, done = 0;

    for (p = 0; p < CG_PAGES_PER_BOOK && done < 3; p++)
        if (book->pages[p].lines)
        {
            cg_render_page(r, book, p, rgba, CG_FRAME_WIDTH / 2, CG_FRAME_HEIGHT / 2, 0.5);
            done++;
        }
}

int main(int argc, char **argv)
{
    const char *kind, *err, *tmp = "build/fuzz/case";
    struct cg_renderer *r;
    struct cg_book *base = NULL;
    uint8_t *orig, *b;
    size_t len, cap;
    int rounds, i, loaded = 0;

    if (argc < 6)
    {
        fprintf(stderr, "usage: fuzzload book|page|ilbm|tfont|picture file rounds seed fontroot [book]\n");
        return 20;
    }
    kind = argv[1];
    orig = read_all(argv[2], &len);
    rounds = atoi(argv[3]);
    srand((unsigned)atoi(argv[4]));
    if (!(r = cg_render_new(argv[5])))
        return 20;
    if ((!strcmp(kind, "page") || !strcmp(kind, "tfontdraw")) && (argc < 7 || !(base = cg_book_load(argv[6], &err))))
    {
        fprintf(stderr, "fuzzload: page needs a book\n");
        return 20;
    }
    cap = len + 64 * 16;
    b = malloc(cap);
    for (i = 0; i < rounds; i++)
    {
        size_t n;
        FILE *f;

        memcpy(b, orig, len);
        n = mutate(b, len, cap);
        if (!(f = fopen(tmp, "wb")) || fwrite(b, 1, n, f) != n)
            return 20;
        fclose(f);
        err = NULL;
        if (!strcmp(kind, "book"))
        {
            struct cg_book *bk = cg_book_parse(b, n, &err);

            if (bk)
            {
                loaded++;
                render_some(r, bk);
                cg_book_free(bk);
            }
        }
        else if (!strcmp(kind, "page"))
        {
            struct cg_editor *e = cg_edit_new(base, r);

            cg_edit_goto_page(e, 30);
            if (cg_page_load(e, tmp, &err))
            {
                loaded++;
                render_some(r, base);
            }
            cg_edit_erase_page(e);
            cg_edit_free(e);
        }
        else if (!strcmp(kind, "ilbm"))
        {
            struct cg_image *img = cg_ilbm_load(tmp, &err);

            if (img)
            {
                loaded++;
                cg_image_free(img);
            }
        }
        else if (!strcmp(kind, "tfont"))
        {
            struct cg_tfont *tf = cg_tfont_load(tmp, &err);

            if (tf)
            {
                loaded++;
                cg_tfont_free(tf);
            }
        }
        else if (!strcmp(kind, "tfontdraw"))
        {
            /* name is Toaster:<rel>: the font root gets <rel> as the damaged
               file and every other top drawer of fontroot as a link */
            static char rel[512], dir[600], target[700];
            static int made, id = -1;
            static uint8_t rgba[CG_FRAME_WIDTH / 2 * (CG_FRAME_HEIGHT / 2) * 4];
            struct cg_renderer *fr;
            const struct cg_data *d;
            int p;

            if (!made)
            {
                const char *colon = strchr(argv[7], ':');
                char *s, *slash;

                snprintf(rel, sizeof(rel), "%s", colon ? colon + 1 : argv[7]);
                for (d = base->data; d; d = d->next)
                    if (d->filename && !strcasecmp(d->filename, argv[7]))
                        id = d->id;
                if (id < 0)
                {
                    fprintf(stderr, "fuzzload: book has no font %s\n", argv[7]);
                    return 20;
                }
                /* root/<first drawer of rel>/<...> made real, the rest linked */
                if (system("rm -rf build/fuzz/root && mkdir -p build/fuzz/root"))
                    return 20;
                snprintf(dir, sizeof(dir), "build/fuzz/root/%s", rel);
                if ((slash = strrchr(dir, '/')))
                    *slash = 0;
                for (s = dir + strlen("build/fuzz/root/"); (s = strchr(s, '/')); s++)
                {
                    *s = 0;
                    mkdir(dir, 0755);
                    *s = '/';
                }
                mkdir(dir, 0755);
                snprintf(target, sizeof(target), "build/fuzz/root/%s", rel);
                made = 1;
            }
            if (rename(tmp, target))
                return 20;
            if (!(fr = cg_render_new("build/fuzz/root")))
                return 20;
            for (p = 0; p < CG_PAGES_PER_BOOK; p++)
                if (uses_font(&base->pages[p], id))
                {
                    cg_render_page(fr, base, p, rgba, CG_FRAME_WIDTH / 2, CG_FRAME_HEIGHT / 2, 0.5);
                    loaded++;
                }
            cg_render_free(fr);
        }
        else if (!strcmp(kind, "picture"))
        {
            if (cg_render_set_picture(r, tmp))
                loaded++;
            cg_render_set_picture(r, NULL);
        }
        else
            return 20;
    }
    printf("%s %s: %d rounds, %d loaded\n", kind, argv[2], rounds, loaded);
    free(b);
    free(orig);
    if (base)
        cg_book_free(base);
    cg_render_free(r);
    return 0;
}
