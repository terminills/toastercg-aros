/*
    The CG interface, after NewGadgets.c and NewFunction.c: the menu bar
    in its four modes (AllocTopBar/AllocBottomBar), the Case* handlers
    behind each gadget, the popups, and editing on the page with the
    mouse and keys. Layout constants the release does not define
    (gadgets.h is missing) are fitted to the gadget sizes: the top row
    and the row of fifteen icons are both 645 pixels wide.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgui.h"
#include "cgpage.h"
#include "canvas.h"
#include "gadgetid.h"
#include "version.h"
#include "cgilbm.h"

#define BAR_WIDTH   645
#define TOP_H       22
#define ICON_MINY   22
#define ICON_W      43
#define BAR_0_H     54
#define BAR_TEXT_Y  16

/* the colour bar */
#define BAR_2_H         110
#define COLOR_WHICH_X   6
#define COLOR_WHICH_Y   26
#define COLOR_2_X       6
#define COLOR_2_Y       66
#define COLOR_3_X       146
#define SLIDER_X        262
#define SLIDER_Y        28
#define SLIDER_W        208
#define SLIDER_H        10
#define ALPHA_Y         80
#define COLOR_NUM_X     480
#define SWATCH_X        552
#define SWATCH_Y        26
#define DEFAULTS_Y      80

#define MAX_GADS        48
#define POP_MAX         64
#define POP_ROWS        14
#define DOUBLE_CLICK_MS 500

enum { BAR_NORMAL, BAR_PAGE_TYPE, BAR_COLOR, BAR_PAGE_CMDS };
enum { COL_BKG, COL_TEXT, COL_SHADOW, COL_OUTLINE };
enum { DRAG_NONE, DRAG_MOVE, DRAG_SIZE, DRAG_SELECT };

static const char msg_default[] = "Video Toaster CG";
static const char msg_choose_page[] = "Select key, color, scroll, or crawl page";
static const char msg_choose_color[] = "Select background, text, shadow and border colors.";
static const char msg_choose_color_nobg[] = "Select text, shadow and border colors.";
static const char msg_controls[] = "Controls Menu";

static const char *const preset_names[10] = { "White", "Grey", "Black", "Yellow", "Red", "Green",
                                              "Light Blue", "Dark Blue", "Purple", "Orange" };
static const struct cg_rgba preset_colors[10] = {
    { 0xd2, 0xd2, 0xd2, 255 }, { 0x80, 0x80, 0x80, 255 }, { 0, 0, 0, 255 }, { 0xe6, 0xe6, 0x50, 255 },
    { 0xc0, 0x20, 0x20, 255 }, { 0x20, 0xc0, 0x20, 255 }, { 0x40, 0xc0, 0xff, 255 }, { 0x10, 0x10, 0x80, 255 },
    { 0xd2, 0x50, 0xd2, 255 }, { 0xf0, 0xa8, 0x68, 255 } };

struct gad
{
    int id, x, y, w, h;
    int sel;
};

struct popup
{
    int  open, gadget;
    int  x, y, w, h;
    int  n, hi, top;
    char names[POP_MAX][40];
    int  values[POP_MAX];
};

struct cg_ui
{
    struct cg_editor   *e;
    struct cg_cmd      *cmd;
    struct cg_host     *host;
    int                 scale;      /* percent */

    int                 bar_mode, bar_on, bar_y, bar_h;
    struct gad          gads[MAX_GADS];
    int                 ngads;
    struct pcanvas      bar;
    int                 pressed, press_inside;
    int                 drag_bar, drag_dy;
    int                 slider;
    int                 col_target, col_bottom;
    const char         *status;
    char                status_buf[80];
    int                 transient;  /* status goes at the next input */
    char                dirs[6][CG_RESULT_MAX];

    uint8_t            *page;       /* the page rendered at scale */
    int                 page_valid;
    int                 preview, help;
    struct popup        pop;

    int                 view_x, view_y; /* scroll and crawl pages: the page at the frame's top left */
    int                 drag_view;
    int                 mdown, drag, mx0, my0, mx, my;
    unsigned            last_ms;
    int                 clicks;
    struct cg_line     *drag_line;
    int                 drag_char;
    int                 box_w, box_h;
};

static const struct cg_art_gadget *art(int id)
{
    int i;

    for (i = 0; i < cg_art_ngadgets; i++)
        if (cg_art_gadgets[i].id == id)
            return &cg_art_gadgets[i];
    return &cg_art_gadgets[0];
}

/************************************************************ geometry */

static int frame_w(const struct cg_ui *u)
{
    return cg_edge(CG_FRAME_WIDTH, u->scale);
}

static int frame_h(const struct cg_ui *u)
{
    return cg_edge(CG_FRAME_HEIGHT, u->scale);
}

static int bar_x(void)
{
    return (CG_FRAME_WIDTH - BAR_WIDTH) / 2;
}

void cg_ui_size(const struct cg_ui *u, int *w, int *h)
{
    *w = frame_w(u);
    *h = frame_h(u);
}

/************************************************************ layout */

static struct gad *add(struct cg_ui *u, int id, int x, int y)
{
    const struct cg_art_gadget *a = art(id);
    struct gad *g;

    if (u->ngads == MAX_GADS)
        return &u->gads[MAX_GADS - 1];
    g = &u->gads[u->ngads++];
    g->id = id;
    g->x = x;
    g->y = y;
    g->w = a->w;
    g->h = a->h;
    g->sel = 0;
    return g;
}

/* LinkGadget(): icons run left to right */
static int row_x;

static struct gad *icon(struct cg_ui *u, int id)
{
    struct gad *g = add(u, id, row_x, ICON_MINY);

    row_x += g->w;
    return g;
}

static struct gad *find(struct cg_ui *u, int id)
{
    int i;

    for (i = 0; i < u->ngads; i++)
        if (u->gads[i].id == id)
            return &u->gads[i];
    return NULL;
}

static int moving_page(const struct cg_page *p)
{
    return p->type == CG_PAGE_SCROLL || p->type == CG_PAGE_CRAWL;
}

static void layout_top(struct cg_ui *u)
{
    struct gad *g;
    int x = 0;

    switch (u->bar_mode)
    {
    case BAR_NORMAL:
        x += add(u, ID_MESSAGE, x, 0)->w;
        x += add(u, ID_CURRENT_FONT, x, 0)->w;
        x += add(u, ID_PAGE_NUMBER, x, 0)->w;
        g = add(u, ID_BAR_PAGE_UP, x, 0);
        add(u, ID_BAR_PAGE_DOWN, x, g->h);
        x += g->w;
        break;
    case BAR_PAGE_TYPE:
    case BAR_COLOR:
        x += add(u, ID_PAGE_TYPE, x, 0)->w;
        x += add(u, ID_BAR_CONTINUE, x, 0)->w;
        break;
    case BAR_PAGE_CMDS:
        x += add(u, ID_MESSAGE, x, 0)->w;
        x += add(u, ID_CURRENT_FONT, x, 0)->w;
        x += add(u, ID_BAR_CONTINUE, x, 0)->w;
        break;
    }
    add(u, ID_SWITCHER, x, 0);
}

static void layout_color(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    int x = COLOR_WHICH_X, i, moving = moving_page(p);
    struct gad *g;

    if (p->type == CG_PAGE_BUFFER)
        x += add(u, ID_COLOR_BKG, x, COLOR_WHICH_Y)->w;
    else
        x += art(ID_COLOR_BKG)->w;
    x += add(u, ID_COLOR_TEXT, x, COLOR_WHICH_Y)->w;
    x += add(u, ID_COLOR_SHADOW, x, COLOR_WHICH_Y)->w;
    if (!moving)
        add(u, ID_COLOR_OUTLINE, x, COLOR_WHICH_Y);
    for (i = 0; i < u->ngads; i++)
        u->gads[i].sel = u->gads[i].id == ID_COLOR_BKG + u->col_target;

    x = COLOR_2_X;
    switch (u->col_target)
    {
    case COL_BKG:
        for (i = 0; i < 3; i++)
        {
            g = add(u, ID_COLOR_SOLID + i, x, COLOR_2_Y);
            g->sel = p->background == i;
            x += g->w;
        }
        if (p->background == CG_BACKGROUND_GRADATION)
        {
            (g = add(u, ID_COLOR_TOP, COLOR_3_X, COLOR_2_Y))->sel = !u->col_bottom;
            add(u, ID_COLOR_BOTTOM, COLOR_3_X + g->w, COLOR_2_Y)->sel = u->col_bottom;
        }
        else if (p->background == CG_BACKGROUND_BUFFER)
            add(u, ID_LOAD_RGB, COLOR_3_X, COLOR_2_Y);
        break;
    case COL_TEXT:
        if (moving)
            break;
        (g = add(u, ID_TEXTCOLOR_SOLID, x, COLOR_2_Y))->sel = !(e->attr.special_fill & CG_FILL_FACE_GRAD);
        add(u, ID_TEXTCOLOR_GRADATION, x + g->w, COLOR_2_Y)->sel = !g->sel;
        if (e->attr.special_fill & CG_FILL_FACE_GRAD)
        {
            (g = add(u, ID_TEXTCOLOR_TOP, COLOR_3_X, COLOR_2_Y))->sel = !u->col_bottom;
            add(u, ID_TEXTCOLOR_BOTTOM, COLOR_3_X + g->w, COLOR_2_Y)->sel = u->col_bottom;
        }
        break;
    case COL_OUTLINE:
        if (moving)
            break;
        (g = add(u, ID_OLCOLOR_SOLID, x, COLOR_2_Y))->sel = !(e->attr.special_fill & CG_FILL_OUTLINE_GRAD);
        add(u, ID_OLCOLOR_GRADATION, x + g->w, COLOR_2_Y)->sel = !g->sel;
        if (e->attr.special_fill & CG_FILL_OUTLINE_GRAD)
        {
            (g = add(u, ID_OLCOLOR_TOP, COLOR_3_X, COLOR_2_Y))->sel = !u->col_bottom;
            add(u, ID_OLCOLOR_BOTTOM, COLOR_3_X + g->w, COLOR_2_Y)->sel = u->col_bottom;
        }
        break;
    }
    if (!(u->col_target == COL_BKG && p->background == CG_BACKGROUND_BUFFER))
        for (i = 0; i < 3; i++)
        {
            g = add(u, ID_COLOR_RED + i, SLIDER_X, SLIDER_Y + i * (SLIDER_H + 4));
            g->w = SLIDER_W;
            g->h = SLIDER_H;
        }
    if (!moving && u->col_target != COL_BKG)
    {
        g = add(u, ID_COLOR_ALPHA, SLIDER_X, ALPHA_Y);
        g->w = SLIDER_W;
        g->h = SLIDER_H;
    }
    add(u, ID_COLOR_SWATCH, SWATCH_X, SWATCH_Y);
    add(u, ID_COLOR_DEFAULTS, SWATCH_X, DEFAULTS_Y);
}

/* AllocBottomBar() */
static void layout_bottom(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    const struct cg_attr *a = &e->attr;
    int i;

    row_x = 0;
    switch (u->bar_mode)
    {
    case BAR_NORMAL:
        icon(u, ID_PAGE_EMPTY + (p->type <= CG_PAGE_CRAWL ? p->type : 0));
        icon(u, ID_PAGE_COMMANDS);
        icon(u, ID_COLOR);
        icon(u, ID_SHADOW_NONE + (a->shadow_type <= 2 ? a->shadow_type : 0));
        if (a->shadow_type)
        {
            icon(u, ID_SHADOW_NORTH + (a->shadow_direction & 7));
            i = (a->shadow_length - 2) >> 1;
            icon(u, ID_SHADOW_1 + (i < 0 ? 0 : i > 4 ? 4 : i));
        }
        else
        {
            icon(u, ID_BLANK);
            icon(u, ID_BLANK);
        }
        icon(u, ID_OUTLINE_NONE + (a->outline_type <= 3 ? a->outline_type : 0));
        if ((a->shadow_type || a->outline_type) && p->type != CG_PAGE_SCROLL && p->type != CG_PAGE_EMPTY)
            icon(u, ID_OUTLINE_PRIORITY + (a->shadow_priority ? 1 : 0));
        else
            icon(u, ID_BLANK);
        if (p->type && p->type != CG_PAGE_CRAWL)
            icon(u, ID_JUSTIFY_NONE + (e->justify <= 3 ? e->justify : 0));
        else
            icon(u, ID_BLANK);
        if (!moving_page(p) && p->type != CG_PAGE_EMPTY)
        {
            icon(u, ID_HORIZ_CENTER);
            icon(u, ID_HORIZ_LOWER);
        }
        else
        {
            icon(u, ID_BLANK);
            icon(u, ID_BLANK);
        }
        switch (p->type)
        {
        case CG_PAGE_STATIC:
        case CG_PAGE_BUFFER:
            icon(u, ID_COPY_PAGE);
            icon(u, ID_RENDER_LINE);
            icon(u, ID_RENDER_PAGE);
            icon(u, ID_TAKE);
            break;
        case CG_PAGE_SCROLL:
        case CG_PAGE_CRAWL:
            icon(u, ID_COPY_PAGE);
            if (p->type == CG_PAGE_SCROLL)
                icon(u, ID_SCROLL_SPEED_1 + (p->speed > 8 ? 4 : p->speed >> 1));
            else
                icon(u, ID_CRAWL_SPEED_1 + (p->speed > 3 ? 3 : p->speed));
            icon(u, ID_PLAY_ONCE + (p->playback < 0 ? 2 : p->playback > 1 ? 1 : p->playback));
            icon(u, ID_TAKE);
            break;
        default:
            for (i = 0; i < 4; i++)
                icon(u, ID_BLANK);
        }
        break;
    case BAR_PAGE_TYPE:
        for (i = ID_PAGE_STATIC; i <= ID_PAGE_CRAWL; i++)
            icon(u, i)->sel = p->type == i - ID_PAGE_EMPTY;
        break;
    case BAR_COLOR:
        layout_color(u);
        break;
    case BAR_PAGE_CMDS:
        if (moving_page(p))
        {
            icon(u, ID_BLANK);
            icon(u, ID_BLANK);
        }
        else
        {
            icon(u, ID_DEPTH_FRONT);
            icon(u, ID_DEPTH_BACK);
        }
        icon(u, ID_FONT_ADD);
        icon(u, ID_LOAD_DRAW);
        icon(u, ID_BRUSH_ADD);
        icon(u, ID_FONT_INFO);
        icon(u, ID_FONT_CLEAR);
        icon(u, ID_SAVE_BOOK);
        icon(u, ID_LOAD_BOOK);
        icon(u, ID_SAVE_PAGE);
        if (p->type != CG_PAGE_EMPTY)
            icon(u, ID_LOAD_TEXT);
        icon(u, ID_BLANK);
        if (p->type == CG_PAGE_CRAWL || p->type == CG_PAGE_EMPTY)
            icon(u, ID_BLANK);
        else
            icon(u, ID_ERASE_LINE);
        icon(u, ID_ERASE_PAGE);
        icon(u, ID_ERASE_BOOK);
        break;
    }
}

static void layout(struct cg_ui *u)
{
    int h;

    u->ngads = 0;
    layout_top(u);
    layout_bottom(u);
    h = u->bar_mode == BAR_COLOR ? BAR_2_H : BAR_0_H;
    if (h != u->bar_h || !u->bar.px)
    {
        pc_free(&u->bar);
        pc_init(&u->bar, BAR_WIDTH + 1, h);
        u->bar_h = h;
    }
    if (u->bar_y + u->bar_h > CG_FRAME_HEIGHT)
        u->bar_y = CG_FRAME_HEIGHT - u->bar_h;
    if (u->bar_y < 0)
        u->bar_y = 0;
}

/************************************************************ drawing the bar */

static struct cg_rgba *slider_color(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);

    switch (u->col_target)
    {
    case COL_BKG:
        return u->col_bottom && p->background == CG_BACKGROUND_GRADATION ? &p->bottom : &p->top;
    case COL_SHADOW:
        return &e->attr.shadow;
    case COL_OUTLINE:
        return u->col_bottom && (e->attr.special_fill & CG_FILL_OUTLINE_GRAD) ? &e->attr.outline_grad :
                                                                                  &e->attr.outline;
    }
    return u->col_bottom && (e->attr.special_fill & CG_FILL_FACE_GRAD) ? &e->attr.face_grad : &e->attr.face;
}

static uint8_t *slider_value(struct cg_ui *u, int id)
{
    struct cg_rgba *c = slider_color(u);

    switch (id)
    {
    case ID_COLOR_RED:
        return &c->r;
    case ID_COLOR_GREEN:
        return &c->g;
    case ID_COLOR_BLUE:
        return &c->b;
    }
    return &c->a;
}

static void draw_text_gadget(struct cg_ui *u, const struct gad *g, const char *text)
{
    pc_image(&u->bar, &art(g->id)->render, g->x, g->y);
    pc_text(&u->bar, text, g->x + 4, g->y + BAR_TEXT_Y, g->w - 12);
}

static void draw_bar(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    const struct cg_data *d;
    char buf[64];
    int i;

    pc_clear(&u->bar, PEN_BG);
    if (u->bar_mode == BAR_COLOR)
    {
        pc_fill(&u->bar, 1, TOP_H + 1, BAR_WIDTH - 3, u->bar_h - 2, PEN_DGREY);
        pc_box(&u->bar, 0, TOP_H, BAR_WIDTH - 2, u->bar_h - 1, BOX_REV);
    }
    for (i = 0; i < u->ngads; i++)
    {
        const struct gad *g = &u->gads[i];
        const struct cg_art_gadget *a = art(g->id);
        int down = u->pressed == i && u->press_inside;

        switch (g->id)
        {
        case ID_MESSAGE:
        case ID_PAGE_TYPE:
            draw_text_gadget(u, g, u->status ? u->status : msg_default);
            continue;
        case ID_CURRENT_FONT:
            d = cg_book_find_data(e->book, (uint16_t)e->attr.id);
            if (e->attr.id == CG_BOX_ID)
                snprintf(buf, sizeof(buf), "Box");
            else if (d)
            {
                const char *n = strrchr(d->filename, '/');

                n = n ? n + 1 : strrchr(d->filename, ':') ? strrchr(d->filename, ':') + 1 : d->filename;
                if (d->type == CG_DATA_BRUSH)
                    snprintf(buf, sizeof(buf), "%s", n);
                else
                    snprintf(buf, sizeof(buf), "%s %d", n, d->height);
            }
            else
                buf[0] = 0;
            draw_text_gadget(u, g, buf);
            continue;
        case ID_PAGE_NUMBER:
            snprintf(buf, sizeof(buf), "Page %3d", e->page);
            pc_image(&u->bar, &a->render, g->x, g->y);
            pc_text(&u->bar, buf, g->x + 8, g->y + BAR_TEXT_Y, 0);
            continue;
        case ID_COLOR_RED:
        case ID_COLOR_GREEN:
        case ID_COLOR_BLUE:
        case ID_COLOR_ALPHA:
        {
            int v = *slider_value(u, g->id), kx = g->x + v * (g->w - 6) / 255;

            pc_box(&u->bar, g->x - 4, g->y - 2, g->x + g->w + 3, g->y + g->h + 1, BOX_REV);
            pc_fill(&u->bar, g->x - 3, g->y - 1, g->x + g->w + 2, g->y + g->h, PEN_DGREY);
            pc_fill(&u->bar, kx, g->y, kx + 5, g->y + g->h - 1, PEN_CREAM);
            pc_box(&u->bar, kx, g->y, kx + 5, g->y + g->h - 1, BOX_STANDARD);
            buf[0] = "RGBA"[g->id - ID_COLOR_RED];
            buf[1] = 0;
            pc_text(&u->bar, buf, g->x - 16, g->y + g->h, 0);
            snprintf(buf, sizeof(buf), "%3d", v);
            pc_fill(&u->bar, COLOR_NUM_X, g->y - 2, COLOR_NUM_X + 26, g->y + g->h + 1, PEN_DGREY);
            pc_text(&u->bar, buf, COLOR_NUM_X, g->y + g->h, 0);
            /* key pages: the four alpha levels live video keys at */
            if (g->id == ID_COLOR_ALPHA && cg_edit_page(e)->type == CG_PAGE_STATIC)
            {
                int dx = (g->w + 8) / 4 - 4, k;

                for (k = 0; k < 4; k++)
                    pc_fill(&u->bar, g->x + k * (dx + 4), g->y + g->h + 3, g->x + k * (dx + 4) + dx,
                            g->y + g->h + 7, PEN_BLACK);
            }
            continue;
        }
        case ID_COLOR_SWATCH:
            pc_image(&u->bar, &a->render, g->x, g->y);
            pc_box(&u->bar, g->x, g->y, g->x + g->w - 1, g->y + g->h - 1, BOX_REV);
            continue;
        }
        if ((g->sel || down) && a->select.px)
            pc_image(&u->bar, &a->select, g->x, g->y);
        else
            pc_image(&u->bar, &a->render, g->x, g->y);
    }
}

/************************************************************ the page */

static void page_dirty(struct cg_ui *u)
{
    u->page_valid = 0;
}

void cg_ui_changed(struct cg_ui *u)
{
    page_dirty(u);
    layout(u);
}

static void blend_px(uint8_t *d, uint8_t r, uint8_t g, uint8_t b, int a)
{
    d[0] = (uint8_t)((r * a + d[0] * (255 - a)) / 255);
    d[1] = (uint8_t)((g * a + d[1] * (255 - a)) / 255);
    d[2] = (uint8_t)((b * a + d[2] * (255 - a)) / 255);
}

static void overlay_rect(struct cg_ui *u, uint8_t *rgba, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
                         uint8_t b, int a, int outline)
{
    int p = u->scale, x, y, W = frame_w(u), H = frame_h(u);
    /* the outline is one Toaster pixel wide, inside the rectangle */
    int il = cg_edge(x0 + 1, p), it = cg_edge(y0 + 1, p), ir = cg_edge(x1, p), ib = cg_edge(y1, p);

    x0 = cg_edge(x0, p);
    y0 = cg_edge(y0, p);
    x1 = cg_edge(x1 + 1, p);
    y1 = cg_edge(y1 + 1, p);
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
        for (x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++)
            if (!outline || y < it || y >= ib || x < il || x >= ir)
                blend_px(rgba + ((size_t)y * W + x) * 4, r, g, b, a);
}

/* where a box or brush's face starts: its outline is above it, inside
   the line's height (cg_outline_y()) */
static int face_top(const struct cg_line *l)
{
    const struct cg_attr *a = l->nchars ? l->chars[0].attr : NULL;

    return l->yoffset + (a ? cg_outline_y(a->outline_type) : 0);
}

/* the same in page coordinates, through the scroll or crawl view */
static void page_rect(struct cg_ui *u, uint8_t *rgba, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
                      uint8_t b, int a, int outline)
{
    overlay_rect(u, rgba, x0 - u->view_x, y0 - u->view_y, x1 - u->view_x, y1 - u->view_y, r, g, b, a, outline);
}

/* on a crawl page any of its lines stands for the whole crawl, merged
   into tmp (cg_crawl_free() it when tmp->chars is set) */
static const struct cg_line *text_line(struct cg_ui *u, const struct cg_line *l, struct cg_line *tmp)
{
    struct cg_page *p = cg_edit_page(u->e);

    tmp->chars = NULL;
    if (l && p->type == CG_PAGE_CRAWL && cg_crawl_merge(p, tmp))
        return tmp;
    return l;
}

static double char_x(struct cg_ui *u, const struct cg_line *line, int i)
{
    struct cg_line tmp;
    const struct cg_line *l = text_line(u, line, &tmp);
    double *xs, x;

    if (!l || cg_line_is_graphic(u->e->book, l) || !cg_line_length(l) || !(xs = cg_render_line_xs(u->e->r, u->e->book, l)))
        x = l ? l->xoffset : 0;
    else
    {
        x = l->xoffset + xs[i < l->nchars ? i : l->nchars];
        free(xs);
    }
    if (tmp.chars)
        cg_crawl_free(&tmp);
    return x;
}

/* scroll pages run past the frame: the slider covers the whole height */
#define PROP_W 10

static int scroll_height(struct cg_ui *u)
{
    struct cg_line *l;
    int bottom = CG_FRAME_HEIGHT;

    for (l = cg_edit_page(u->e)->lines; l; l = l->next)
        if (l->yoffset + l->total_height + CG_FRAME_HEIGHT / 2 > bottom)
            bottom = l->yoffset + l->total_height + CG_FRAME_HEIGHT / 2;
    return bottom;
}

static void prop_knob(struct cg_ui *u, int *y, int *h)
{
    int total = scroll_height(u), range = total - CG_FRAME_HEIGHT;

    *h = CG_FRAME_HEIGHT * CG_FRAME_HEIGHT / total;
    if (*h < 20)
        *h = 20;
    *y = range > 0 ? u->view_y * (CG_FRAME_HEIGHT - *h) / range : 0;
}

static void set_view_from_prop(struct cg_ui *u, int fy)
{
    int ky, kh, total = scroll_height(u), range = total - CG_FRAME_HEIGHT;
    struct cg_line *l, *close = NULL, *last = NULL;

    prop_knob(u, &ky, &kh);
    if (range <= 0 || CG_FRAME_HEIGHT == kh)
        return;
    u->view_y = (fy - kh / 2) * range / (CG_FRAME_HEIGHT - kh);
    if (u->view_y < 0)
        u->view_y = 0;
    if (u->view_y > range)
        u->view_y = range;
    /* the line at the view's top becomes current, as newscroll.c's
       FindCloseLine makes it, so the view stays where it was put */
    for (l = cg_edit_page(u->e)->lines; l; l = l->next)
    {
        if (l->yoffset >= u->view_y && (!close || l->yoffset < close->yoffset))
            close = l;
        if (!last || l->yoffset > last->yoffset)
            last = l;
    }
    if (close || last)
        cg_edit_set_line(u->e, close ? close : last, 0);
    u->page_valid = 0;
}

/* a crawl's slider, above the bar: one level a character and one more,
   the cursor's (newscroll.c's FindSliderValues on CrawlLineLength + 1) */
static int crawl_slider_y(struct cg_ui *u)
{
    return (u->bar_on ? u->bar_y : CG_FRAME_HEIGHT) - PROP_W - 2;
}

static void crawl_knob(struct cg_ui *u, int *x, int *w)
{
    int levels = cg_edit_length(u->e) + 1;

    *w = CG_FRAME_WIDTH / levels;
    if (*w < 20)
        *w = 20;
    *x = levels > 1 ? u->e->cursor * (CG_FRAME_WIDTH - *w) / (levels - 1) : 0;
}

static void set_cursor_from_slider(struct cg_ui *u, int fx)
{
    int kx, kw, levels = cg_edit_length(u->e) + 1, c;

    crawl_knob(u, &kx, &kw);
    if (levels < 2 || CG_FRAME_WIDTH == kw)
        return;
    c = ((fx - kw / 2) * (levels - 1) + (CG_FRAME_WIDTH - kw) / 2) / (CG_FRAME_WIDTH - kw);
    u->e->cursor = c < 0 ? 0 : c > levels - 1 ? levels - 1 : c;
    u->page_valid = 0;
}

/* keep the cursor in view on a scroll or crawl page */
static void follow(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l = e->line;
    int vx = 0, vy = 0;

    if (p->type == CG_PAGE_SCROLL && l)
    {
        vy = u->view_y;
        if (l->yoffset < vy + 16)
            vy = l->yoffset - 16;
        if (l->yoffset + l->total_height > vy + CG_FRAME_HEIGHT - 16)
            vy = l->yoffset + l->total_height - CG_FRAME_HEIGHT + 16;
        if (vy < 0)
            vy = 0;
    }
    else if (p->type == CG_PAGE_CRAWL && l)
    {
        int x = (int)char_x(u, l, e->cursor);

        vx = u->view_x;
        if (x < vx + 40)
            vx = x - 40;
        if (x > vx + CG_FRAME_WIDTH - 40)
            vx = x - CG_FRAME_WIDTH + 40;
        if (vx < 0)
            vx = 0;
    }
    if (vx != u->view_x || vy != u->view_y)
    {
        u->view_x = vx;
        u->view_y = vy;
        u->page_valid = 0;
    }
}

static void draw_prop(struct cg_ui *u, uint8_t *rgba)
{
    struct pcanvas c;
    int ky, kh;

    if (cg_edit_page(u->e)->type == CG_PAGE_CRAWL && pc_init(&c, CG_FRAME_WIDTH, PROP_W))
    {
        crawl_knob(u, &ky, &kh);
        pc_fill(&c, 0, 0, CG_FRAME_WIDTH - 1, PROP_W - 1, PEN_DGREY);
        pc_box(&c, 0, 0, CG_FRAME_WIDTH - 1, PROP_W - 1, BOX_REV);
        pc_fill(&c, ky + 1, 2, ky + kh - 2, PROP_W - 3, PEN_CREAM);
        pc_box(&c, ky, 1, ky + kh - 1, PROP_W - 2, BOX_STANDARD);
        pc_blit(&c, rgba, frame_w(u), frame_h(u), 0, crawl_slider_y(u), u->scale);
        pc_free(&c);
        return;
    }
    if (cg_edit_page(u->e)->type != CG_PAGE_SCROLL || !pc_init(&c, PROP_W, CG_FRAME_HEIGHT))
        return;
    prop_knob(u, &ky, &kh);
    pc_fill(&c, 0, 0, PROP_W - 1, CG_FRAME_HEIGHT - 1, PEN_DGREY);
    pc_box(&c, 0, 0, PROP_W - 1, CG_FRAME_HEIGHT - 1, BOX_REV);
    pc_fill(&c, 2, ky + 1, PROP_W - 3, ky + kh - 2, PEN_CREAM);
    pc_box(&c, 1, ky, PROP_W - 2, ky + kh - 1, BOX_STANDARD);
    pc_blit(&c, rgba, frame_w(u), frame_h(u), CG_FRAME_WIDTH - PROP_W, 0, u->scale);
    pc_free(&c);
}

static void draw_overlays(struct cg_ui *u, uint8_t *rgba)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    const struct cg_line *line;
    struct cg_line *l, tmp;
    double *xs;
    int i;

    /* selected characters; a crawl's are all on its merged line */
    for (l = p->lines; l; l = p->type == CG_PAGE_CRAWL ? NULL : l->next)
    {
        if (cg_line_is_graphic(u->e->book, l) && cg_edit_line_selected(l))
        {
            page_rect(u, rgba, l->xoffset, face_top(l), l->xoffset + l->face_width - 1,
                         face_top(l) + l->face_height - 1, 255, 255, 255, 160, 1);
            continue;
        }
        line = text_line(u, l, &tmp);
        if (cg_edit_line_selected(line) && (xs = cg_render_line_xs(e->r, e->book, line)))
        {
            for (i = 0; i < cg_line_length(line); i++)
                if (line->chars[i].select)
                    page_rect(u, rgba, line->xoffset + (int)xs[i], line->yoffset,
                              line->xoffset + (int)xs[i + 1] - 1, line->yoffset + line->total_height - 1,
                              120, 160, 255, 110, 0);
            free(xs);
        }
        if (tmp.chars)
            cg_crawl_free(&tmp);
    }
    if (!e->line)
        return;
    l = e->line;
    if (cg_line_is_graphic(u->e->book, l))
    {
        /* the box, and its resize corner */
        page_rect(u, rgba, l->xoffset - 1, face_top(l) - 1, l->xoffset + l->face_width,
                     face_top(l) + l->face_height, 255, 255, 255, 220, 1);
        page_rect(u, rgba, l->xoffset + l->face_width - 8, face_top(l) + l->face_height - 8,
                     l->xoffset + l->face_width, face_top(l) + l->face_height, 255, 255, 255, 220, 0);
    }
    else
    {
        int x = (int)(char_x(u, l, e->cursor) + 0.5);

        page_rect(u, rgba, x - 1, l->yoffset, x, l->yoffset + l->total_height - 1, 255, 255, 255, 230, 0);
        page_rect(u, rgba, x - 1, l->yoffset, x, l->yoffset + l->total_height - 1, 0, 0, 0, 100, 1);
    }
}

/* DrawSafeArea(): where titles stay visible on any set */
static void draw_safe_area(struct cg_ui *u, uint8_t *rgba)
{
    overlay_rect(u, rgba, CG_SAFE_LEFT, CG_SAFE_TOP, CG_SAFE_RIGHT, CG_SAFE_BOTTOM, 255, 255, 255, 40, 1);
}

static const char *const help_lines[] = {
    "     F1  Shadow type           F6  Justify",
    "     F2  Light direction       F7  Render page to disk",
    "     F3  Shadow length         F8  Render line",
    "     F4  Border                F9  Render page",
    "     F5  Shadow/border front   F10 Take",
    "",
    "                  Esc  Cancel/Switcher",
    "            PgUp or -  Previous page",
    "            PgDn or +  Next page",
    "                 Home  Top of page",
    "                  End  Bottom of page",
    "           Shift-Help  Menu bar on/off",
    "             Alt-Help  Preview interface on/off",
    "     Left/Right Amiga  Page/book menu",
    "  Shift/Alt arrows     Move line",
    "            Alt-click  Select next layer",
    "         Double-click  Select line",
    "   Shift Double-click  Select page",
    "  [Shift-] Alt F1-F10  Run preset ARexx macros",
    "",
    "       Video Toaster CG  Version 4.1 Mar  7 1996",
    "       (c) 1990,1996 NewTek, Inc.  AROS port " CG_VERSTRING,
    NULL };

/* the gadget showing for a range of IDs (a group's current state) */
static struct gad *find_range(struct cg_ui *u, int lo, int hi)
{
    int i;

    for (i = 0; i < u->ngads; i++)
        if (u->gads[i].id >= lo && u->gads[i].id <= hi)
            return &u->gads[i];
    return NULL;
}

/* Help's row of F-key numbers over the icons they work, as the CG 4.1
   help screen has it; F7 (Render page to disk) has no icon: its number
   goes a slot before F8's */
static void draw_fkey_labels(struct cg_ui *u, uint8_t *rgba)
{
    static const int groups[10][2] = {
        { ID_SHADOW_NONE, ID_SHADOW_CAST }, { ID_SHADOW_NORTH, ID_SHADOW_NW }, { ID_SHADOW_1, ID_SHADOW_5 },
        { ID_OUTLINE_NONE, ID_OUTLINE_3 }, { ID_OUTLINE_PRIORITY, ID_OUTLINE_PRIORITY_1 },
        { ID_JUSTIFY_NONE, ID_JUSTIFY_SKIP }, { -1, -1 }, { ID_RENDER_LINE, ID_RENDER_LINE },
        { ID_RENDER_PAGE, ID_RENDER_PAGE }, { ID_TAKE, ID_TAKE } };
    struct pcanvas c;
    int cx[10], f, any = 0;

    if (!u->bar_on || u->bar_y < 16 || !pc_init(&c, BAR_WIDTH, 14))
        return;
    for (f = 0; f < 10; f++)
    {
        struct gad *g = groups[f][0] >= 0 ? find_range(u, groups[f][0], groups[f][1]) : NULL;

        cx[f] = g ? g->x + g->w / 2 : -1;
    }
    if (cx[7] >= 0 && cx[8] >= 0)
        cx[6] = cx[7] - (cx[8] - cx[7]);
    pc_fill(&c, 0, 0, BAR_WIDTH - 1, 13, PEN_DGREY);
    for (f = 0; f < 10; f++)
        if (cx[f] >= 0)
        {
            static const char *const label[10] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10" };

            pc_text(&c, label[f], cx[f] - pc_text_width(label[f]) / 2, 11, 0);
            any = 1;
        }
    if (any)
        pc_blit(&c, rgba, frame_w(u), frame_h(u), bar_x(), u->bar_y - 14, u->scale);
    pc_free(&c);
}

static void draw_help(struct cg_ui *u, uint8_t *rgba)
{
    struct pcanvas c;
    int i, n, w = 560, h = 0, y;

    char mem[2][72];
    int nmem = 0;

    /* free memory, as the CG's help showed it, when the host can tell */
    if (u->host && u->host->memory)
    {
        unsigned long g = 0, o = 0;

        u->host->memory(u->host->ud, &g, &o);
        snprintf(mem[0], sizeof(mem[0]), "       Available Memory  Graphics: %lu KB", g >> 10);
        snprintf(mem[1], sizeof(mem[1]), "                            Other: %lu KB", o >> 10);
        nmem = 2;
    }
    for (i = 0; help_lines[i]; i++)
        h += 16;
    h += 24 + nmem * 16;
    if (!pc_init(&c, w, h))
        return;
    pc_fill(&c, 0, 0, w - 1, h - 1, PEN_DGREY);
    pc_box(&c, 0, 0, w - 1, h - 1, BOX_STANDARD);
    pc_box(&c, 4, 4, w - 5, h - 5, BOX_REV);
    for (i = 0; help_lines[i]; i++)
        pc_text(&c, help_lines[i], 12, 22 + i * 16, w - 24);
    for (n = 0; n < nmem; n++)
        pc_text(&c, mem[n], 12, 22 + (i + n) * 16, w - 24);
    /* centred, and clear of the F-key numbers over the bar */
    y = (CG_FRAME_HEIGHT - h) / 2;
    if (u->bar_on && y + h > u->bar_y - 16)
        y = u->bar_y - 16 - h;
    if (y < 0)
        y = 0;
    pc_blit(&c, rgba, frame_w(u), frame_h(u), (CG_FRAME_WIDTH - w) / 2, y, u->scale);
    pc_free(&c);
}

static void draw_popup(struct cg_ui *u, uint8_t *rgba)
{
    struct popup *p = &u->pop;
    struct pcanvas c;
    int i, rows = p->n < POP_ROWS ? p->n : POP_ROWS;

    if (!pc_init(&c, p->w, p->h))
        return;
    pc_fill(&c, 0, 0, p->w - 1, p->h - 1, PEN_DGREY);
    pc_box(&c, 0, 0, p->w - 1, p->h - 1, BOX_STANDARD);
    if (p->top > 0)
        pc_image(&c, &art(ID_POPUP_UP)->render, (p->w - 18) / 2, 1);
    if (p->top + rows < p->n)
        pc_image(&c, &art(ID_POPUP_DOWN)->render, (p->w - 18) / 2, p->h - 12);
    for (i = 0; i < rows; i++)
    {
        int y = 12 + i * 16;

        if (p->top + i == p->hi)
            pc_fill(&c, 3, y, p->w - 4, y + 15, PEN_CREAM);
        pc_text(&c, p->names[p->top + i], 8, y + 13, p->w - 16);
    }
    pc_blit(&c, rgba, frame_w(u), frame_h(u), p->x, p->y, u->scale);
    pc_free(&c);
}

void cg_ui_draw(struct cg_ui *u, uint8_t *rgba)
{
    size_t size = (size_t)frame_w(u) * frame_h(u) * 4;

    if (!u->page)
        u->page = malloc(size);
    if (u->page && !u->page_valid)
    {
        memset(u->page, 0, size);
        cg_render_page_view(u->e->r, u->e->book, u->e->page, u->page, frame_w(u), frame_h(u), u->scale / 100.0,
                            cg_edge(u->view_x, u->scale), cg_edge(u->view_y, u->scale));
        cg_render_flatten(u->page, frame_w(u), frame_h(u));
        u->page_valid = 1;
    }
    if (u->page)
        memcpy(rgba, u->page, size);
    if (!u->preview)
    {
        draw_safe_area(u, rgba);
        draw_overlays(u, rgba);
        draw_prop(u, rgba);
    }
    if (u->bar_on)
    {
        layout(u);
        draw_bar(u);
        pc_blit(&u->bar, rgba, frame_w(u), frame_h(u), bar_x(), u->bar_y, u->scale);
        /* RenderThick(): the swatch shows the colour itself */
        if (u->bar_mode == BAR_COLOR)
        {
            const struct gad *g = find(u, ID_COLOR_SWATCH);
            const struct cg_rgba *c = slider_color(u);

            if (g)
                overlay_rect(u, rgba, bar_x() + g->x + 3, u->bar_y + g->y + 3, bar_x() + g->x + g->w - 4,
                             u->bar_y + g->y + g->h - 4, c->r, c->g, c->b, 255, 0);
        }
    }
    if (u->pop.open)
        draw_popup(u, rgba);
    if (u->help)
    {
        draw_help(u, rgba);
        draw_fkey_labels(u, rgba);
    }
}

/************************************************************ requesters */

static int ask(struct cg_ui *u, const char *a, const char *b, int okcancel)
{
    const char *lines[2] = { a, b };

    if (!u->host || !u->host->request)
        return 1;
    return u->host->request(u->host->ud, lines, b ? 2 : 1, okcancel);
}

void cg_ui_preview(struct cg_ui *u)
{
    u->preview = 1;
    page_dirty(u);
}

int cg_ui_may_quit(struct cg_ui *u)
{
    return !u->e->dirty || ask(u, "The book has changed.", "Leave the CG without saving it?", 1);
}

static void tell_message(struct cg_ui *u)
{
    if (u->e->message)
    {
        u->status = u->e->message;
        u->e->message = NULL;
        u->transient = 1;
    }
}

/* a message from the last action stays until the next input */
static void clear_transient(struct cg_ui *u)
{
    if (u->transient)
    {
        u->status = NULL;
        u->transient = 0;
    }
}

void cg_ui_status(struct cg_ui *u, const char *message)
{
    if (message)
    {
        snprintf(u->status_buf, sizeof(u->status_buf), "%s", message);
        u->status = u->status_buf;
        u->transient = 1;
    }
    else
        u->status = NULL;
}

/* what a file requester is for: each opens where it was last left */
enum { FILE_FONT, FILE_PAGE, FILE_BACKGROUND, FILE_BRUSH, FILE_TEXT, FILE_BOOK };

static int ask_file(struct cg_ui *u, int kind, const char *title, const char *dir_default, char *path, size_t len,
                    int save)
{
    char file[CG_RESULT_MAX] = "", dir[CG_RESULT_MAX];

    snprintf(dir, sizeof(dir), "%s", u->dirs[kind][0] ? u->dirs[kind] : dir_default);
    if (!u->host || !u->host->file || !u->host->file(u->host->ud, title, file, sizeof(file), dir, sizeof(dir), save) ||
        !file[0])
        return 0;
    snprintf(u->dirs[kind], sizeof(u->dirs[kind]), "%s", dir);
    cg_join_path(path, len, dir, file);
    return 1;
}

static const char *config_path(struct cg_ui *u, int i, const char *fallback)
{
    return u->cmd && u->cmd->paths[i][0] ? u->cmd->paths[i] : fallback;
}

/************************************************************ bar actions */

static void go_normal(struct cg_ui *u)
{
    u->bar_mode = BAR_NORMAL;
    u->status = NULL;
}

static void set_attr(struct cg_ui *u, enum cg_attr_field f)
{
    cg_edit_set_attr(u->e, f);
    tell_message(u);
    page_dirty(u);
}

static void cycle_shadow_type(struct cg_ui *u)
{
    struct cg_attr *a = &u->e->attr;

    if (cg_edit_all_select_same(u->e, CG_ATTR_SHADOW_TYPE))
    {
        a->shadow_type = (uint8_t)(a->shadow_type >= 2 ? 0 : a->shadow_type + 1);
        if (a->shadow_type && a->shadow_length < 2)
            a->shadow_length = 2;
    }
    set_attr(u, CG_ATTR_SHADOW_TYPE);
}

static void cycle_shadow_dir(struct cg_ui *u)
{
    struct cg_attr *a = &u->e->attr;

    if (!a->shadow_type)
        return;
    if (cg_edit_all_select_same(u->e, CG_ATTR_SHADOW_DIRECTION))
        a->shadow_direction = (uint8_t)((a->shadow_direction + 1) & 7);
    set_attr(u, CG_ATTR_SHADOW_DIRECTION);
}

static void cycle_shadow_length(struct cg_ui *u)
{
    struct cg_attr *a = &u->e->attr;

    if (!a->shadow_type)
        return;
    if (cg_edit_all_select_same(u->e, CG_ATTR_SHADOW_LENGTH))
        a->shadow_length = (uint8_t)(a->shadow_length >= 10 ? 2 : a->shadow_length + 2);
    set_attr(u, CG_ATTR_SHADOW_LENGTH);
}

static void cycle_outline(struct cg_ui *u)
{
    struct cg_attr *a = &u->e->attr;

    if (cg_edit_all_select_same(u->e, CG_ATTR_OUTLINE_TYPE))
        a->outline_type = (uint8_t)(a->outline_type >= 3 ? 0 : a->outline_type + 1);
    set_attr(u, CG_ATTR_OUTLINE_TYPE);
}

static void toggle_priority(struct cg_ui *u)
{
    struct cg_attr *a = &u->e->attr;
    struct cg_page *p = cg_edit_page(u->e);

    if (!(a->shadow_type || a->outline_type) || p->type == CG_PAGE_SCROLL || p->type == CG_PAGE_EMPTY)
        return;
    if (cg_edit_all_select_same(u->e, CG_ATTR_SHADOW_PRIORITY))
        a->shadow_priority ^= 1;
    set_attr(u, CG_ATTR_SHADOW_PRIORITY);
}

static void cycle_justify(struct cg_ui *u)
{
    struct cg_page *p = cg_edit_page(u->e);

    if (!p->type || p->type == CG_PAGE_CRAWL)
        return;
    cg_edit_set_justify(u->e, u->e->justify >= CG_JUSTIFY_RIGHT ? CG_JUSTIFY_NONE : u->e->justify + 1);
    page_dirty(u);
}

/* Horizontal center / lower: the selected lines (or all) as one block,
   centred on the frame or set on the bottom of the safe area */
static void place_vertically(struct cg_ui *u, int lower)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l;
    int any = cg_edit_any_selected(e), top = 32767, bottom = -32768, dy;

    for (l = p->lines; l; l = l->next)
        if (!any || cg_edit_line_selected(l))
        {
            if (l->yoffset < top)
                top = l->yoffset;
            if (l->yoffset + l->total_height > bottom)
                bottom = l->yoffset + l->total_height;
        }
    if (top > bottom)
        return;
    dy = lower ? CG_SAFE_BOTTOM - bottom : (CG_FRAME_HEIGHT - (bottom - top)) / 2 - top;
    for (l = p->lines; l; l = l->next)
        if (!any || cg_edit_line_selected(l))
            l->yoffset = (int16_t)(l->yoffset + dy);
    e->dirty = 1;
    page_dirty(u);
}

static void cycle_speed(struct cg_ui *u)
{
    struct cg_page *p = cg_edit_page(u->e);

    if (p->type == CG_PAGE_CRAWL)
        p->speed = (uint8_t)(p->speed >= 3 ? 0 : p->speed + 1);
    else
        p->speed = (uint8_t)(p->speed >= 8 ? 0 : p->speed + 2);
    u->e->dirty = 1;
}

static void cycle_playback(struct cg_ui *u)
{
    struct cg_page *p = cg_edit_page(u->e);
    int max = p->type == CG_PAGE_CRAWL ? 0 : 1;

    p->playback = (int8_t)(p->playback >= max ? -1 : p->playback + 1);
    u->e->dirty = 1;
}

static void set_page_type(struct cg_ui *u, int type)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    int trivial = !p->lines || (!p->lines->next && !cg_line_length(p->lines));

    if (type != p->type &&
        (trivial || ask(u, "Changing the page type may cause some", "information to be lost.", 1)))
    {
        cg_edit_set_page_type(e, type);
        page_dirty(u);
    }
    go_normal(u);
}

static void goto_page(struct cg_ui *u, int n)
{
    cg_edit_goto_page(u->e, n);
    u->view_x = u->view_y = 0;
    page_dirty(u);
}

static void copy_page(struct cg_ui *u)
{
    long n = u->e->page + 1;
    char prompt[48];

    snprintf(prompt, sizeof(prompt), "Copy page %d\nto which page number?", u->e->page);
    if (!u->host || !u->host->number || !u->host->number(u->host->ud, prompt, &n))
        return;
    if (n < 0 || n >= CG_PAGES_PER_BOOK || n == u->e->page)
        return;
    if (u->e->book->pages[n].used && !ask(u, "Replace the page you are copying to?", NULL, 1))
        return;
    cg_edit_copy_page(u->e, (int)n);
}

static void ask_page_number(struct cg_ui *u)
{
    long n = u->e->page;

    if (u->host && u->host->number && u->host->number(u->host->ud, "Enter new page number.", &n) &&
        n >= 0 && n < CG_PAGES_PER_BOOK)
        goto_page(u, (int)n);
}

static int add_font(struct cg_ui *u)
{
    char path[CG_RESULT_MAX], prompt[CG_RESULT_MAX + 40], *c;
    const char *name;
    long h = 40;
    struct cg_data *d;

    if (!ask_file(u, FILE_FONT, "Load font", config_path(u, 0, "Toaster:PSFonts"), path, sizeof(path), 0))
        return 0;
    /* bitmap fonts carry their height; PostScript fonts are asked for one */
    name = strrchr(path, '/') ? strrchr(path, '/') + 1 : strrchr(path, ':') ? strrchr(path, ':') + 1 : path;
    snprintf(prompt, sizeof(prompt), "Select height from 10 to 400 for\n%s", name);
    for (c = prompt; *c; c++)
        if (*c == '%')
            *c = ' ';
    if (!strstr(path, "olorfonts") && !strstr(path, "OldFonts") && u->host && u->host->number &&
        !u->host->number(u->host->ud, prompt, &h))
        return 0;
    if (h < 10)
        h = 10;
    if (h > 400)
        h = 400;
    u->status = "Loading font...";
    if (!(d = cg_cmd_add_font(u->cmd, path, (int)h)))
        ask(u, "Unable to load font.", path, 0);
    else
        set_attr(u, CG_ATTR_ID);
    u->status = NULL;
    return d != NULL;
}

static int add_brush(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];

    if (!ask_file(u, FILE_BRUSH, "Load brush", config_path(u, 3, "Toaster:Brushes"), path, sizeof(path), 0))
        return 0;
    if (!cg_cmd_add_brush(u->cmd, path))
    {
        ask(u, u->e->message ? u->e->message : "Unable to load brush.", path, 0);
        return 0;
    }
    set_attr(u, CG_ATTR_ID);
    return 1;
}

/* Load Draw: a PostScript graphic, as the CG's "Graphic Text Control"
   panel made one (Text, Font, Rotate, Shear): text in the current
   PostScript font, rotated and sheared, drawn in the line's colours */
static void load_draw(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    const struct cg_data *font = cg_book_find_data(e->book, (uint16_t)e->attr.id);
    char text[CG_DRAW_TEXT_MAX + 1] = "";
    long rot = 0, shear = 0;
    struct cg_data *d;

    if (!font || font->type != CG_DATA_FONT || cg_render_font_type(e->r, font) != 4)
    {
        ask(u, "Please, Load a PostScript font first.", NULL, 0);
        return;
    }
    if (!u->host || !u->host->string || !u->host->number ||
        !u->host->string(u->host->ud, "Graphic Text Control\nText", text, sizeof(text)) || !text[0] ||
        !u->host->number(u->host->ud, "Rotate (0 to 360)", &rot) ||
        !u->host->number(u->host->ud, "Shear (0 to 89)", &shear))
        return;
    rot = ((rot % 360) + 360) % 360;
    shear = shear < 0 ? 0 : shear > 89 ? 89 : shear;
    if (!(d = cg_edit_add_draw(e, font, text, (int)rot, (int)shear)))
        return;
    e->attr.id = (int16_t)d->id;
    set_attr(u, CG_ATTR_ID);
    page_dirty(u);
}

/* Font Clear: the current font or brush out of the book, after asking
   (the CG binary's own messages; its handler is not in the release). One
   still used on a page stays: removing it would empty those lines. */
static void font_clear(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct cg_data **pp, *d;
    char q[CG_RESULT_MAX + 8];

    for (pp = &e->book->data; (d = *pp) && d->id != (uint16_t)e->attr.id; pp = &d->next)
        ;
    if (!d)
    {
        ask(u, "We do not remove boxes.", NULL, 0);
        return;
    }
    snprintf(q, sizeof(q), "%s", d->filename);
    if (!ask(u, "Are you sure you want to remove", q, 1))
        return;
    if (cg_edit_data_used(e, d->id))
    {
        ask(u, d->type == CG_DATA_FONT ? "We dare not remove this font." : "Unable to remove this entry.", NULL, 0);
        return;
    }
    *pp = d->next;
    free(d->filename);
    free(d->drawdata);
    free(d);
    e->attr.id = e->book->data ? (int16_t)e->book->data->id : 0;
    e->dirty = 1;
    set_attr(u, CG_ATTR_ID);
}

/* the Info panel, as NewGadgets.c's CaseFontInfo lays it out */
static void font_info(struct cg_ui *u)
{
    static const char *const font_types[] = { "", "ToasterFont (one color)", "Toaster ColorFont (millions of colors)",
                                              "Asian bitmap font", "PostScript font", "Amiga outline font" };
    const struct cg_data *d = cg_book_find_data(u->e->book, (uint16_t)u->e->attr.id);
    char name[CG_RESULT_MAX], path[CG_RESULT_MAX * 2], notice[CG_RESULT_MAX], size[64], disk[64], kern[64],
        wrap[5][64];
    const char *lines[18], *s;
    long disksize = 0;
    int n = 0, kp = -1, type, w = 0, h = 0;

    if (!d)
    {
        const struct cg_line *l = u->e->line;

        /* a box: what it is and its size */
        if (!l || !cg_line_is_graphic(u->e->book, l))
            return;
        snprintf(size, sizeof(size), "Height: %d x %d", l->face_height, l->face_width);
        lines[n++] = "Box";
        lines[n++] = size;
        ask(u, lines[0], lines[1], 0);
        return;
    }
    s = strrchr(d->filename, '/') ? strrchr(d->filename, '/') + 1 : strrchr(d->filename, ':') ? strrchr(d->filename, ':') + 1 : d->filename;
    snprintf(name, sizeof(name), "%s", s);
    if (strlen(name) > 4 && !strcasecmp(name + strlen(name) - 4, ".pfb"))
        name[strlen(name) - 4] = 0;
    lines[n++] = name;
    cg_render_data_info(u->e->r, d, path, sizeof(path), &disksize, notice, sizeof(notice), &kp);
    if (d->type == CG_DATA_FONT)
    {
        /* the notice in up to five lines, broken at spaces (ParseLongString) */
        for (s = notice; *s && n < 6;)
        {
            size_t len = strlen(s), cut = len < 60 ? len : 60;

            if (cut < len)
                while (cut > 20 && s[cut] != ' ')
                    cut--;
            snprintf(wrap[n - 1], sizeof(wrap[0]), "%.*s", (int)cut, s);
            lines[n] = wrap[n - 1];
            n++;
            s += cut;
            while (*s == ' ')
                s++;
        }
        type = cg_render_font_type(u->e->r, d);
        lines[n++] = " ";
        lines[n++] = type >= 0 && type <= 5 ? font_types[type] : "";
        if (type == 4)
        {
            if (kp > 0)
            {
                snprintf(kern, sizeof(kern), "Kerning pairs: %d", kp);
                lines[n++] = kern;
            }
            else
                lines[n++] = "No kerning pairs available";
        }
        cg_render_font_metrics(u->e->r, d, &h, &w);
        snprintf(size, sizeof(size), "Height: %d", type == 4 ? d->height : h);
    }
    else
    {
        lines[n++] = " ";
        lines[n++] = "Brush";
        cg_render_brush_size(u->e->r, d, &w, &h);
        snprintf(size, sizeof(size), "Height: %d x %d", h, w);
    }
    lines[n++] = size;
    if (disksize > 0)
    {
        snprintf(disk, sizeof(disk), "Disk size: %ld bytes", disksize);
        lines[n++] = disk;
    }
    lines[n++] = "Current path:";
    lines[n++] = d->filename;
    if (u->host && u->host->request)
        u->host->request(u->host->ud, lines, n, 0);
}

static void save_book(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];
    const char *err;
    FILE *f;

    if (!ask_file(u, FILE_BOOK, "Save book", "Toaster:CG/Books", path, sizeof(path), 1))
        return;
    if ((f = fopen(path, "r")))
    {
        fclose(f);
        if (!ask(u, "Replace existing book", path, 1))
            return;
    }
    if (cg_book_save(u->e->book, path, &err))
    {
        u->e->dirty = 0;
        ask(u, "Successfully saved book", path, 0);
    }
    else
        ask(u, "Unable to save book", path, 0);
}

static void load_book(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];
    const char *err;
    struct cg_book *b;

    if (u->e->dirty && !ask(u, "The book has changed.", "Load another without saving it?", 1))
        return;
    if (!ask_file(u, FILE_BOOK, "Load book", "Toaster:CG/Books", path, sizeof(path), 0))
        return;
    /* a page file goes onto the current page; a book replaces the book */
    if (!(b = cg_book_load(path, &err)))
    {
        ask(u, "Unable to open", path, 0);
        return;
    }
    if (u->host && u->host->load_book && u->host->load_book(u->host->ud, path))
    {
        cg_book_free(b);
        page_dirty(u);
        return;
    }
    cg_book_free(b);
    if (!cg_page_load(u->e, path, &err))
        ask(u, "Unable to open", path, 0);
    page_dirty(u);
}

static void save_page(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];
    const char *err;

    if (cg_edit_page(u->e)->type == CG_PAGE_EMPTY)
        return;
    if (!ask_file(u, FILE_PAGE, "Save page", config_path(u, 1, "Toaster:CG"), path, sizeof(path), 1))
        return;
    if (!cg_page_save(u->e, path, &err))
        ask(u, err, path, 0);
    else
        cg_ui_status(u, "Page saved");
}

static int load_text(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];

    int ok;

    if (!ask_file(u, FILE_TEXT, "Select text file", config_path(u, 4, "Toaster:CG/TextFiles"), path, sizeof(path), 0))
        return 0;
    u->status = "Now reading text file...";
    if (!(ok = cg_edit_load_text(u->e, path)))
        ask(u, "Unable to read text file.", path, 0);
    u->status = NULL;
    tell_message(u);
    page_dirty(u);
    return ok;
}

static int load_background(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];

    int ok;

    if (!ask_file(u, FILE_BACKGROUND, "Select background image", config_path(u, 2, "Toaster:Framestore"), path, sizeof(path), 0))
        return 0;
    if (!(ok = u->host && u->host->load_background && u->host->load_background(u->host->ud, path)))
        ask(u, "Unable to load background image", path, 0);
    page_dirty(u);
    return ok;
}

int cg_ui_load(struct cg_ui *u, int what)
{
    switch (what)
    {
    case CG_ASK_FONT:
        return add_font(u);
    case CG_ASK_BRUSH:
        return add_brush(u);
    case CG_ASK_TEXT:
        return load_text(u);
    default:
        return load_background(u);
    }
}

/* slider and swatch changes go to the selection as they happen */
static void color_changed(struct cg_ui *u)
{
    if (u->col_target == COL_BKG)
        u->e->dirty = 1;
    else
        cg_edit_set_attr(u->e, CG_ATTR_COLORS);
    page_dirty(u);
}

static void open_popup(struct cg_ui *u, struct gad *g)
{
    struct cg_editor *e = u->e;
    struct popup *p = &u->pop;
    const struct cg_data *d;
    int i, rows;

    memset(p, 0, sizeof(*p));
    p->gadget = g->id;
    if (g->id == ID_CURRENT_FONT)
    {
        for (d = e->book->data; d && p->n < POP_MAX - 1; d = d->next)
        {
            const char *n = strrchr(d->filename, '/');

            n = n ? n + 1 : d->filename;
            if (d->type == CG_DATA_BRUSH)
                snprintf(p->names[p->n], sizeof(p->names[0]), "Brush %s", n);
            else
                snprintf(p->names[p->n], sizeof(p->names[0]), "%s %d", n, d->height);
            p->values[p->n] = d->id;
            if (d->id == (uint16_t)e->attr.id)
                p->hi = p->n;
            p->n++;
        }
        snprintf(p->names[p->n], sizeof(p->names[0]), "Box");
        p->values[p->n] = CG_BOX_ID;
        if (e->attr.id == CG_BOX_ID)
            p->hi = p->n;
        p->n++;
    }
    else
        for (i = 0; i < 10; i++)
        {
            snprintf(p->names[i], sizeof(p->names[0]), "%s", preset_names[i]);
            p->values[i] = i;
            p->n++;
        }
    rows = p->n < POP_ROWS ? p->n : POP_ROWS;
    p->w = g->w;
    p->h = rows * 16 + 24;
    p->x = bar_x() + g->x;
    p->top = p->hi > rows / 2 ? p->hi - rows / 2 : 0;
    if (p->top + rows > p->n)
        p->top = p->n - rows;
    p->y = u->bar_y + g->y + g->h / 2 - (12 + (p->hi - p->top) * 16 + 8);
    if (p->y + p->h > CG_FRAME_HEIGHT)
        p->y = CG_FRAME_HEIGHT - p->h;
    if (p->y < 0)
        p->y = 0;
    p->open = 1;
}

static void popup_chosen(struct cg_ui *u)
{
    struct cg_editor *e = u->e;
    struct popup *p = &u->pop;
    int v = p->values[p->hi];

    p->open = 0;
    if (p->gadget == ID_COLOR_DEFAULTS)
    {
        struct cg_rgba *c = slider_color(u);
        uint8_t a = c->a;

        *c = preset_colors[v];
        c->a = a;
        color_changed(u);
        return;
    }
    if (v == CG_BOX_ID)
    {
        /* "Box": a new box at the cursor, the size of the last one */
        int x = e->line ? e->line->xoffset : CG_SAFE_LEFT, y = e->line ? e->line->yoffset : CG_SAFE_TOP;

        if (cg_edit_make_box(e, x, y, u->box_w, u->box_h))
            page_dirty(u);
        tell_message(u);
        return;
    }
    e->attr.id = (int16_t)v;
    set_attr(u, CG_ATTR_ID);
}

static int bar_action(struct cg_ui *u, struct gad *g, int x, int y)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    int id = g->id;

    switch (id)
    {
    case ID_PAGE_EMPTY:
    case ID_PAGE_STATIC:
    case ID_PAGE_BUFFER:
    case ID_PAGE_SCROLL:
    case ID_PAGE_CRAWL:
        if (u->bar_mode == BAR_PAGE_TYPE)
            set_page_type(u, id - ID_PAGE_EMPTY);
        else
        {
            u->bar_mode = BAR_PAGE_TYPE;
            u->status = msg_choose_page;
        }
        break;
    case ID_PAGE_COMMANDS:
        u->bar_mode = BAR_PAGE_CMDS;
        u->status = msg_controls;
        break;
    case ID_COLOR:
        u->bar_mode = BAR_COLOR;
        u->col_target = COL_TEXT;
        u->col_bottom = 0;
        u->status = p->type == CG_PAGE_BUFFER ? msg_choose_color : msg_choose_color_nobg;
        break;
    case ID_SHADOW_NONE:
    case ID_SHADOW_DROP:
    case ID_SHADOW_CAST:
        cycle_shadow_type(u);
        break;
    case ID_SHADOW_NORTH:
    case ID_SHADOW_NE:
    case ID_SHADOW_EAST:
    case ID_SHADOW_SE:
    case ID_SHADOW_SOUTH:
    case ID_SHADOW_SW:
    case ID_SHADOW_WEST:
    case ID_SHADOW_NW:
        cycle_shadow_dir(u);
        break;
    case ID_SHADOW_1:
    case ID_SHADOW_2:
    case ID_SHADOW_3:
    case ID_SHADOW_4:
    case ID_SHADOW_5:
        cycle_shadow_length(u);
        break;
    case ID_OUTLINE_NONE:
    case ID_OUTLINE_1:
    case ID_OUTLINE_2:
    case ID_OUTLINE_3:
        cycle_outline(u);
        break;
    case ID_OUTLINE_PRIORITY:
    case ID_OUTLINE_PRIORITY_1:
        toggle_priority(u);
        break;
    case ID_JUSTIFY_NONE:
    case ID_JUSTIFY_LEFT:
    case ID_JUSTIFY_CENTER:
    case ID_JUSTIFY_RIGHT:
    case ID_JUSTIFY_SKIP:
        cycle_justify(u);
        break;
    case ID_HORIZ_CENTER:
    case ID_HORIZ_LOWER:
        place_vertically(u, id == ID_HORIZ_LOWER);
        break;
    case ID_COPY_PAGE:
        copy_page(u);
        break;
    case ID_RENDER_LINE:
    case ID_RENDER_PAGE:
        u->preview = 1;
        page_dirty(u);
        break;
    case ID_TAKE:
        return CG_UI_TAKE;
    case ID_PLAY_ONCE:
    case ID_PLAY_FREEZE:
    case ID_PLAY_FOREVER:
        cycle_playback(u);
        break;
    case ID_SCROLL_SPEED_1:
    case ID_SCROLL_SPEED_2:
    case ID_SCROLL_SPEED_3:
    case ID_SCROLL_SPEED_4:
    case ID_SCROLL_SPEED_5:
    case ID_CRAWL_SPEED_1:
    case ID_CRAWL_SPEED_2:
    case ID_CRAWL_SPEED_3:
    case ID_CRAWL_SPEED_4:
        cycle_speed(u);
        break;
    case ID_SWITCHER:
        if (!cg_ui_may_quit(u))
            break;
        return CG_UI_QUIT;
    case ID_BAR_CONTINUE:
        go_normal(u);
        break;
    /* the up arrow is _CasePageDown in the CG binary: the next page */
    case ID_BAR_PAGE_UP:
        goto_page(u, e->page + 1);
        break;
    case ID_BAR_PAGE_DOWN:
        goto_page(u, e->page - 1);
        break;
    case ID_MESSAGE:
    case ID_PAGE_TYPE:
        u->drag_bar = 1;
        u->drag_dy = y;
        break;
    case ID_CURRENT_FONT:
    case ID_COLOR_DEFAULTS:
        open_popup(u, g);
        break;
    case ID_PAGE_NUMBER:
        ask_page_number(u);
        break;
    case ID_DEPTH_FRONT:
    case ID_DEPTH_BACK:
        cg_edit_depth(e, id == ID_DEPTH_FRONT);
        page_dirty(u);
        break;
    case ID_FONT_ADD:
        add_font(u);
        break;
    case ID_LOAD_DRAW:
        load_draw(u);
        break;
    case ID_BRUSH_ADD:
        add_brush(u);
        break;
    case ID_FONT_INFO:
        font_info(u);
        break;
    case ID_FONT_CLEAR:
        font_clear(u);
        break;
    case ID_SAVE_BOOK:
        save_book(u);
        break;
    case ID_LOAD_BOOK:
        load_book(u);
        break;
    case ID_SAVE_PAGE:
        save_page(u);
        break;
    case ID_LOAD_TEXT:
        load_text(u);
        break;
    case ID_ERASE_LINE:
        cg_edit_erase_line(e);
        page_dirty(u);
        break;
    case ID_ERASE_PAGE:
        if (ask(u, "Erase this entire page?", NULL, 1))
        {
            cg_edit_erase_page(e);
            page_dirty(u);
        }
        break;
    case ID_ERASE_BOOK:
        if (ask(u, "Are you sure you want", "to erase this book?", 1))
        {
            cg_edit_erase_book(e);
            page_dirty(u);
        }
        break;
    case ID_COLOR_BKG:
    case ID_COLOR_TEXT:
    case ID_COLOR_SHADOW:
    case ID_COLOR_OUTLINE:
        u->col_target = id - ID_COLOR_BKG;
        u->col_bottom = 0;
        break;
    case ID_COLOR_SOLID:
    case ID_COLOR_GRADATION:
    case ID_COLOR_COM_RGB:
        p->background = (uint8_t)(id - ID_COLOR_SOLID);
        u->col_bottom = 0;
        e->dirty = 1;
        page_dirty(u);
        break;
    case ID_COLOR_TOP:
    case ID_TEXTCOLOR_TOP:
    case ID_OLCOLOR_TOP:
        u->col_bottom = 0;
        break;
    case ID_COLOR_BOTTOM:
    case ID_TEXTCOLOR_BOTTOM:
    case ID_OLCOLOR_BOTTOM:
        u->col_bottom = 1;
        break;
    case ID_TEXTCOLOR_SOLID:
    case ID_TEXTCOLOR_GRADATION:
        if (id == ID_TEXTCOLOR_GRADATION)
            e->attr.special_fill |= CG_FILL_FACE_GRAD;
        else
            e->attr.special_fill &= ~CG_FILL_FACE_GRAD;
        u->col_bottom = 0;
        set_attr(u, CG_ATTR_FILL);
        break;
    case ID_OLCOLOR_SOLID:
    case ID_OLCOLOR_GRADATION:
        if (id == ID_OLCOLOR_GRADATION)
            e->attr.special_fill |= CG_FILL_OUTLINE_GRAD;
        else
            e->attr.special_fill &= ~CG_FILL_OUTLINE_GRAD;
        u->col_bottom = 0;
        set_attr(u, CG_ATTR_FILL);
        break;
    case ID_COLOR_RED:
    case ID_COLOR_GREEN:
    case ID_COLOR_BLUE:
    case ID_COLOR_ALPHA:
    {
        int v = (x - g->x - 3) * 255 / (g->w - 6);

        *slider_value(u, id) = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        u->slider = id;
        color_changed(u);
        break;
    }
    case ID_LOAD_RGB:
        load_background(u);
        break;
    }
    return CG_UI_REDRAW;
}

/************************************************************ the page, by mouse */

static int line_width(struct cg_ui *u, const struct cg_line *line)
{
    struct cg_line tmp;
    const struct cg_line *l;
    double *xs;
    int w = 8;

    if (cg_line_is_graphic(u->e->book, line))
        return line->face_width;
    l = text_line(u, line, &tmp);
    if (cg_line_length(l) && (xs = cg_render_line_xs(u->e->r, u->e->book, l)))
    {
        w = (int)(xs[l->nchars] + 0.5);
        free(xs);
    }
    if (tmp.chars)
        cg_crawl_free(&tmp);
    return w;
}

static int line_hit(struct cg_ui *u, const struct cg_line *l, int x, int y)
{
    int top = cg_line_is_graphic(u->e->book, l) ? face_top(l) : l->yoffset;
    int h = cg_line_is_graphic(u->e->book, l) ? l->face_height : l->total_height;

    return x >= l->xoffset && x < l->xoffset + line_width(u, l) && y >= top && y < top + h;
}

/* what is under the mouse, text first, then boxes and brushes front to
   back; Alt-click takes the one after the current line */
static struct cg_line *hit(struct cg_ui *u, int x, int y, int next_layer)
{
    struct cg_page *p = cg_edit_page(u->e);
    struct cg_line *list[256], *l;
    int n = 0, pri, i;

    for (l = p->lines; l && n < 256; l = p->type == CG_PAGE_CRAWL ? NULL : l->next)
        if (!cg_line_is_graphic(u->e->book, l) && line_hit(u, l, x, y))
            list[n++] = l;
    for (pri = 255; pri >= 0; pri--)
        for (l = p->lines; l && n < 256; l = l->next)
            if (cg_line_is_graphic(u->e->book, l) && l->render_pri == pri && line_hit(u, l, x, y))
                list[n++] = l;
    if (!n)
        return NULL;
    if (next_layer)
        for (i = 0; i < n; i++)
            if (list[i] == u->e->line)
                return list[(i + 1) % n];
    return list[0];
}

static int char_at(struct cg_ui *u, const struct cg_line *line, int x)
{
    struct cg_line tmp;
    const struct cg_line *l = text_line(u, line, &tmp);
    double *xs;
    int i, n = cg_line_length(l);

    if (n && !cg_line_is_graphic(u->e->book, l) && (xs = cg_render_line_xs(u->e->r, u->e->book, l)))
    {
        for (i = 0; i < n && x >= l->xoffset + (xs[i] + xs[i + 1]) / 2; i++)
            ;
        free(xs);
        n = i;
    }
    else
        n = 0;
    if (tmp.chars)
        cg_crawl_free(&tmp);
    return n;
}

static void page_down(struct cg_ui *u, int x, int y, unsigned qual, unsigned ms)
{
    struct cg_editor *e = u->e;
    struct cg_page *p = cg_edit_page(e);
    struct cg_line *l, *cur = e->line;
    int dbl = ms - u->last_ms < DOUBLE_CLICK_MS && abs(x - u->mx0) < 4 && abs(y - u->my0) < 4;

    u->last_ms = ms;
    u->mx0 = u->mx = x;
    u->my0 = u->my = y;
    u->drag = DRAG_NONE;
    u->drag_line = NULL;
    if (p->type == CG_PAGE_EMPTY)
    {
        u->status = "Change page type to add text.";
        return;
    }
    /* the current box's corner sizes it */
    if (cur && cg_line_is_graphic(u->e->book, cur) && x >= cur->xoffset + cur->face_width - 10 &&
        x <= cur->xoffset + cur->face_width + 2 && y >= face_top(cur) + cur->face_height - 10 &&
        y <= face_top(cur) + cur->face_height + 2)
    {
        u->drag = DRAG_SIZE;
        u->drag_line = cur;
        return;
    }
    l = hit(u, x, y, qual & CG_QUAL_ALT);
    if (dbl && l)
    {
        if (qual & CG_QUAL_SHIFT)
            cg_edit_select_page(e, 1);
        else
            cg_edit_select_line(e, l, 1);
        page_dirty(u);
        return;
    }
    if (!l)
    {
        cg_edit_select_page(e, 0);
        if (p->type != CG_PAGE_SCROLL && (l = cg_edit_new_line(e, x, y)))
        {
            l->yoffset = (int16_t)(y - l->total_height / 2);
            cg_edit_justify_line(e, l);
        }
        tell_message(u);
        page_dirty(u);
        return;
    }
    cg_edit_set_line(e, l, char_at(u, l, x));
    cg_edit_take_attr(e);
    u->drag_line = l;
    u->drag_char = e->cursor;
    if (qual & CG_QUAL_SHIFT)
    {
        cg_edit_select_page(e, 0);
        u->drag = DRAG_SELECT;
    }
    else
    {
        if (!cg_edit_line_selected(l))
            cg_edit_select_page(e, 0);
        u->drag = DRAG_MOVE;
    }
    page_dirty(u);
}

static void page_move(struct cg_ui *u, int x, int y)
{
    struct cg_editor *e = u->e;
    struct cg_line *l = u->drag_line;
    int i, a, b;

    if (!l)
        return;
    switch (u->drag)
    {
    case DRAG_MOVE:
        if (x != u->mx || y != u->my)
        {
            if (!cg_edit_line_selected(l))
            {
                struct cg_line *keep = e->line;

                e->line = l;
                cg_edit_move_selected(e, x - u->mx, y - u->my);
                e->line = keep;
            }
            else
                cg_edit_move_selected(e, x - u->mx, y - u->my);
        }
        break;
    case DRAG_SIZE:
        a = x - l->xoffset;
        b = y - face_top(l);
        l->face_width = (uint16_t)(a < 4 ? 4 : a > CG_FRAME_WIDTH ? CG_FRAME_WIDTH : a);
        l->face_height = (uint16_t)(b < 4 ? 4 : b > CG_FRAME_HEIGHT ? CG_FRAME_HEIGHT : b);
        cg_edit_fix_line(e, l);
        /* boxes default to the last size, kept under 640 by 400 */
        u->box_w = l->face_width < 640 ? l->face_width : 639;
        u->box_h = l->face_height < 400 ? l->face_height : 399;
        e->dirty = 1;
        break;
    case DRAG_SELECT:
        if (cg_edit_page(e)->type == CG_PAGE_CRAWL)
        {
            cg_edit_select_page(e, 1);
            break;
        }
        a = u->drag_char;
        b = char_at(u, l, x);
        cg_edit_select_line(e, l, 0);
        for (i = a < b ? a : b; i < (a < b ? b : a); i++)
            cg_edit_select_char(e, l, i, 1);
        cg_edit_set_line(e, l, b);
        break;
    }
    u->mx = x;
    u->my = y;
    page_dirty(u);
}

/************************************************************ events */

static int in_bar(struct cg_ui *u, int x, int y, int *bx, int *by)
{
    *bx = x - bar_x();
    *by = y - u->bar_y;
    return u->bar_on && *bx >= 0 && *bx < BAR_WIDTH + 1 && *by >= 0 && *by < u->bar_h;
}

static int gadget_at(struct cg_ui *u, int bx, int by)
{
    int i;

    for (i = 0; i < u->ngads; i++)
    {
        struct gad *g = &u->gads[i];

        if (g->id != ID_BLANK && g->id != ID_COLOR_SWATCH && bx >= g->x && bx < g->x + g->w && by >= g->y &&
            by < g->y + g->h)
            return i;
    }
    return -1;
}

static int popup_item(struct cg_ui *u, int x, int y)
{
    struct popup *p = &u->pop;
    int rows = p->n < POP_ROWS ? p->n : POP_ROWS, i;

    if (x < p->x || x >= p->x + p->w || y < p->y || y >= p->y + p->h)
        return -1;
    if (y < p->y + 12)
        return p->top > 0 ? p->top - 1 : p->top;
    i = (y - p->y - 12) / 16;
    if (i >= rows)
        return p->top + rows < p->n ? p->top + rows : p->n - 1;
    return p->top + i;
}

static void popup_track(struct cg_ui *u, int x, int y)
{
    struct popup *p = &u->pop;
    int i = popup_item(u, x, y), rows = p->n < POP_ROWS ? p->n : POP_ROWS;

    if (i < 0)
        return;
    p->hi = i;
    if (p->hi < p->top)
        p->top = p->hi;
    if (p->hi >= p->top + rows)
        p->top = p->hi - rows + 1;
}

int cg_ui_mouse(struct cg_ui *u, int event, int x, int y, unsigned qual, unsigned ms)
{
    int fx = cg_logical(x, u->scale), fy = cg_logical(y, u->scale), bx, by, i, r = CG_UI_REDRAW;

    switch (event)
    {
    case CG_MOUSE_RDOWN:
        u->pop.open = 0;
        u->help = 0;
        u->preview = 0;
        return CG_UI_REDRAW;

    case CG_MOUSE_DOWN:
        clear_transient(u);
        if (u->help || u->preview)
        {
            u->help = u->preview = 0;
            return CG_UI_REDRAW;
        }
        if (u->pop.open)
        {
            if (popup_item(u, fx, fy) >= 0)
            {
                popup_track(u, fx, fy);
                popup_chosen(u);
            }
            else
                u->pop.open = 0;
            return CG_UI_REDRAW;
        }
        u->mdown = 1;
        if (in_bar(u, fx, fy, &bx, &by))
        {
            layout(u);
            if ((i = gadget_at(u, bx, by)) < 0)
                return 0;
            u->pressed = i;
            u->press_inside = 1;
            /* GACT_IMMEDIATE gadgets act on the press */
            if (art(u->gads[i].id)->activation & 0x0002 || (u->gads[i].id >= ID_COLOR_RED &&
                                                            u->gads[i].id <= ID_COLOR_ALPHA))
            {
                u->pressed = -1;
                r = bar_action(u, &u->gads[i], bx, by);
            }
            return r;
        }
        if (cg_edit_page(u->e)->type == CG_PAGE_SCROLL && fx >= CG_FRAME_WIDTH - PROP_W)
        {
            u->drag_view = 1;
            set_view_from_prop(u, fy);
            return CG_UI_REDRAW;
        }
        if (cg_edit_page(u->e)->type == CG_PAGE_CRAWL && fy >= crawl_slider_y(u) && fy < crawl_slider_y(u) + PROP_W)
        {
            u->drag_view = 2;
            set_cursor_from_slider(u, fx);
            follow(u);
            return CG_UI_REDRAW;
        }
        page_down(u, fx + u->view_x, fy + u->view_y, qual, ms);
        follow(u);
        return CG_UI_REDRAW;

    case CG_MOUSE_MOVE:
        if (u->drag_bar)
        {
            u->bar_y = fy - u->drag_dy;
            if (u->bar_y < 0)
                u->bar_y = 0;
            if (u->bar_y + u->bar_h > CG_FRAME_HEIGHT)
                u->bar_y = CG_FRAME_HEIGHT - u->bar_h;
            return CG_UI_REDRAW;
        }
        if (u->slider)
        {
            struct gad *g = find(u, u->slider);
            int v;

            if (!g)
                return 0;
            v = (fx - bar_x() - g->x - 3) * 255 / (g->w - 6);
            *slider_value(u, u->slider) = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            color_changed(u);
            return CG_UI_REDRAW;
        }
        if (u->pop.open)
        {
            popup_track(u, fx, fy);
            return CG_UI_REDRAW;
        }
        if (u->pressed >= 0 && u->mdown)
        {
            int inside = in_bar(u, fx, fy, &bx, &by) && gadget_at(u, bx, by) == u->pressed;

            if (inside != u->press_inside)
            {
                u->press_inside = inside;
                return CG_UI_REDRAW;
            }
            return 0;
        }
        if (u->drag_view == 2)
        {
            set_cursor_from_slider(u, fx);
            follow(u);
            return CG_UI_REDRAW;
        }
        if (u->drag_view)
        {
            set_view_from_prop(u, fy);
            return CG_UI_REDRAW;
        }
        if (u->mdown && u->drag)
        {
            page_move(u, fx + u->view_x, fy + u->view_y);
            return CG_UI_REDRAW;
        }
        return 0;

    case CG_MOUSE_UP:
        u->mdown = 0;
        u->drag_view = 0;
        u->drag_bar = 0;
        u->slider = 0;
        u->drag = DRAG_NONE;
        if (u->pop.open && popup_item(u, fx, fy) >= 0 && (abs(fx - u->mx0) > 2 || abs(fy - u->my0) > 2))
        {
            popup_track(u, fx, fy);
            popup_chosen(u);
        }
        if (u->pressed >= 0)
        {
            i = u->pressed;
            u->pressed = -1;
            if (u->press_inside && in_bar(u, fx, fy, &bx, &by) && gadget_at(u, bx, by) == i)
            {
                u->mx0 = fx;
                u->my0 = fy;
                r = bar_action(u, &u->gads[i], bx, by);
            }
        }
        return r;
    }
    return 0;
}

static void nudge(struct cg_ui *u, int dx, int dy)
{
    cg_edit_move_selected(u->e, dx, dy);
    page_dirty(u);
}

static int run_macro(struct cg_ui *u, int n)
{
    /* without an interpreter (no REXX port) a macro key would do nothing
       at all: say why */
    if (u->cmd && n < CG_MACROS && u->cmd->macros[n][0] && strcmp(u->cmd->macros[n], "unused") &&
        u->host && u->host->macro && !u->host->macro(u->host->ud, u->cmd->macros[n]))
        cg_ui_status(u, "No ARexx interpreter is running (no REXX port).");
    return CG_UI_REDRAW;
}

/* FunctionKeyTable: F1-F10 are the icons from shadow type to Take */
/* F7, _CaseRenderDisk in newrender.a's FunctionKeyTable: the page as
   the Toaster would show it, written to a file (renderdisk.a is not in the
   release; its names, DoRenderDisk and WriteRenderBuffer, say this much):
   a 24-bit ILBM at the frame's size, which Load RGB takes back */
static void render_disk(struct cg_ui *u)
{
    char path[CG_RESULT_MAX];
    const char *err = NULL;
    uint8_t *rgba;

    if (cg_edit_page(u->e)->type == CG_PAGE_EMPTY)
        return;
    if (!ask_file(u, FILE_BACKGROUND, "Render page to disk", config_path(u, 2, "Toaster:Framestore"), path, sizeof(path), 1))
        return;
    if (!(rgba = calloc((size_t)CG_FRAME_WIDTH * CG_FRAME_HEIGHT, 4)))
        return;
    u->status = "Rendering page to disk...";
    if (!cg_render_page(u->e->r, u->e->book, u->e->page, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, 1.0) ||
        !cg_ilbm_save(path, rgba, CG_FRAME_WIDTH, CG_FRAME_HEIGHT, &err))
        ask(u, "Unable to render page to disk.", err ? err : path, 0);
    u->status = NULL;
    free(rgba);
}

static int function_key(struct cg_ui *u, int f)
{
    switch (f)
    {
    case 0:
        cycle_shadow_type(u);
        break;
    case 1:
        cycle_shadow_dir(u);
        break;
    case 2:
        cycle_shadow_length(u);
        break;
    case 3:
        cycle_outline(u);
        break;
    case 4:
        toggle_priority(u);
        break;
    case 5:
        cycle_justify(u);
        break;
    case 6:
        render_disk(u);
        break;
    case 7:
    case 8:
        u->preview = 1;
        break;
    case 9:
        return CG_UI_TAKE;
    }
    return CG_UI_REDRAW;
}

static int key(struct cg_ui *u, int raw, int ascii, unsigned qual);

int cg_ui_key(struct cg_ui *u, int raw, int ascii, unsigned qual)
{
    int r;

    clear_transient(u);
    r = key(u, raw, ascii, qual);

    follow(u);
    return r;
}

/* the numeric keypad moves as newrender.a's RawKeyTable has it: 1 End,
   2 down, 3 page down, 4 left, 6 right, 7 Home, 8 up, 9 page up; 0, 5 and
   "." do nothing (CGBetaChanges, Beta 3) */
static int keypad(int raw)
{
    switch (raw)
    {
    case 0x1d: return CG_RAW_END;
    case 0x1e: return CG_RAW_DOWN;
    case 0x1f: return CG_RAW_PAGEDOWN;
    case 0x2d: return CG_RAW_LEFT;
    case 0x2f: return CG_RAW_RIGHT;
    case 0x3d: return CG_RAW_HOME;
    case 0x3e: return CG_RAW_UP;
    case 0x3f: return CG_RAW_PAGEUP;
    case 0x0f:
    case 0x2e:
    case 0x3c: return -1;
    }
    return raw;
}

static int key(struct cg_ui *u, int raw, int ascii, unsigned qual)
{
    struct cg_editor *e = u->e;
    int step = qual & CG_QUAL_ALT ? 8 : 1;

    if ((raw = keypad(raw)) < 0)
        return 0;
    if (raw == CG_RAW_RAMIGA)
        raw = CG_RAW_LAMIGA;
    if (u->help || u->preview)
    {
        if (raw != CG_RAW_LAMIGA)
            u->help = u->preview = 0;
        return CG_UI_REDRAW;
    }
    if (u->pop.open)
    {
        struct popup *p = &u->pop;

        if (raw == CG_RAW_UP && p->hi > 0)
            p->hi--;
        else if (raw == CG_RAW_DOWN && p->hi < p->n - 1)
            p->hi++;
        else if (raw == CG_RAW_RETURN || raw == CG_RAW_ENTER)
            popup_chosen(u);
        else if (raw == CG_RAW_ESC)
            p->open = 0;
        if (p->hi < p->top)
            p->top = p->hi;
        if (p->hi >= p->top + POP_ROWS)
            p->top = p->hi - POP_ROWS + 1;
        return CG_UI_REDRAW;
    }
    e->message = NULL;
    switch (raw)
    {
    case CG_RAW_ESC:
        if (u->bar_mode == BAR_NORMAL)
            return cg_ui_may_quit(u) ? CG_UI_QUIT : 0;
        go_normal(u);
        return CG_UI_REDRAW;
    case CG_RAW_HELP:
        if (qual & CG_QUAL_SHIFT)
            u->bar_on = !u->bar_on;
        else if (qual & CG_QUAL_ALT)
            u->preview = !u->preview;
        else
            u->help = 1;
        return CG_UI_REDRAW;
    case CG_RAW_LAMIGA:
        u->bar_mode = u->bar_mode == BAR_PAGE_CMDS ? BAR_NORMAL : BAR_PAGE_CMDS;
        u->status = u->bar_mode == BAR_PAGE_CMDS ? msg_controls : NULL;
        return CG_UI_REDRAW;
    case CG_RAW_UP:
        if (qual & (CG_QUAL_SHIFT | CG_QUAL_ALT))
            nudge(u, 0, -step);
        else
            cg_edit_up(e);
        return CG_UI_REDRAW;
    case CG_RAW_DOWN:
        if (qual & (CG_QUAL_SHIFT | CG_QUAL_ALT))
            nudge(u, 0, step);
        else
            cg_edit_down(e);
        return CG_UI_REDRAW;
    case CG_RAW_LEFT:
        if (qual & (CG_QUAL_SHIFT | CG_QUAL_ALT))
            nudge(u, -step, 0);
        else
            cg_edit_left(e);
        return CG_UI_REDRAW;
    case CG_RAW_RIGHT:
        if (qual & (CG_QUAL_SHIFT | CG_QUAL_ALT))
            nudge(u, step, 0);
        else
            cg_edit_right(e);
        return CG_UI_REDRAW;
    case CG_RAW_PAGEUP:
    case CG_RAW_KP_MINUS:
        goto_page(u, e->page - 1);
        return CG_UI_REDRAW;
    case CG_RAW_PAGEDOWN:
    case CG_RAW_KP_PLUS:
        goto_page(u, e->page + 1);
        return CG_UI_REDRAW;
    case CG_RAW_HOME:
        cg_edit_home(e);
        return CG_UI_REDRAW;
    case CG_RAW_END:
        cg_edit_end(e);
        return CG_UI_REDRAW;
    case CG_RAW_RETURN:
    case CG_RAW_ENTER:
        cg_edit_return(e);
        break;
    case CG_RAW_BACKSPACE:
        cg_edit_backspace(e);
        break;
    case CG_RAW_DEL:
        cg_edit_delete(e);
        break;
    case CG_RAW_TAB:
        cg_edit_tab(e);
        break;
    default:
        if (raw >= CG_RAW_F1 && raw <= CG_RAW_F10)
        {
            if (qual & CG_QUAL_ALT)
                return run_macro(u, raw - CG_RAW_F1 + (qual & CG_QUAL_SHIFT ? 10 : 0));
            if (u->bar_mode != BAR_PAGE_TYPE)
                return function_key(u, raw - CG_RAW_F1);
            return 0;
        }
        if (ascii < 32 || ascii == 127 || (qual & (CG_QUAL_LAMIGA | CG_QUAL_RAMIGA | CG_QUAL_CTRL)))
            return 0;
        cg_edit_insert(e, (uint16_t)ascii);
        break;
    }
    tell_message(u);
    page_dirty(u);
    return CG_UI_REDRAW;
}

/************************************************************ life */

struct cg_ui *cg_ui_new(struct cg_editor *e, struct cg_cmd *cmd, struct cg_host *host, int percent)
{
    struct cg_ui *u = calloc(1, sizeof(*u));

    if (!u)
        return NULL;
    u->e = e;
    u->cmd = cmd;
    u->host = host;
    u->scale = percent < 100 ? 100 : percent;
    u->bar_on = 1;
    u->pressed = -1;
    u->bar_y = CG_FRAME_HEIGHT - BAR_0_H;
    u->box_w = 200;
    u->box_h = 100;
    layout(u);
    return u;
}

void cg_ui_set_scale(struct cg_ui *u, int percent)
{
    if (percent < 100)
        percent = 100;
    if (percent != u->scale)
    {
        u->scale = percent;
        free(u->page);
        u->page = NULL;
        u->page_valid = 0;
    }
}

void cg_ui_free(struct cg_ui *u)
{
    if (!u)
        return;
    pc_free(&u->bar);
    free(u->page);
    free(u);
}
