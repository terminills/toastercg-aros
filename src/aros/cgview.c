/*
    CGView: show the pages of a ToasterCG book in a window.

    CGView BOOK/A,PAGE/N,FONTS/K,SCALE/K/N

    FONTS is the drawer that "Toaster:" in the book's font names refers to
    (default Toaster:). SCALE is in percent of the Toaster's 752x480 frame.
    Cursor left/right change the page; Esc or the close gadget quit.
*/

#include <exec/types.h>
#include <intuition/intuition.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include <stdio.h>
#include <stdlib.h>

#include "cgbook.h"
#include "cgrender.h"

#define TEMPLATE "BOOK/A,PAGE/N,FONTS/K,SCALE/K/N"
enum { ARG_BOOK, ARG_PAGE, ARG_FONTS, ARG_SCALE, ARG_COUNT };

#define RAWKEY_LEFT  0x4F
#define RAWKEY_RIGHT 0x4E
#define RAWKEY_ESC   0x45

static int next_page(const struct cg_book *book, int page, int dir)
{
    int p = page + dir;

    while (p >= 0 && p < CG_PAGES_PER_BOOK)
    {
        if (book->pages[p].used)
            return p;
        p += dir;
    }
    return page;
}

static void show(struct Window *win, struct cg_renderer *r, const struct cg_book *book,
                 int page, uint8_t *rgba, int w, int h, double scale)
{
    static char title[80];      /* Intuition keeps the pointer */

    cg_render_page(r, book, page, rgba, w, h, scale);
    WritePixelArray(rgba, 0, 0, w * 4, win->RPort, win->BorderLeft, win->BorderTop, w, h, RECTFMT_RGBA);
    snprintf(title, sizeof(title), "CGView - page %d", page);
    SetWindowTitles(win, (CONST_STRPTR)title, (CONST_STRPTR)~0);
}

int main(void)
{
    IPTR args[ARG_COUNT] = { 0 };
    struct RDArgs *rda;
    struct cg_book *book = NULL;
    struct cg_renderer *r = NULL;
    struct Window *win = NULL;
    const char *err = NULL;
    uint8_t *rgba = NULL;
    double scale = 1.0;
    int page = 0, w, h, done = 0, rc = 20;

    if (!(rda = ReadArgs((CONST_STRPTR)TEMPLATE, (SIPTR *)args, NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)"CGView");
        return 20;
    }
    if (args[ARG_PAGE])
        page = *(LONG *)args[ARG_PAGE];
    if (args[ARG_SCALE] && *(LONG *)args[ARG_SCALE] > 0)
        scale = *(LONG *)args[ARG_SCALE] / 100.0;

    if (!(book = cg_book_load((const char *)args[ARG_BOOK], &err)))
    {
        printf("CGView: %s: %s\n", (const char *)args[ARG_BOOK], err);
        goto out;
    }
    if (page < 0 || page >= CG_PAGES_PER_BOOK || !book->pages[page].used)
        page = next_page(book, -1, 1);
    if (!(r = cg_render_new(args[ARG_FONTS] ? (const char *)args[ARG_FONTS] : "Toaster:")))
        goto out;

    w = (int)(CG_FRAME_WIDTH * scale);
    h = (int)(CG_FRAME_HEIGHT * scale);
    if (!(rgba = malloc((size_t)w * h * 4)))
        goto out;
    if (!(win = OpenWindowTags(NULL,
            WA_Title, (IPTR)"CGView",
            WA_InnerWidth, w, WA_InnerHeight, h,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
            WA_Activate, TRUE, WA_RMBTrap, TRUE,
            WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_REFRESHWINDOW,
            WA_SimpleRefresh, TRUE,
            TAG_DONE)))
        goto out;

    show(win, r, book, page, rgba, w, h, scale);
    while (!done)
    {
        struct IntuiMessage *msg;

        WaitPort(win->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)))
        {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;

            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_RAWKEY && code == RAWKEY_ESC))
                done = 1;
            else if (cls == IDCMP_RAWKEY && (code == RAWKEY_LEFT || code == RAWKEY_RIGHT))
            {
                int np = next_page(book, page, code == RAWKEY_LEFT ? -1 : 1);

                if (np != page)
                {
                    page = np;
                    show(win, r, book, page, rgba, w, h, scale);
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW)
            {
                BeginRefresh(win);
                WritePixelArray(rgba, 0, 0, w * 4, win->RPort, win->BorderLeft, win->BorderTop, w, h, RECTFMT_RGBA);
                EndRefresh(win, TRUE);
            }
        }
    }
    rc = 0;

out:
    if (win)
        CloseWindow(win);
    free(rgba);
    cg_render_free(r);
    cg_book_free(book);
    FreeArgs(rda);
    return rc;
}
