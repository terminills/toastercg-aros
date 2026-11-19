/*
    cgscript: run CG_AREXX functions from a file against a book, for
    testing the editor on the host.

    usage: cgscript fontroot script [book]

    Script lines are function calls (SET_PAGE 3, MAKE_LINE "Hello") plus:
      EXPECT text       the last result must equal text
      ANSWER text       the next file or string requester's answer; for a file
                        requester "@offered" answers with the drawer offered
      EXPECTRC rc err   the last call's RC and secondary code (rm_Result2);
                        a call expecting an RC of 10 is not counted a failure
      RENDER out.png    the current page as a PNG
      SAVE out.book     the book
    '#' starts a comment.
*/

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgcmd.h"
#include "cgpage.h"
#include "cgplay.h"

static int request(void *ud, const char *const *lines, int n, int okcancel)
{
    int i;

    (void)ud;
    printf("  [request%s]", okcancel ? " ok/cancel" : "");
    for (i = 0; i < n; i++)
        printf(" %s", lines[i]);
    printf("\n");
    return 1;
}

static char answers[16][256];
static int nanswers, next_answer;

static int file(void *ud, const char *title, char *f, size_t flen, char *dir, size_t dlen, int save)
{
    const char *a = next_answer < nanswers ? answers[next_answer++] : NULL, *slash;

    (void)ud;
    printf("  [file %s%s, offered \"%s\"] -> %s\n", title, save ? " (save)" : "", dir, a ? a : "cancel");
    if (!a)
        return 0;
    if (!strcmp(a, "@offered"))
    {
        snprintf(f, flen, "offered");
        return 1;
    }
    slash = strrchr(a, '/');
    snprintf(dir, dlen, "%.*s", slash ? (int)(slash - a) : 0, a);
    snprintf(f, flen, "%s", slash ? slash + 1 : a);
    return 1;
}

static int string(void *ud, const char *prompt, char *buf, size_t len)
{
    const char *a = next_answer < nanswers ? answers[next_answer++] : NULL;

    (void)ud;
    printf("  [string %s] -> %s\n", prompt, a ? a : "cancel");
    if (!a)
        return 0;
    snprintf(buf, len, "%s", a);
    return 1;
}

static int macro(void *ud, const char *name)
{
    (void)ud;
    printf("  [macro] %s\n", name);
    return 1;
}

static void status(void *ud, const char *m)
{
    (void)ud;
    printf("  [status] %s\n", m);
}

static int write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    png_structp p;
    png_infop in;
    int y;

    if (!f)
        return 0;
    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    in = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, in, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, in);
    for (y = 0; y < h; y++)
        png_write_row(p, rgba + (size_t)y * w * 4);
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &in);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    struct cg_host host = { NULL, request, file, string, NULL, status, macro, NULL, NULL, NULL, NULL, NULL, NULL };
    char line[1024], result[CG_RESULT_MAX] = "";
    struct cg_renderer *r;
    struct cg_editor *e;
    struct cg_book *book;
    struct cg_cmd c;
    const char *err;
    int lineno = 0, fails = 0, rc = 0, pending = 0;
    FILE *f;

    if (argc < 3)
    {
        fprintf(stderr, "usage: cgscript fontroot script [book]\n");
        return 20;
    }
    book = argc > 3 ? cg_book_load(argv[3], &err) : calloc(1, sizeof(*book));
    if (!book || !(f = fopen(argv[2], "r")) || !(r = cg_render_new(argv[1])))
    {
        fprintf(stderr, "cgscript: cannot start\n");
        return 20;
    }
    book->version = '4';
    e = cg_edit_new(book, r);
    cg_cmd_init(&c, e, &host);
    while (fgets(line, sizeof(line), f))
    {
        char *s = line + strlen(line);

        lineno++;
        while (s > line && (s[-1] == '\n' || s[-1] == '\r'))
            *--s = 0;
        for (s = line; *s == ' ' || *s == '\t'; s++)
            ;
        if (!*s || *s == '#')
            continue;
        if (!strncmp(s, "ANSWER ", 7))
        {
            if (nanswers < 16)
                snprintf(answers[nanswers++], sizeof(answers[0]), "%s", s + 7);
            continue;
        }
        if (!strncmp(s, "EXPECTRC ", 9))
        {
            int want_rc = 0, want_err = 0;

            sscanf(s + 9, "%d %d", &want_rc, &want_err);
            if (rc != want_rc || c.err != want_err)
            {
                printf("%d: FAIL expected rc %d err %d, got rc %d err %d\n", lineno, want_rc, want_err, rc, c.err);
                fails++;
            }
            pending = 0;
            continue;
        }
        if (pending)
            fails++;
        pending = 0;
        if (!strncmp(s, "EXPECT ", 7))
        {
            if (strcmp(result, s + 7))
            {
                printf("%d: FAIL expected \"%s\", got \"%s\"\n", lineno, s + 7, result);
                fails++;
            }
            continue;
        }
        if (!strncmp(s, "RENDER ", 7))
        {
            uint8_t *rgba = calloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT, 4);

            cg_render_page(r, book, e->page, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, 1.0);
            cg_render_flatten(rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);
            write_png(s + 7, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);
            free(rgba);
            continue;
        }
        if (!strncmp(s, "PLAY ", 5))
        {
            int fields = 0, every = 1, n, i;
            char prefix[512];

            if (sscanf(s + 5, "%d %d %511s", &fields, &every, prefix) == 3)
            {
                struct cg_play *pl = cg_play_new(r, book, e->page, 100);
                uint8_t *rgba = calloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT, 4);

                for (i = n = 0; pl && rgba && i < fields; i++)
                {
                    int more = cg_play_field(pl, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);

                    if (i % every == 0 || !more)
                    {
                        char name[600];

                        snprintf(name, sizeof(name), "%s%03d.png", prefix, n++);
                        write_png(name, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT);
                    }
                    if (!more)
                        break;
                }
                printf("PLAY %d fields, %d shots%s\n", i, n, i < fields ? " (ended)" : "");
                free(rgba);
                cg_play_free(pl);
            }
            continue;
        }
        if (!strncmp(s, "SAVE ", 5))
        {
            if (!cg_book_save(book, s + 5, &err))
            {
                printf("%d: FAIL save: %s\n", lineno, err);
                fails++;
            }
            continue;
        }
        if (!strncmp(s, "SAVEPAGE ", 9))
        {
            if (!cg_page_save(e, s + 9, &err))
            {
                printf("%d: FAIL savepage: %s\n", lineno, err);
                fails++;
            }
            continue;
        }
        rc = cg_cmd_line(&c, s, result);
        printf("%-40s rc %2d  \"%s\"%s%s\n", s, rc, result, e->message ? "  msg: " : "",
               e->message ? e->message : "");
        pending = rc >= CG_RC_ERROR;
    }
    if (pending)
        fails++;
    fclose(f);
    printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
    cg_edit_free(e);
    cg_book_free(book);
    cg_render_free(r);
    return fails ? 10 : 0;
}
