/*
    ToasterCG for AROS: the CG interface in a window scaled to the
    screen, reqtools requesters, the CG_AREXX port and Take.
    With the Video Toaster Switcher for AROS running, Take goes on its
    Program and the Switcher gadget goes to it, the CG staying loaded.

    CG [BOOK] [TOASTER dir] [SCALE percent] [CONFIG file]

    TOASTER is the Video Toaster's drawer, where "Toaster:" in books
    points (default Toaster:). The panel is drawn from its CG program
    and interface font, once: redrawn it is kept in CG.art beside the CG
    (or in T: when that cannot be written). CONFIG is the cg-config file
    (default PROGDIR:cg-config, else the Toaster's own in
    Programs/CG_Support). SCALE is the window's size against the
    Toaster's 752x480 frame, 100 or more in steps of 25; by default the
    largest that fits the screen.

    From Workbench the same are the icon's tool types (TOASTER=, SCALE=,
    CONFIG=), and a book icon given with CG's (shift-click, or a book
    whose default tool is CG) is opened.
*/

#include <exec/ports.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <libraries/reqtools.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/keymap.h>
#include <proto/reqtools.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <proto/icon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cgart.h"
#include "cgui.h"
#include "cgpage.h"
#include "cgplay.h"
#include "version.h"

#define PORT_NAME "CG_AREXX"

static const char version[] __attribute__((used)) = "$VER: CG " CG_VERSTRING " (" CG_DATE ") ToasterCG for AROS";

struct ReqToolsBase *ReqToolsBase;

static struct Window *win;
static struct MsgPort *rexx_port;
static struct cg_editor *editor;
static struct cg_cmd cmd;
static struct cg_ui *ui;
static struct cg_renderer *renderer;
static uint8_t *rgba;
static int width, height, percent;
static int macros_out;
static char status_text[80];
static int from_wb;
static const char *toaster;

/* a name in the Toaster's drawer */
static void in_toaster(char *buf, size_t len, const char *name)
{
    snprintf(buf, len, "%s%s%s", toaster, toaster[0] && strchr(":/", toaster[strlen(toaster) - 1]) ? "" : "/", name);
}

/* whether a file or drawer is there, without a request for its volume */
static int exists(const char *name)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    BPTR l;

    me->pr_WindowPtr = (APTR)-1;
    l = Lock((CONST_STRPTR)name, SHARED_LOCK);
    me->pr_WindowPtr = old;
    if (l)
        UnLock(l);
    return l != BNULL;
}

/* a message for the user: the shell, or a requester from Workbench */
static void tell(const char *m)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (CONST_STRPTR)"ToasterCG", (CONST_STRPTR)"%s",
                             (CONST_STRPTR)"OK" };

    if (!from_wb)
    {
        PutStr((CONST_STRPTR)"CG: ");
        PutStr((CONST_STRPTR)m);
        PutStr((CONST_STRPTR)"\n");
    }
    else
        EasyRequest(NULL, &es, NULL, (IPTR)m);
}

static void show(void)
{
    cg_ui_draw(ui, rgba);
    WritePixelArray(rgba, 0, 0, width * 4, win->RPort, win->BorderLeft, win->BorderTop, width, height,
                    RECTFMT_RGBA);
}

/************************************************************ requesters */

static int request(void *ud, const char *const *lines, int n, int okcancel)
{
    char body[512] = "";
    int i;

    (void)ud;
    for (i = 0; i < n; i++)
    {
        if (i)
            strncat(body, "\n", sizeof(body) - strlen(body) - 1);
        strncat(body, lines[i], sizeof(body) - strlen(body) - 1);
    }
    /* rtEZRequest treats the text as a format */
    for (i = 0; body[i]; i++)
        if (body[i] == '%')
            body[i] = ' ';
    return rtEZRequestTags(body, okcancel ? "OK|Cancel" : "Continue", NULL, NULL, RT_Window, (IPTR)win,
                           RT_ReqPos, REQPOS_CENTERWIN, RTEZ_ReqTitle, (IPTR)"ToasterCG", TAG_END) != 0;
}

static int file(void *ud, const char *title, char *f, size_t flen, char *dir, size_t dlen, int save)
{
    struct rtFileRequester *req;
    char buf[108];
    int ok = 0;

    (void)ud;
    if (!(req = rtAllocRequestA(RT_FILEREQ, NULL)))
        return 0;
    rtChangeReqAttr(req, RTFI_Dir, (IPTR)dir, TAG_END);
    snprintf(buf, sizeof(buf), "%s", f);
    if (rtFileRequest(req, buf, (char *)title, RT_Window, (IPTR)win, RT_ReqPos, REQPOS_CENTERWIN,
                      RTFI_Flags, save ? FREQF_SAVE : 0, TAG_END))
    {
        snprintf(f, flen, "%s", buf);
        snprintf(dir, dlen, "%s", req->Dir ? req->Dir : "");
        ok = 1;
    }
    rtFreeRequest(req);
    return ok;
}

static int string(void *ud, const char *prompt, char *buf, size_t len)
{
    (void)ud;
    return rtGetString((UBYTE *)buf, len - 1, "ToasterCG", NULL, RT_Window, (IPTR)win, RT_ReqPos, REQPOS_CENTERWIN,
                       RTGS_TextFmt, (IPTR)prompt, TAG_END) != 0;
}

static int number(void *ud, const char *prompt, long *value)
{
    ULONG v = (ULONG)*value;

    (void)ud;
    if (!rtGetLong(&v, "ToasterCG", NULL, RT_Window, (IPTR)win, RT_ReqPos, REQPOS_CENTERWIN, RTGL_TextFmt,
                   (IPTR)prompt, TAG_END))
        return 0;
    *value = (long)v;
    return 1;
}

static void status(void *ud, const char *m)
{
    (void)ud;
    snprintf(status_text, sizeof(status_text), "%s", m);
    cg_ui_status(ui, status_text);
}

/* a macro goes to the ARexx server as a command; its reply comes back to
   our port */
/* while the CG runs, ToasterCG: is its drawer (unless the name is taken):
   a name without spaces for macros in it, since the interpreter takes the
   first word of a command as the macro ("RAM Disk:..." would be "RAM") */
static int own_assign;

static int macro(void *ud, const char *name)
{
    struct MsgPort *rexx;
    struct RexxMsg *rm;
    char full[600], file[300];

    (void)ud;
    /* the Toaster's macros are named in REXX:CG/, where its installer
       copies them: without the copy they are run from the Toaster's
       drawer */
    if (!strncasecmp(name, "REXX:CG/", 8))
    {
        snprintf(file, sizeof(file), "%.*s", (int)strcspn(name, " "), name);
        if (!exists(file))
        {
            in_toaster(file, sizeof(file), "ARexx/CG/");
            snprintf(full, sizeof(full), "%s%s", file, name + 8);
            snprintf(file, sizeof(file), "%.*s", (int)strcspn(full, " "), full);
            if (exists(file))
                name = full;
        }
    }
    /* the interpreter runs elsewhere: PROGDIR: is the CG's drawer by name */
    if (!strncasecmp(name, "PROGDIR:", 8))
    {
        if (own_assign)
        {
            snprintf(full, sizeof(full), "ToasterCG:%s", name + 8);
            name = full;
        }
        else if (NameFromLock(GetProgramDir(), (STRPTR)full, sizeof(full)) &&
                 AddPart((STRPTR)full, (CONST_STRPTR)name + 8, sizeof(full)))
            name = full;
    }
    if (!rexx_port || !(rm = CreateRexxMsg(rexx_port, (UBYTE *)"rexx", (UBYTE *)PORT_NAME)))
        return 0;
    /* no I/O, as the Toaster's ARexxMacro: a macro gets no console */
    rm->rm_Action = RXCOMM | RXFF_NOIO;
    if (!(rm->rm_Args[0] = (IPTR)CreateArgstring((STRPTR)name, strlen(name))))
    {
        DeleteRexxMsg(rm);
        return 0;
    }
    Forbid();
    if ((rexx = FindPort((CONST_STRPTR)"REXX")))
        PutMsg(rexx, &rm->rm_Node);
    Permit();
    if (!rexx)
    {
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
        return 0;
    }
    macros_out++;
    return 1;
}

static int load_book(void *ud, const char *path)
{
    const char *err;
    struct cg_book *old = editor->book, *b = cg_book_load(path, &err);

    (void)ud;
    if (!b)
        return 0;
    /* a page file is one page: it goes onto the current page instead */
    {
        int i, pages = 0;

        for (i = 0; i < CG_PAGES_PER_BOOK; i++)
            pages += b->pages[i].used;
        if (pages == 1 && strncmp((const char *)path + strlen(path) - 5, ".Book", 5) &&
            strncmp((const char *)path + strlen(path) - 5, ".book", 5))
        {
            cg_book_free(b);
            return 0;
        }
    }
    cg_edit_set_book(editor, b);
    cg_book_free(old);
    cg_cmd_check_book(&cmd);
    cg_ui_changed(ui);
    return 1;
}

static int load_background(void *ud, const char *path)
{
    (void)ud;
    return cg_render_set_picture(renderer, path);
}

static int render(void *ud);

static int ask_load(void *ud, int what)
{
    (void)ud;
    return cg_ui_load(ui, what);
}

static void memory(void *ud, unsigned long *graphics, unsigned long *other)
{
    (void)ud;
    *graphics = AvailMem(MEMF_CHIP);
    *other = AvailMem(MEMF_ANY) - *graphics;
}

/************************************************************ the Switcher */

#define SW_PORT  "ToasterARexx.port"    /* Switcher(...) */
#define ED_PORT  "PROJECT_REXX_PORT"    /* its Editor's functions: croutons */
#define SW_ASK   (3 * TICKS_PER_SECOND)     /* an answer is waited for 3 seconds, */
#define SW_DO    (10 * TICKS_PER_SECOND)    /* 10 once the Switcher has answered */

static struct MsgPort *sw_reply;
static int sw_late;                     /* calls not answered in that time, */
static int sw_silent;                   /* the last one among them */

static void sw_free(struct RexxMsg *rm)
{
    int i;

    if (!rm->rm_Result1 && rm->rm_Result2)
        DeleteArgstring((UBYTE *)rm->rm_Result2);
    for (i = 0; i < 3; i++)
        if (rm->rm_Args[i])
            DeleteArgstring((UBYTE *)rm->rm_Args[i]);
    DeleteRexxMsg(rm);
}

/* the clock's ticks: a count of Delay()s runs long beside a busy Switcher */
static ULONG sw_ticks(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return ((ULONG)ds.ds_Days * 1440 + ds.ds_Minute) * 60 * TICKS_PER_SECOND + ds.ds_Tick;
}

static int sw_there(void)
{
    int there;

    Forbid();
    there = FindPort((CONST_STRPTR)SW_PORT) != NULL;
    Permit();
    return there;
}

/* one function of the Switcher's, as a script calls it after addlib():
   1 with its result when it answered 0 in time. A Switcher that is in
   one of its own requesters answers late; that answer is freed when it
   comes. */
static int sw_call(int ticks, const char *port, const char *fn, const char *a1, const char *a2, char *result,
                   size_t len)
{
    const char *arg[3];
    struct RexxMsg *rm, *m = NULL;
    struct MsgPort *to;
    ULONG since;
    int i, ok;

    arg[0] = fn;
    arg[1] = a1;
    arg[2] = a2;
    if (result && len)
        result[0] = 0;
    sw_silent = 0;
    if (!sw_reply && !(sw_reply = CreateMsgPort()))
        return 0;
    if (!(rm = CreateRexxMsg(sw_reply, NULL, NULL)))
        return 0;
    rm->rm_Action = RXFUNC | RXFF_RESULT | (a2 ? 2 : a1 ? 1 : 0);
    for (i = 0; i < 3 && arg[i]; i++)
        if (!(rm->rm_Args[i] = (IPTR)CreateArgstring((UBYTE *)arg[i], strlen(arg[i]))))
        {
            sw_free(rm);
            return 0;
        }
    Forbid();
    if ((to = FindPort((CONST_STRPTR)port)))
        PutMsg(to, &rm->rm_Node);
    Permit();
    if (!to)
    {
        sw_free(rm);
        return 0;
    }
    since = sw_ticks();
    for (;;)
    {
        while ((m = (struct RexxMsg *)GetMsg(sw_reply)) && m != rm)
        {
            sw_free(m);
            sw_late--;
        }
        if (m || sw_ticks() - since >= (ULONG)ticks)
            break;
        Delay(1);
    }
    sw_silent = !m;
    if (!m)
    {
        sw_late++;
        return 0;
    }
    ok = rm->rm_Result1 == 0;
    if (ok && result && len && rm->rm_Result2)
        snprintf(result, len, "%s", (const char *)rm->rm_Result2);
    sw_free(rm);
    return ok;
}

static void sw_close(void)
{
    struct RexxMsg *m;
    ULONG since = sw_ticks();

    if (!sw_reply)
        return;
    for (;;)
    {
        while ((m = (struct RexxMsg *)GetMsg(sw_reply)))
        {
            sw_free(m);
            sw_late--;
        }
        if (sw_late <= 0 || sw_ticks() - since >= SW_ASK)
            break;
        Delay(1);
    }
    /* a call still out comes back to this port: it stays */
    if (sw_late <= 0)
        DeleteMsgPort(sw_reply);
    sw_reply = NULL;
}

/* the Switcher gadget and Esc with a Switcher running: one that is busy
   comes to the front when it gets to it */
static int to_switcher(void *ud)
{
    (void)ud;
    if (!sw_there())
        return 0;
    sw_call(SW_ASK, SW_PORT, "Switcher", "TOSW", NULL, NULL, 0);
    return 1;
}

static struct cg_host host = { NULL, request, file, string, number, status, macro, render, load_book,
                               load_background, ask_load, memory, to_switcher };

/************************************************************ Take */

struct Device *TimerBase;

/* NTSC fields: 60000/1001 a second, in microseconds from the Take's start */
#define FIELD_US(n) ((UQUAD)(n) * 1001000 / 60)

static UQUAD uptime_us(void)
{
    struct timeval tv;

    GetUpTime(&tv);
    return (UQUAD)tv.tv_secs * 1000000 + tv.tv_micro;
}

/* the page out, full frame: still pages until a key or click, scrolls
   and crawls field by field until they end, Esc or the right button.
   Fields keep the Toaster's rate whatever the display refreshes at: one
   that is early waits for its time, and a Take that falls behind (a big
   frame on a slow machine) skips the fields it has missed, so a scroll
   lasts as long as it did on the Toaster. */
static int take(void)
{
    struct cg_play *p = cg_play_new(renderer, editor->book, editor->page, percent);
    struct MsgPort *tport = CreateMsgPort();
    struct timerequest *tr = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*tr)) : NULL;
    struct IntuiMessage *m;
    int going = 1, moving, timer = 0;
    UQUAD start = 0, n = 0;

    if (!p)
        return 0;
    if (tr && !OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, &tr->tr_node, 0))
    {
        timer = 1;
        TimerBase = tr->tr_node.io_Device;
        start = uptime_us();
    }
    moving = cg_edit_page(editor)->type == CG_PAGE_SCROLL || cg_edit_page(editor)->type == CG_PAGE_CRAWL;
    while (going)
    {
        int more = cg_play_field(p, rgba, width, height);

        WritePixelArray(rgba, 0, 0, width * 4, win->RPort, win->BorderLeft, win->BorderTop, width, height,
                        RECTFMT_RGBA);
        n++;
        if (moving && !more)
            break;
        if (moving && timer)
        {
            UQUAD now = uptime_us() - start, due = FIELD_US(n);

            if (now < due)
            {
                tr->tr_node.io_Command = TR_ADDREQUEST;
                tr->tr_time.tv_secs = 0;
                tr->tr_time.tv_micro = (ULONG)(due - now);
                DoIO(&tr->tr_node);
            }
            else if (now >= FIELD_US(n + 1))
            {
                /* the fields whose time has gone */
                UQUAD late = now * 60 / 1001000 - n;

                n += late;
                if (!cg_play_skip(p, (int)late))
                    break;
            }
        }
        else if (moving)
            WaitTOF();
        else
            WaitPort(win->UserPort);
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)))
        {
            if ((m->Class == IDCMP_RAWKEY && !(m->Code & IECODE_UP_PREFIX)) ||
                (m->Class == IDCMP_MOUSEBUTTONS && (m->Code == SELECTDOWN || m->Code == MENUDOWN)) ||
                m->Class == IDCMP_CLOSEWINDOW)
                going = 0;
            ReplyMsg(&m->ExecMessage);
        }
    }
    cg_play_free(p);
    if (timer)
        CloseDevice(&tr->tr_node);
    if (tr)
        DeleteIORequest((struct IORequest *)tr);
    if (tport)
        DeleteMsgPort(tport);
    return 1;
}

/* Take with a Switcher running: the page goes on its Program as well, as
   a crouton of its project that is loaded for the Take and deleted after
   it. A key page is keyed over Main, a scroll or crawl runs there, a
   buffer page is taken from the Framestore; the key or click that ends
   the Take takes it off. 0 when no Switcher takes the page. */
static int take_program(void)
{
    static const char file[] = "T:ToasterCG.take", *const bus[] = { "001", "002", "003", "004", "DV1", "DV2", "BKG" };
    int buffer = cg_edit_page(editor)->type == CG_PAGE_BUFFER, had;
    char spot[CG_RESULT_MAX], prvw[CG_RESULT_MAX], frez[CG_RESULT_MAX], framed[CG_RESULT_MAX] = "", text[CG_RESULT_MAX];
    const char *err;

    if (!sw_there() || !cg_page_save(editor, file, &err))
        return 0;
    had = sw_call(SW_ASK, ED_PORT, "CROUTONSPOT", NULL, NULL, spot, sizeof(spot));
    if (sw_silent || !sw_call(SW_DO, SW_PORT, "Switcher", "STAT", "PREV", prvw, sizeof(prvw)) ||
        !sw_call(SW_DO, SW_PORT, "Switcher", "STAT", "FREZ", frez, sizeof(frez)) ||
        !sw_call(SW_DO, ED_PORT, "CROUTONLOAD", file, NULL, text, sizeof(text)))
    {
        DeleteFile((CONST_STRPTR)file);
        return 0;
    }
    if (sw_call(SW_DO, SW_PORT, "Switcher", "TAKE", NULL, NULL, 0))
        sw_call(SW_DO, SW_PORT, "Switcher", "STAT", "MAIN", framed, sizeof(framed));
    take();
    /* off: only while the crouton is the one loaded here, and a buffer
       page only while it is still Main */
    if (sw_call(SW_DO, ED_PORT, "CROUTONPICK", "LAST", NULL, NULL, 0) &&
        sw_call(SW_DO, ED_PORT, "CROUTONNAME", NULL, NULL, text, sizeof(text)) && !strcasecmp(text, file))
    {
        if (buffer && framed[0] && sw_call(SW_DO, SW_PORT, "Switcher", "STAT", "MAIN", text, sizeof(text)) &&
            !strcmp(text, framed))
        {
            int slot = 0;
            long bit = atol(prvw);

            sw_call(SW_DO, SW_PORT, "Switcher", "TAKE", NULL, NULL, 0);
            while (bit > 1 && slot < 6)
            {
                bit >>= 1;
                slot++;
            }
            if (prvw[0] && bit == 1)
            {
                snprintf(text, sizeof(text), "P%s", bus[slot]);
                sw_call(SW_DO, SW_PORT, "Switcher", text, NULL, NULL, 0);
            }
            /* the Framestore was live before the page froze it */
            if (!strcmp(frez, "0"))
                sw_call(SW_DO, SW_PORT, "Switcher", "LVID", NULL, NULL, 0);
        }
        sw_call(SW_DO, ED_PORT, "CROUTONDELETE", NULL, NULL, NULL, 0);
        if (had)
            sw_call(SW_DO, ED_PORT, "CROUTONPICK", spot, NULL, NULL, 0);
    }
    DeleteFile((CONST_STRPTR)file);
    cg_ui_status(ui, framed[0] ? "Taken on Program" : "Switcher: not taken");
    return 1;
}

/* REND: the F9 Render, the page shown as it will be taken */
static int render(void *ud)
{
    (void)ud;
    cg_ui_preview(ui);
    show();
    return 1;
}

/************************************************************ ARexx */

static void rexx_message(struct RexxMsg *rm)
{
    char result[CG_RESULT_MAX];
    const char *argv[16];
    int rc, argc = 0, i;

    if (rm->rm_Node.mn_Node.ln_Type == NT_REPLYMSG)
    {
        /* a macro we started has finished */
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
        macros_out--;
        return;
    }
    /* what the Toaster's Switcher sent its CG to enter it: to the front */
    if (rm->rm_Args[0] && !strcasecmp((const char *)rm->rm_Args[0], "FGC_SELECT"))
    {
        ScreenToFront(win->WScreen);
        WindowToFront(win);
        ActivateWindow(win);
        rm->rm_Result1 = RC_OK;
        rm->rm_Result2 = 0;
        ReplyMsg(&rm->rm_Node);
        return;
    }
    if ((rm->rm_Action & RXCODEMASK) == RXFUNC)
    {
        argc = rm->rm_Action & RXARGMASK;
        for (i = 0; i < argc && i < 15; i++)
            argv[i] = rm->rm_Args[i + 1] ? (const char *)rm->rm_Args[i + 1] : "";
        rc = cg_cmd_run(&cmd, (const char *)rm->rm_Args[0], argc, argv, result);
    }
    else
        rc = cg_cmd_line(&cmd, (const char *)rm->rm_Args[0], result);
    rm->rm_Result1 = rc;
    rm->rm_Result2 = rc ? (IPTR)cmd.err : 0;
    if (rc == RC_OK && (rm->rm_Action & RXFF_RESULT))
        rm->rm_Result2 = (IPTR)CreateArgstring((UBYTE *)result, strlen(result));
    ReplyMsg(&rm->rm_Node);
    cg_ui_changed(ui);
    if (editor->message)
        cg_ui_status(ui, editor->message);
}

/************************************************************ events */

static unsigned qualifiers(UWORD q)
{
    unsigned r = 0;

    if (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
        r |= CG_QUAL_SHIFT;
    if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))
        r |= CG_QUAL_ALT;
    if (q & IEQUALIFIER_CONTROL)
        r |= CG_QUAL_CTRL;
    if (q & IEQUALIFIER_LCOMMAND)
        r |= CG_QUAL_LAMIGA;
    if (q & IEQUALIFIER_RCOMMAND)
        r |= CG_QUAL_RAMIGA;
    return r;
}

static int resized;

/* the window's new size: the largest 25 percent step that fits it, and
   the window made that size exactly */
static void resize(void)
{
    int iw = win->Width - win->BorderLeft - win->BorderRight, ih = win->Height - win->BorderTop - win->BorderBottom;
    int fw = iw * 100 / CG_FRAME_WIDTH, fh = ih * 100 / CG_FRAME_HEIGHT, p = (fw < fh ? fw : fh) / 25 * 25, w, h;
    uint8_t *n;

    if (p < 100)
        p = 100;
    cg_ui_set_scale(ui, p);
    cg_ui_size(ui, &w, &h);
    if ((w != width || h != height) && (n = realloc(rgba, (size_t)w * h * 4)))
    {
        rgba = n;
        width = w;
        height = h;
        percent = p;
    }
    if (iw != width || ih != height)
        ChangeWindowBox(win, win->LeftEdge, win->TopEdge, width + win->BorderLeft + win->BorderRight,
                        height + win->BorderTop + win->BorderBottom);
}

static int handle(struct IntuiMessage *m)
{
    unsigned ms = (unsigned)(m->Seconds * 1000 + m->Micros / 1000);
    int x = m->MouseX - win->BorderLeft, y = m->MouseY - win->BorderTop, r = 0;

    switch (m->Class)
    {
    case IDCMP_CLOSEWINDOW:
        return cg_ui_may_quit(ui) ? CG_UI_QUIT : 0;
    case IDCMP_RAWKEY:
        if (!(m->Code & IECODE_UP_PREFIX))
        {
            struct InputEvent ie;
            char buf[8];
            int n;

            memset(&ie, 0, sizeof(ie));
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = m->Code;
            ie.ie_Qualifier = m->Qualifier;
            ie.ie_EventAddress = m->IAddress ? *(APTR *)m->IAddress : NULL;
            n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL);
            r = cg_ui_key(ui, m->Code, n == 1 ? (unsigned char)buf[0] : 0, qualifiers(m->Qualifier));
        }
        break;
    case IDCMP_MOUSEBUTTONS:
        if (m->Code == SELECTDOWN)
            r = cg_ui_mouse(ui, CG_MOUSE_DOWN, x, y, qualifiers(m->Qualifier), ms);
        else if (m->Code == SELECTUP)
            r = cg_ui_mouse(ui, CG_MOUSE_UP, x, y, qualifiers(m->Qualifier), ms);
        else if (m->Code == MENUDOWN)
            r = cg_ui_mouse(ui, CG_MOUSE_RDOWN, x, y, qualifiers(m->Qualifier), ms);
        break;
    case IDCMP_MOUSEMOVE:
        r = cg_ui_mouse(ui, CG_MOUSE_MOVE, x, y, qualifiers(m->Qualifier), ms);
        break;
    case IDCMP_NEWSIZE:
        resized = 1;
        return CG_UI_REDRAW;
    case IDCMP_REFRESHWINDOW:
        BeginRefresh(win);
        EndRefresh(win, TRUE);
        r = CG_UI_REDRAW;
        break;
    }
    return r;
}

/* Workbench: the tool types of CG's icon, and the first project icon
   given with it as the book */
static char wb_book[512], wb_toaster[256], wb_config[256];
static LONG wb_scale;

static void wb_args(struct WBStartup *wbs, IPTR *args)
{
    struct DiskObject *dob;
    BPTR old;

    old = CurrentDir(wbs->sm_ArgList[0].wa_Lock);
    if ((dob = GetDiskObject((CONST_STRPTR)wbs->sm_ArgList[0].wa_Name)))
    {
        STRPTR v;

        if ((v = FindToolType((CONST STRPTR *)dob->do_ToolTypes, (STRPTR)"TOASTER")))
        {
            snprintf(wb_toaster, sizeof(wb_toaster), "%s", (const char *)v);
            args[1] = (IPTR)wb_toaster;
        }
        if ((v = FindToolType((CONST STRPTR *)dob->do_ToolTypes, (STRPTR)"SCALE")))
        {
            wb_scale = atol((const char *)v);
            args[2] = (IPTR)&wb_scale;
        }
        if ((v = FindToolType((CONST STRPTR *)dob->do_ToolTypes, (STRPTR)"CONFIG")))
        {
            snprintf(wb_config, sizeof(wb_config), "%s", (const char *)v);
            args[3] = (IPTR)wb_config;
        }
        FreeDiskObject(dob);
    }
    CurrentDir(old);
    if (wbs->sm_NumArgs > 1 && NameFromLock(wbs->sm_ArgList[1].wa_Lock, (STRPTR)wb_book, sizeof(wb_book)) &&
        AddPart((STRPTR)wb_book, (CONST_STRPTR)wbs->sm_ArgList[1].wa_Name, sizeof(wb_book)))
        args[0] = (IPTR)wb_book;
}

/* the panel being redrawn takes a while: a small window says how far it is */
static struct Window *art_win;

static void art_note(void *ud, int done, int of)
{
    static const char sample[] = "Drawing the panel from the Toaster's CG (once): 100%";
    static int shown = -1, tried;
    int pc = of > 0 ? done * 100 / of : 0;
    struct Screen *scr;
    char text[80];

    (void)ud;
    if (!tried && (scr = LockPubScreen(NULL)))
    {
        int w = TextLength(&scr->RastPort, (CONST_STRPTR)sample, sizeof(sample) - 1) + 24;
        int h = scr->RastPort.TxHeight * 3;

        tried = 1;
        art_win = OpenWindowTags(NULL, WA_PubScreen, (IPTR)scr, WA_Title, (IPTR)"ToasterCG", WA_InnerWidth, w,
                                 WA_InnerHeight, h, WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
                                 WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SmartRefresh, TRUE, TAG_END);
        /* the font the width was measured in */
        if (art_win)
            SetFont(art_win->RPort, scr->RastPort.Font);
        UnlockPubScreen(NULL, scr);
    }
    if (!art_win || pc == shown)
        return;
    shown = pc;
    snprintf(text, sizeof(text), "Drawing the panel from the Toaster's CG (once): %3d%%", pc);
    SetAPen(art_win->RPort, 1);
    SetBPen(art_win->RPort, 0);
    SetDrMd(art_win->RPort, JAM2);
    Move(art_win->RPort, art_win->BorderLeft + 12,
         art_win->BorderTop + art_win->RPort->TxHeight + art_win->RPort->TxBaseline);
    Text(art_win->RPort, (CONST_STRPTR)text, strlen(text));
}

static int cg_main(int argc, char **argv)
{
    static const char *const keep[] = { "PROGDIR:CG.art", "T:ToasterCG.art", NULL };
    IPTR args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rd = NULL;
    struct Screen *scr;
    struct cg_book *book = NULL;
    const char *err = NULL;
    char msg[600];
    int fit, quit = 0, rc = RETURN_FAIL;

    if (argc == 0)
    {
        from_wb = 1;
        wb_args((struct WBStartup *)argv, args);
    }
    else if (!(rd = ReadArgs((CONST_STRPTR)"BOOK,TOASTER/K,SCALE/K/N,CONFIG/K", (SIPTR *)args, NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)"CG");
        return RETURN_FAIL;
    }
    toaster = args[1] ? (const char *)args[1] : "Toaster:";
    if (!(ReqToolsBase = (struct ReqToolsBase *)OpenLibrary((CONST_STRPTR)"reqtools.library", 38)))
    {
        tell("needs reqtools.library");
        goto out;
    }
    if (args[0] && !(book = cg_book_load((const char *)args[0], &err)))
    {
        snprintf(msg, sizeof(msg), "%s: %s", (const char *)args[0], err);
        tell(msg);
        goto out;
    }
    /* the panel's imagery: the Toaster's own, or what was kept of it */
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        APTR old = me->pr_WindowPtr;
        int got;

        me->pr_WindowPtr = (APTR)-1;
        got = cg_art_load(toaster, keep, art_note, NULL, msg, sizeof(msg));
        me->pr_WindowPtr = old;
        if (art_win)
            CloseWindow(art_win);
        art_win = NULL;
        if (!got)
        {
            tell(msg);
            goto out;
        }
        if (got == CG_ART_UNKEPT)
            tell("The panel was drawn from the Toaster's CG, but could not be kept in\n"
                 "PROGDIR:CG.art or T:ToasterCG.art: it is drawn again at the next start.");
    }
    /* without the Toaster's drawers there are no fonts to set text in */
    {
        char probe[300];

        in_toaster(probe, sizeof(probe), "PSFonts");
        if (!exists(probe))
        {
            snprintf(msg, sizeof(msg),
                     "There is no %s: the Video Toaster's fonts, brushes and pages are not where\n"
                     "\"%s\" points. Assign Toaster: to the drawer that holds PSFonts, Brushes\n"
                     "and CG (the Video Toaster 4 content), or give TOASTER. The CG starts\n"
                     "without them; text has no fonts until one is loaded.",
                     probe, toaster);
            tell(msg);
        }
    }
    if (!book && !(book = calloc(1, sizeof(*book))))
        goto out;
    book->version = '4';
    if (!(renderer = cg_render_new(toaster)) || !(editor = cg_edit_new(book, renderer)))
        goto out;
    cg_cmd_init(&cmd, editor, &host);
    if (args[3])
        cg_cmd_read_config(&cmd, (const char *)args[3]);
    else if (!exists("PROGDIR:cg-config") || !cg_cmd_read_config(&cmd, "PROGDIR:cg-config"))
    {
        char own[300];

        in_toaster(own, sizeof(own), "Programs/CG_Support/cg-config");
        if (exists(own))
            cg_cmd_read_config(&cmd, own);
    }

    if (!(scr = LockPubScreen(NULL)))
        goto out;
    {
        /* the window's inside, under the screen title, in its borders */
        int aw = scr->Width - scr->WBorLeft - scr->WBorRight;
        int ah = scr->Height - scr->BarHeight - 1 - scr->WBorTop - scr->Font->ta_YSize - 1 - scr->WBorBottom;
        int fw = aw * 100 / CG_FRAME_WIDTH, fh = ah * 100 / CG_FRAME_HEIGHT;

        fit = (fw < fh ? fw : fh) / 25 * 25;
    }
    percent = args[2] ? (int)*(LONG *)args[2] : fit < 100 ? 100 : fit;
    if (percent < 100 || percent % 25)
    {
        tell("SCALE is a percentage, 100 or more in steps of 25");
        UnlockPubScreen(NULL, scr);
        goto out;
    }
    if (args[2] && percent > fit)
    {
        snprintf(msg, sizeof(msg), "SCALE %d does not fit this screen; %d does", percent, fit);
        tell(msg);
        UnlockPubScreen(NULL, scr);
        goto out;
    }
    if (!(ui = cg_ui_new(editor, &cmd, &host, percent)))
    {
        UnlockPubScreen(NULL, scr);
        goto out;
    }
    cg_ui_size(ui, &width, &height);
    win = OpenWindowTags(NULL, WA_PubScreen, (IPTR)scr, WA_Title, (IPTR)"ToasterCG", WA_InnerWidth, width,
                         WA_InnerHeight, height, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE, WA_SmartRefresh, TRUE,
                         WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                                       IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE,
                         TAG_END);
    UnlockPubScreen(NULL, scr);
    if (!win || !(rgba = malloc((size_t)width * height * 4)))
        goto out;
    /* sized from the Toaster's frame (100 percent) to the screen */
    WindowLimits(win, CG_FRAME_WIDTH + win->BorderLeft + win->BorderRight,
                 CG_FRAME_HEIGHT + win->BorderTop + win->BorderBottom, win->WScreen->Width, win->WScreen->Height);
    host.ud = NULL;

    {
        /* looked up in the DOS list: a Lock of a name that is not there
           asks for the volume (from a shell, "Please insert volume") */
        struct DosList *dl = LockDosList(LDF_ALL | LDF_READ);
        int taken = FindDosEntry(dl, (CONST_STRPTR)"ToasterCG", LDF_ALL) != NULL;
        BPTR pd;

        UnLockDosList(LDF_ALL | LDF_READ);
        if (!taken && (pd = DupLock(GetProgramDir())))
        {
            if (AssignLock((CONST_STRPTR)"ToasterCG", pd))
                own_assign = 1;
            else
                UnLock(pd);
        }
    }
    Forbid();
    if (!FindPort((CONST_STRPTR)PORT_NAME) && (rexx_port = CreateMsgPort()))
    {
        rexx_port->mp_Node.ln_Name = PORT_NAME;
        rexx_port->mp_Node.ln_Pri = 0;
        AddPort(rexx_port);
    }
    Permit();
    if (cmd.startup[0])
        macro(NULL, cmd.startup);

    show();
    if (args[0])
    {
        cg_cmd_check_book(&cmd);
        cg_ui_changed(ui);
        show();
    }
    while (!quit)
    {
        ULONG sigs = Wait((1UL << win->UserPort->mp_SigBit) | (rexx_port ? 1UL << rexx_port->mp_SigBit : 0) |
                          SIGBREAKF_CTRL_C);
        struct IntuiMessage *m;
        struct RexxMsg *rm;
        int r = 0;

        if ((sigs & SIGBREAKF_CTRL_C) && cg_ui_may_quit(ui))
            break;
        while (rexx_port && (rm = (struct RexxMsg *)GetMsg(rexx_port)))
        {
            rexx_message(rm);
            r |= CG_UI_REDRAW;
            if (cmd.exit)
                quit = 1;
        }
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)))
        {
            r |= handle(m);
            ReplyMsg(&m->ExecMessage);
        }
        if (resized)
        {
            resized = 0;
            resize();
        }
        if (r & CG_UI_QUIT)
            quit = 1;
        if ((r & CG_UI_TAKE) && !take_program())
            take();
        if (r & (CG_UI_REDRAW | CG_UI_TAKE))
            show();
    }
    rc = RETURN_OK;

out:
    if (rexx_port)
    {
        struct Message *m;

        RemPort(rexx_port);
        while (macros_out > 0)
        {
            WaitPort(rexx_port);
            while ((m = GetMsg(rexx_port)))
                rexx_message((struct RexxMsg *)m);
        }
        while ((m = GetMsg(rexx_port)))
            ReplyMsg(m);
        DeleteMsgPort(rexx_port);
    }
    sw_close();
    if (own_assign)
        AssignLock((CONST_STRPTR)"ToasterCG", BNULL);
    if (win)
        CloseWindow(win);
    free(rgba);
    cg_ui_free(ui);
    if (editor)
    {
        cg_book_free(editor->book);
        cg_edit_free(editor);
    }
    else
        cg_book_free(book);
    cg_render_free(renderer);
    cg_art_free();
    if (ReqToolsBase)
        CloseLibrary((struct Library *)ReqToolsBase);
    if (rd)
        FreeArgs(rd);
    return rc;
}

/* FreeType and the interface want more stack than a shell or icon may
   give: below CG_STACK the CG runs on its own */
#define CG_STACK (256 * 1024)

int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    struct StackSwapStruct sss;
    struct StackSwapArgs ssa;
    int rc;

    if ((IPTR)me->tc_SPUpper - (IPTR)me->tc_SPLower >= CG_STACK ||
        !(sss.stk_Lower = AllocVec(CG_STACK, MEMF_ANY)))
        return cg_main(argc, argv);
    sss.stk_Upper = (APTR)((IPTR)sss.stk_Lower + CG_STACK);
    sss.stk_Pointer = sss.stk_Upper;
    memset(&ssa, 0, sizeof(ssa));
    ssa.Args[0] = (IPTR)argc;
    ssa.Args[1] = (IPTR)argv;
    rc = (int)NewStackSwap(&sss, (APTR)cg_main, &ssa);
    FreeVec(sss.stk_Lower);
    return rc;
}
