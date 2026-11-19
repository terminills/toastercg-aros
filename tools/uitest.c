/*
    uitest: drive the CG interface on the host from a script and save
    what it shows, for checking the interface without a machine.

    usage: uitest fontroot script [book]

    KEY raw [ascii [qual]]   an Amiga raw key (hex), e.g. KEY 44 for Return
    TYPE text                characters, as typed
    CLICK x y [qual]         left button down and up at frame x,y
    DRAG x0 y0 x1 y1 [qual]  press, move, release
    PRESS x y [qual]         left button down (a popup stays open)
    RELEASE x y [qual]       left button up
    ANSWER text              the next file, number or string requester's answer
    SHOT out.png             the window as it is now
    SCALE percent            the interface at this size (100 = the Toaster's)
    SAVEBOOK file            the book as it is now
    CHECKBOOK                the loaded book's missing fonts and brushes offered
                             for replacement (answers from ANSWER)
    VECTOR 0|1               imagery and text from the SVG redraws (1, the
                             default) or the original pixels (0)
*/

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgart.h"
#include "cgui.h"
#include "canvas.h"

static char answers[16][256];
static int nanswers, next_answer;

static const char *answer(void)
{
    return next_answer < nanswers ? answers[next_answer++] : NULL;
}

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

static int file(void *ud, const char *title, char *f, size_t flen, char *dir, size_t dlen, int save)
{
    const char *a = answer();
    const char *slash;

    (void)ud;
    printf("  [file %s%s] -> %s\n", title, save ? " (save)" : "", a ? a : "cancel");
    if (!a)
        return 0;
    slash = strrchr(a, '/');
    snprintf(dir, dlen, "%.*s", slash ? (int)(slash - a) : 0, a);
    snprintf(f, flen, "%s", slash ? slash + 1 : a);
    return 1;
}

static int string(void *ud, const char *prompt, char *buf, size_t len)
{
    const char *a = answer();

    (void)ud;
    printf("  [string %s] -> %s\n", prompt, a ? a : "cancel");
    if (!a)
        return 0;
    snprintf(buf, len, "%s", a);
    return 1;
}

static int number(void *ud, const char *prompt, long *v)
{
    const char *a = answer();

    (void)ud;
    printf("  [number %s] -> %s\n", prompt, a ? a : "cancel");
    if (!a)
        return 0;
    *v = atol(a);
    return 1;
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
    struct cg_host host = { NULL, request, file, string, number, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    char line[1024];
    struct cg_renderer *r;
    struct cg_editor *e;
    struct cg_book *book;
    struct cg_cmd c;
    struct cg_ui *u;
    unsigned ms = 1000;
    const char *err;
    int scale = 100;
    FILE *f;

    if (argc < 3)
    {
        fprintf(stderr, "usage: uitest fontroot script [book]\n");
        return 20;
    }
    {
        /* the interface art: from the Toaster files under fontroot/iso, kept
           between runs (CG_ART names the file) */
        const char *keep[2] = { getenv("CG_ART") ? getenv("CG_ART") : "build/host/CG.art", NULL };
        char toaster[1024], why[1024];

        snprintf(toaster, sizeof(toaster), "%s/iso", argv[1]);
        if (!cg_art_load(toaster, keep, NULL, NULL, why, sizeof(why)))
        {
            fprintf(stderr, "uitest: %s\n", why);
            return 20;
        }
    }
    book = argc > 3 ? cg_book_load(argv[3], &err) : calloc(1, sizeof(*book));
    if (!book || !(f = fopen(argv[2], "r")) || !(r = cg_render_new(argv[1])))
        return 20;
    book->version = '4';
    e = cg_edit_new(book, r);
    cg_cmd_init(&c, e, &host);
    u = cg_ui_new(e, &c, &host, scale);
    while (fgets(line, sizeof(line), f))
    {
        char *s = line + strlen(line), arg[256] = "";
        int a = 0, b = 0, x1 = 0, y1 = 0, q = 0, res = 0;

        while (s > line && (s[-1] == '\n' || s[-1] == '\r'))
            *--s = 0;
        if (!line[0] || line[0] == '#')
            continue;
        ms += 1000;
        if (sscanf(line, "KEY %x %d %d", &a, &b, &q) >= 1)
            res = cg_ui_key(u, a, b, (unsigned)q);
        else if (!strncmp(line, "TYPE ", 5))
            for (s = line + 5; *s; s++)
                res |= cg_ui_key(u, 0x20, (unsigned char)*s, 0);
        else if (sscanf(line, "CLICK %d %d %d", &a, &b, &q) >= 2)
        {
            res = cg_ui_mouse(u, CG_MOUSE_DOWN, cg_edge(a, scale), cg_edge(b, scale), (unsigned)q, ms);
            res |= cg_ui_mouse(u, CG_MOUSE_UP, cg_edge(a, scale), cg_edge(b, scale), (unsigned)q, ms + 50);
        }
        else if (sscanf(line, "PRESS %d %d %d", &a, &b, &q) >= 2)
            res = cg_ui_mouse(u, CG_MOUSE_DOWN, cg_edge(a, scale), cg_edge(b, scale), (unsigned)q, ms);
        else if (sscanf(line, "RELEASE %d %d %d", &a, &b, &q) >= 2)
            res = cg_ui_mouse(u, CG_MOUSE_UP, cg_edge(a, scale), cg_edge(b, scale), (unsigned)q, ms);
        else if (sscanf(line, "DRAG %d %d %d %d %d", &a, &b, &x1, &y1, &q) >= 4)
        {
            int i;

            res = cg_ui_mouse(u, CG_MOUSE_DOWN, cg_edge(a, scale), cg_edge(b, scale), (unsigned)q, ms);
            for (i = 1; i <= 8; i++)
                cg_ui_mouse(u, CG_MOUSE_MOVE, cg_edge(a + (x1 - a) * i / 8, scale), cg_edge(b + (y1 - b) * i / 8, scale),
                            (unsigned)q, ms + 10 * (unsigned)i);
            res |= cg_ui_mouse(u, CG_MOUSE_UP, cg_edge(x1, scale), cg_edge(y1, scale), (unsigned)q, ms + 100);
        }
        else if (sscanf(line, "ANSWER %255[^\n]", arg) == 1 && nanswers < 16)
            snprintf(answers[nanswers++], sizeof(answers[0]), "%s", arg);
        else if (sscanf(line, "SAVEBOOK %255s", arg) == 1)
            printf("  saved %s: %d\n", arg, cg_book_save(e->book, arg, &err));
        else if (!strcmp(line, "CHECKBOOK"))
        {
            printf("  replaced %d\n", cg_cmd_check_book(&c));
            cg_ui_changed(u);
        }
        else if (sscanf(line, "VECTOR %d", &a) == 1)
            pc_set_vector(a);
        else if (sscanf(line, "SCALE %d", &a) == 1)
        {
            scale = a;
            cg_ui_set_scale(u, scale);
        }
        else if (sscanf(line, "SHOT %255s", arg) == 1)
        {
            int w, h;
            uint8_t *rgba;

            cg_ui_size(u, &w, &h);
            rgba = calloc((size_t)w * h, 4);
            cg_ui_draw(u, rgba);
            write_png(arg, rgba, w, h);
            free(rgba);
            printf("shot %s (%dx%d)\n", arg, w, h);
        }
        else
            printf("?? %s\n", line);
        if (res & CG_UI_QUIT)
            printf("  -> quit\n");
        if (res & CG_UI_TAKE)
            printf("  -> take\n");
    }
    fclose(f);
    cg_ui_free(u);
    cg_edit_free(e);
    cg_book_free(book);
    cg_render_free(r);
    cg_art_free();
    return 0;
}
