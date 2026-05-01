#include <stdint.h>
#include <stddef.h>
#include "browser.h"
#include "fb.h"
#include "http.h"
#include "console.h"
#include "str.h"
#include "heap.h"

/*
 * Tiny HTML browser. Pipeline:
 *   1. http_get fetches the response into a kalloc'd body buffer.
 *   2. parse_html walks it linearly, emitting "items" (text spans
 *      with style flags, line breaks, <hr> rules) and recording
 *      links with their hrefs.
 *   3. layout_and_paint word-wraps the items inside the canvas
 *      widget's rect, calling fb_draw_string_ui / _hd for spans.
 *      Link spans get blue + underline + their bbox is recorded.
 *   4. browser_canvas_click hit-tests the recorded link bboxes.
 */

#define MAX_BODY      32768
#define MAX_ITEMS      512
#define MAX_LINKS      128
#define HOME_HTML \
    "<h1>Hobby ARM OS Browser</h1>" \
    "<p>This is a tiny HTML/1 renderer running on a hand-rolled " \
    "AArch64 kernel. HTTPS isn't supported (no TLS yet) -- plain " \
    "HTTP only.</p>" \
    "<h2>Try a link</h2>" \
    "<ul>" \
      "<li><a href=\"/index.html\">Local repo home</a></li>" \
      "<li><a href=\"/index.json\">Local repo index.json</a></li>" \
      "<li><a href=\"/packages/hello/manifest.json\">hello manifest</a></li>" \
      "<li><a href=\"/packages/minesweeper/manifest.json\">minesweeper manifest</a></li>" \
    "</ul>" \
    "<p>Type a path or full <i>http://</i> URL into the bar above " \
    "and press Enter.</p>"

#define ITEM_TEXT  0
#define ITEM_LBR   1   /* hard line break */
#define ITEM_PARA  2   /* paragraph spacing */
#define ITEM_HR    3
#define ITEM_BULLET 4

#define STYLE_NONE   0x0
#define STYLE_LINK   0x1
#define STYLE_H1     0x2
#define STYLE_H2     0x4
#define STYLE_BOLD   0x8
#define STYLE_ITALIC 0x10

struct item {
    uint8_t  kind;
    uint8_t  style;
    uint16_t link_idx;   /* 0xffff if not a link */
    uint16_t off;        /* offset into text buffer */
    uint16_t len;
};

struct link {
    int      x, y, w, h;     /* layout bbox after paint */
    uint16_t href_off;
    uint16_t href_len;
};

static struct item items[MAX_ITEMS];
static int          item_count;
static struct link  links[MAX_LINKS];
static int          link_count;

/* Pool of all text strings (item bodies + hrefs) */
static char  text_pool[MAX_BODY];
static int   text_used;

static char  current_url[256];
static int   status;
static char  status_msg[64];

#define HISTORY_MAX 16
static char  history[HISTORY_MAX][256];
static int   history_count;

static window_t *win;
static widget_t *url_widget;
static widget_t *canvas_widget;

/* ---- string utilities ---- */

static int sp_str(const char *s) {
    if (text_used + (int)strlen(s) + 1 >= MAX_BODY) return -1;
    int o = text_used;
    int i = 0;
    while (s[i]) text_pool[text_used++] = s[i++];
    text_pool[text_used++] = 0;
    return o;
}

static void reset_pool(void) {
    text_used  = 0;
    item_count = 0;
    link_count = 0;
}

/* ---- url parser ---- */

struct parsed_url {
    char host[64];
    char path[192];
    uint16_t port;
};

static int parse_url(const char *url, struct parsed_url *out) {
    /* defaults: local docker repo */
    int hi = 0; const char *def = "10.0.2.2";
    while (def[hi]) { out->host[hi] = def[hi]; hi++; }
    out->host[hi] = 0;
    out->port = 8090;
    out->path[0] = '/'; out->path[1] = 0;

    const char *p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p') {
        while (*p && *p != '/') p++;
        while (*p == '/') p++;
        out->host[0] = 0;
        int hi2 = 0;
        while (*p && *p != '/' && *p != ':' && hi2 + 1 < (int)sizeof(out->host)) {
            out->host[hi2++] = *p++;
        }
        out->host[hi2] = 0;
        out->port = 80;
        if (*p == ':') {
            p++;
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v) out->port = (uint16_t)v;
        }
        if (*p != '/') {
            out->path[0] = '/'; out->path[1] = 0;
        } else {
            int pi = 0;
            while (*p && pi + 1 < (int)sizeof(out->path)) out->path[pi++] = *p++;
            out->path[pi] = 0;
        }
    } else if (p[0] == '/') {
        int pi = 0;
        while (*p && pi + 1 < (int)sizeof(out->path)) out->path[pi++] = *p++;
        out->path[pi] = 0;
    } else {
        /* host[:port]/... */
        out->host[0] = 0;
        int hi2 = 0;
        while (*p && *p != '/' && *p != ':' && hi2 + 1 < (int)sizeof(out->host)) {
            out->host[hi2++] = *p++;
        }
        out->host[hi2] = 0;
        out->port = 80;
        if (*p == ':') {
            p++;
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v) out->port = (uint16_t)v;
        }
        if (*p == '/') {
            int pi = 0;
            while (*p && pi + 1 < (int)sizeof(out->path)) out->path[pi++] = *p++;
            out->path[pi] = 0;
        } else {
            out->path[0] = '/'; out->path[1] = 0;
        }
    }
    return 0;
}

/* IPv4 parsing of a numeric "a.b.c.d" host string. Hostnames aren't
 * resolved -- DNS is out of scope. */
static int parse_ipv4(const char *s, uint32_t *out) {
    uint32_t a[4] = {0};
    for (int i = 0; i < 4; i++) {
        int v = 0, any = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
        if (!any || v > 255) return -1;
        a[i] = (uint32_t)v;
        if (i < 3) {
            if (*s != '.') return -1;
            s++;
        }
    }
    *out = (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
    return 0;
}

/* ---- HTML parser ---- */

static int eq_lower(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

static int decode_entity(const char *s, int len, char *out) {
    /* simple entity decode; *out gets the decoded char, returns
     * how many input bytes consumed past '&' */
    if (len >= 4 && eq_lower(s, "&lt;", 4))  { *out = '<'; return 4; }
    if (len >= 4 && eq_lower(s, "&gt;", 4))  { *out = '>'; return 4; }
    if (len >= 5 && eq_lower(s, "&amp;", 5)) { *out = '&'; return 5; }
    if (len >= 6 && eq_lower(s, "&quot;",6)) { *out = '"'; return 6; }
    if (len >= 6 && eq_lower(s, "&apos;",6)) { *out = '\''; return 6; }
    if (len >= 6 && eq_lower(s, "&nbsp;",6)) { *out = ' '; return 6; }
    return 0;
}

static void emit_break(int kind) {
    if (item_count >= MAX_ITEMS) return;
    items[item_count].kind = (uint8_t)kind;
    items[item_count].style = 0;
    items[item_count].link_idx = 0xffff;
    items[item_count].off = 0;
    items[item_count].len = 0;
    item_count++;
}

static void emit_text(const char *buf, int n, uint8_t style, int link_idx) {
    if (n <= 0) return;
    if (item_count >= MAX_ITEMS) return;
    if (text_used + n + 1 >= MAX_BODY) return;
    int off = text_used;
    for (int i = 0; i < n; i++) text_pool[text_used++] = buf[i];
    text_pool[text_used++] = 0;
    items[item_count].kind = ITEM_TEXT;
    items[item_count].style = style;
    items[item_count].link_idx = (uint16_t)link_idx;
    items[item_count].off = (uint16_t)off;
    items[item_count].len = (uint16_t)n;
    item_count++;
}

static void parse_html(const char *src, int srclen) {
    reset_pool();

    /* Skip past the HTTP headers if the input still includes them
     * (callers usually strip them, but be defensive). */
    if (srclen > 8 && src[0] == 'H' && src[1] == 'T') {
        const char *eoh = 0;
        for (int i = 0; i < srclen - 3; i++) {
            if (src[i]=='\r' && src[i+1]=='\n' && src[i+2]=='\r' && src[i+3]=='\n') {
                eoh = src + i + 4;
                srclen -= (i + 4);
                src = eoh;
                break;
            }
        }
    }

    uint8_t style = 0;
    int     active_link = -1;
    char    text_buf[1024];
    int     tlen = 0;
    int     last_was_space = 1;
    int     in_skip = 0;   /* inside <script> or <style>, drop content */

    int i = 0;
    while (i < srclen) {
        char c = src[i];

        if (in_skip) {
            if (c == '<' && i + 1 < srclen && src[i+1] == '/') {
                /* check for </script> or </style> */
                int j = i + 2;
                int taglen = 0;
                while (j < srclen && taglen < 16
                       && src[j] != '>' && src[j] != ' ') {
                    j++; taglen++;
                }
                if ((taglen == 6 && eq_lower(src + i + 2, "script", 6)) ||
                    (taglen == 5 && eq_lower(src + i + 2, "style",  5))) {
                    in_skip = 0;
                    while (i < srclen && src[i] != '>') i++;
                    if (i < srclen) i++;
                    continue;
                }
            }
            i++;
            continue;
        }

        if (c == '<') {
            /* flush pending text */
            if (tlen > 0) {
                emit_text(text_buf, tlen, style, active_link);
                tlen = 0;
            }
            int j = i + 1;
            int closing = 0;
            if (j < srclen && src[j] == '/') { closing = 1; j++; }
            int tag_start = j;
            while (j < srclen && src[j] != '>' && src[j] != ' ') j++;
            int tag_len = j - tag_start;
            const char *tag = src + tag_start;

            if (eq_lower(tag, "script", tag_len) && tag_len == 6 && !closing) {
                in_skip = 1;
            } else if (eq_lower(tag, "style", tag_len) && tag_len == 5 && !closing) {
                in_skip = 1;
            } else if (eq_lower(tag, "h1", tag_len) && tag_len == 2) {
                if (closing) { style &= ~STYLE_H1; emit_break(ITEM_PARA); }
                else         { emit_break(ITEM_PARA); style |= STYLE_H1; }
            } else if (eq_lower(tag, "h2", tag_len) && tag_len == 2) {
                if (closing) { style &= ~STYLE_H2; emit_break(ITEM_PARA); }
                else         { emit_break(ITEM_PARA); style |= STYLE_H2; }
            } else if (eq_lower(tag, "h3", tag_len) && tag_len == 2) {
                if (closing) { style &= ~STYLE_H2; emit_break(ITEM_PARA); }
                else         { emit_break(ITEM_PARA); style |= STYLE_H2; }
            } else if (eq_lower(tag, "p", tag_len) && tag_len == 1) {
                emit_break(ITEM_PARA);
            } else if (eq_lower(tag, "br", tag_len) && tag_len == 2) {
                emit_break(ITEM_LBR);
            } else if (eq_lower(tag, "hr", tag_len) && tag_len == 2) {
                emit_break(ITEM_HR);
            } else if (eq_lower(tag, "li", tag_len) && tag_len == 2) {
                if (!closing) { emit_break(ITEM_LBR); emit_break(ITEM_BULLET); }
            } else if (eq_lower(tag, "ul", tag_len) && tag_len == 2) {
                emit_break(ITEM_LBR);
            } else if (eq_lower(tag, "b", tag_len) && tag_len == 1) {
                if (closing) style &= ~STYLE_BOLD; else style |= STYLE_BOLD;
            } else if (eq_lower(tag, "i", tag_len) && tag_len == 1) {
                if (closing) style &= ~STYLE_ITALIC; else style |= STYLE_ITALIC;
            } else if (eq_lower(tag, "a", tag_len) && tag_len == 1) {
                if (closing) {
                    style &= ~STYLE_LINK;
                    active_link = -1;
                } else {
                    /* parse href="..." */
                    char href[192]; int hl = 0;
                    int p = j;
                    while (p < srclen && src[p] != '>') {
                        if (src[p] == 'h' && p + 5 < srclen &&
                            src[p+1]=='r' && src[p+2]=='e' && src[p+3]=='f') {
                            int q = p + 4;
                            while (q < srclen && src[q] == ' ') q++;
                            if (q < srclen && src[q] == '=') q++;
                            while (q < srclen && (src[q] == ' ' || src[q] == '"' || src[q] == '\'')) q++;
                            while (q < srclen && src[q] != '"' && src[q] != '\'' &&
                                   src[q] != ' ' && src[q] != '>' &&
                                   hl + 1 < (int)sizeof(href)) {
                                href[hl++] = src[q++];
                            }
                            href[hl] = 0;
                            break;
                        }
                        p++;
                    }
                    if (hl > 0 && link_count < MAX_LINKS) {
                        int off = sp_str(href);
                        if (off >= 0) {
                            links[link_count].href_off = (uint16_t)off;
                            links[link_count].href_len = (uint16_t)hl;
                            links[link_count].x = links[link_count].y =
                            links[link_count].w = links[link_count].h = 0;
                            active_link = link_count;
                            link_count++;
                            style |= STYLE_LINK;
                        }
                    }
                }
            }
            /* skip to end of tag */
            while (i < srclen && src[i] != '>') i++;
            if (i < srclen) i++;
            last_was_space = 1;
            continue;
        }

        if (c == '&') {
            char entity_ch;
            int  consumed = decode_entity(src + i, srclen - i, &entity_ch);
            if (consumed > 0) {
                if (tlen + 1 < (int)sizeof(text_buf)) text_buf[tlen++] = entity_ch;
                i += consumed;
                last_was_space = 0;
                continue;
            }
        }

        /* whitespace collapse */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_was_space) {
                if (tlen + 1 < (int)sizeof(text_buf)) text_buf[tlen++] = ' ';
                last_was_space = 1;
            }
            i++;
            continue;
        }

        if (tlen + 1 < (int)sizeof(text_buf)) text_buf[tlen++] = c;
        last_was_space = 0;
        i++;
    }
    if (tlen > 0) emit_text(text_buf, tlen, style, active_link);
}

/* ---- layout & paint ---- */

#define LINK_FG   0x002c6cb8u
#define BODY_FG   0x00141a26u
#define DIM_FG    0x00606878u
#define HR_FG     0x00b0b8c8u
#define HEAD_FG   0x000a1530u

static void paint_link_word(int x, int y, int w, int link_idx) {
    /* underline + record bbox */
    if (link_idx >= 0 && link_idx < link_count) {
        struct link *l = &links[link_idx];
        if (l->w == 0) {
            /* first time we're drawing this link this frame */
            l->x = x; l->y = y;
            l->w = w; l->h = (int)fb_text_ui_line_height();
        } else {
            /* extend bbox to cover all words of the link */
            int new_right = x + w;
            int link_right = l->x + l->w;
            if (x < l->x) { l->w += (l->x - x); l->x = x; }
            if (new_right > link_right) l->w = new_right - l->x;
        }
    }
}

static void paint_text_run(int *xp, int *yp, int x0, int x1,
                           const char *txt, uint8_t style, int link_idx) {
    /* Word-wrap: split on spaces, flush full words into lines. */
    int x = *xp, y = *yp;
    int lh_ui = (int)fb_text_ui_line_height();
    int lh_hd = (int)fb_text_hd_line_height();
    int is_h  = (style & (STYLE_H1 | STYLE_H2)) != 0;
    int lh    = is_h ? lh_hd : lh_ui;
    uint32_t color = (style & STYLE_LINK)
                     ? LINK_FG
                     : (style & (STYLE_H1 | STYLE_H2)) ? HEAD_FG : BODY_FG;

    int i = 0;
    int n = (int)strlen(txt);
    while (i < n) {
        /* skip leading spaces -- but emit a space before the next word
         * if we're not at the start of a line */
        int leading_space = 0;
        while (i < n && txt[i] == ' ') { i++; leading_space = 1; }
        if (i >= n) break;
        int wstart = i;
        while (i < n && txt[i] != ' ') i++;
        int wlen = i - wstart;
        char buf[128];
        int copy = wlen < (int)sizeof(buf) - 1 ? wlen : (int)sizeof(buf) - 1;
        for (int k = 0; k < copy; k++) buf[k] = txt[wstart + k];
        buf[copy] = 0;
        int ww = is_h ? (int)fb_text_hd_width(buf) : (int)fb_text_ui_width(buf);
        int sp_w = (is_h ? 12 : 8);

        if (leading_space && x > x0) {
            x += sp_w;
        }
        if (x + ww > x1 && x > x0) {
            x = x0;
            y += lh;
        }
        if (is_h) fb_draw_string_hd((uint32_t)x, (uint32_t)y, buf, color);
        else      fb_draw_string_ui((uint32_t)x, (uint32_t)y, buf, color);

        if (style & STYLE_LINK) {
            /* underline */
            fb_fill_rect((uint32_t)x, (uint32_t)(y + lh - 3),
                         (uint32_t)ww, 1, LINK_FG);
            paint_link_word(x, y, ww, link_idx);
        }
        x += ww;
    }
    *xp = x; *yp = y;
}

static int hits(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* ---- canvas widget callbacks ---- */

static int  scroll_y;

static void canvas_paint(window_t *w, widget_t *self, int abs_x, int abs_y) {
    (void)w;
    int cw = self->w;
    int ch = self->h;
    /* white background */
    fb_fill_rect((uint32_t)abs_x, (uint32_t)abs_y, (uint32_t)cw, (uint32_t)ch,
                 0x00ffffffu);
    /* status bar at the bottom of the canvas */
    int bar_h = 24;
    fb_fill_rect((uint32_t)abs_x, (uint32_t)(abs_y + ch - bar_h),
                 (uint32_t)cw, (uint32_t)bar_h, 0x00f0f0f4u);
    fb_fill_rect((uint32_t)abs_x, (uint32_t)(abs_y + ch - bar_h),
                 (uint32_t)cw, 1, 0x00d0d6e0u);
    char status_line[80];
    int o = 0;
    const char *p = "HTTP ";
    while (*p) status_line[o++] = *p++;
    status_line[o++] = (char)('0' + (status / 100) % 10);
    status_line[o++] = (char)('0' + (status / 10)  % 10);
    status_line[o++] = (char)('0' + status % 10);
    status_line[o++] = ' '; status_line[o++] = ' ';
    for (int k = 0; status_msg[k] && o + 1 < (int)sizeof(status_line); k++)
        status_line[o++] = status_msg[k];
    status_line[o] = 0;
    fb_draw_string_ui((uint32_t)(abs_x + 12),
                      (uint32_t)(abs_y + ch - bar_h + 4),
                      status_line, DIM_FG);

    int x0 = abs_x + 16;
    int x1 = abs_x + cw - 16;
    int y  = abs_y + 12 - scroll_y;
    int x  = x0;
    int lh_ui = (int)fb_text_ui_line_height();

    /* Reset link bboxes so they get recomputed for this frame. */
    for (int i = 0; i < link_count; i++) {
        links[i].x = links[i].y = links[i].w = links[i].h = 0;
    }

    for (int i = 0; i < item_count; i++) {
        struct item *it = &items[i];
        switch (it->kind) {
        case ITEM_LBR:
            x = x0; y += lh_ui;
            break;
        case ITEM_PARA:
            x = x0; y += lh_ui + 4;
            break;
        case ITEM_HR:
            x = x0; y += lh_ui / 2;
            fb_fill_rect((uint32_t)x0, (uint32_t)y,
                         (uint32_t)(x1 - x0), 1, HR_FG);
            y += lh_ui / 2;
            break;
        case ITEM_BULLET:
            paint_text_run(&x, &y, x0, x1, "  - ", 0, -1);
            break;
        case ITEM_TEXT: {
            const char *t = text_pool + it->off;
            paint_text_run(&x, &y, x0, x1, t, it->style,
                           (int)(int16_t)it->link_idx);
            break;
        }
        }
        if (y > abs_y + ch - bar_h) break;  /* bottom; rough clip */
    }
}

static void canvas_click(window_t *w, widget_t *self,
                         int local_x, int local_y, int btn) {
    (void)w; (void)btn;
    int abs_x = self->x;       /* widget origin is local already; the
                                * stored link bboxes are in absolute fb
                                * coords, so translate input. */
    /* We need the widget's framebuffer position to correlate -- the
     * window manager passed local coords. Recompute abs_x/abs_y from
     * the parent window. Since paint_window passes wx/wy, we can stash
     * them in a static during the last paint -- but simpler: walk our
     * link list ourselves with abs coords already recorded. */
    (void)abs_x;

    /* The link rects were recorded in absolute framebuffer coords by
     * canvas_paint. Translate the click to absolute coords here. */
    int win_abs_x = w->x + 1 /* WIN_BORDER */ + self->x;
    int win_abs_y = w->y + 18 /* WIN_TITLE_H approx */ + self->y;
    int abs_click_x = win_abs_x + local_x;
    int abs_click_y = win_abs_y + local_y;

    for (int i = 0; i < link_count; i++) {
        struct link *l = &links[i];
        if (l->w == 0) continue;
        if (hits(abs_click_x, abs_click_y, l->x, l->y, l->w, l->h)) {
            const char *href = text_pool + l->href_off;
            browser_navigate(href);
            return;
        }
    }
}

/* ---- public API ---- */

static void render_homepage(void) {
    parse_html(HOME_HTML, (int)strlen(HOME_HTML));
    status = 200;
    int o = 0;
    const char *m = "homepage";
    while (*m) status_msg[o++] = *m++;
    status_msg[o] = 0;
}

static void set_status(int code, const char *text) {
    status = code;
    int o = 0;
    while (*text && o + 1 < (int)sizeof(status_msg)) status_msg[o++] = *text++;
    status_msg[o] = 0;
}

void browser_navigate(const char *url) {
    if (!url || !url[0]) return;

    /* push current url onto history (if it differs) */
    if (current_url[0] && history_count < HISTORY_MAX) {
        int eq = 0;
        for (int i = 0; ; i++) {
            if (current_url[i] != url[i]) break;
            if (current_url[i] == 0) { eq = 1; break; }
        }
        if (!eq) {
            int j = 0;
            while (current_url[j] && j + 1 < 256) {
                history[history_count][j] = current_url[j]; j++;
            }
            history[history_count][j] = 0;
            history_count++;
        }
    }
    int j = 0;
    while (url[j] && j + 1 < 256) { current_url[j] = url[j]; j++; }
    current_url[j] = 0;
    if (url_widget) widget_set_text(url_widget, current_url);

    /* "about:home" or empty path -> built-in homepage */
    if (current_url[0] == 0 || strcmp(current_url, "about:home") == 0) {
        render_homepage();
        return;
    }

    struct parsed_url pu;
    parse_url(current_url, &pu);
    uint32_t ip;
    if (parse_ipv4(pu.host, &ip) != 0) {
        set_status(0, "no DNS -- enter a numeric host");
        item_count = 0;
        return;
    }

    set_status(0, "fetching...");
    uint8_t *body = (uint8_t *)kalloc(MAX_BODY);
    if (!body) { set_status(0, "out of memory"); return; }
    int code = 0;
    int n = http_get(ip, pu.port, pu.host, pu.path,
                     body, MAX_BODY - 1, &code);
    if (n < 0) {
        kfree(body);
        char buf[40];
        const char *p = "fetch failed (";
        int o = 0; while (*p) buf[o++] = *p++;
        int err = -n;
        if (err >= 100) buf[o++] = (char)('0' + err / 100);
        if (err >=  10) buf[o++] = (char)('0' + (err / 10) % 10);
        buf[o++] = (char)('0' + err % 10);
        buf[o++] = ')';
        buf[o] = 0;
        set_status(0, buf);
        item_count = 0;
        return;
    }
    body[n] = 0;
    parse_html((const char *)body, n);
    kfree(body);
    set_status(code, current_url);
}

static void on_url_submit(window_t *w, widget_t *self) {
    (void)w;
    browser_navigate(self->input);
    widget_input_clear(self);
    /* keep the bar showing the resolved url */
    widget_set_text(self, current_url);
    /* and pre-fill .input so the user can edit it next time */
    int j = 0;
    while (current_url[j] && j + 1 < WIDGET_INPUT_MAX) {
        self->input[j] = current_url[j]; j++;
    }
    self->input[j] = 0;
    self->input_len = j;
}

static void on_back(window_t *w, widget_t *self) {
    (void)w; (void)self;
    if (history_count == 0) return;
    history_count--;
    char tmp[256];
    int j = 0;
    while (history[history_count][j] && j + 1 < 256) {
        tmp[j] = history[history_count][j]; j++;
    }
    tmp[j] = 0;
    /* pop without re-pushing */
    int saved = history_count;
    history_count = HISTORY_MAX;  /* prevent navigate from pushing */
    browser_navigate(tmp);
    history_count = saved;
    /* hack: navigate would have pushed if URL differed; undo if so */
    if (history_count > saved) history_count--;
}

static void on_home(window_t *w, widget_t *self) {
    (void)w; (void)self;
    browser_navigate("about:home");
}

window_t *browser_window(void) {
    if (win) return win;
    win = window_create_widget("Browser", 200, 60, 1100, 700);
    /* Top chrome: Back | Home | URL [............] | Go */
    window_add_button   (win,  10,  6,  70, "Back",   on_back);
    window_add_button   (win,  86,  6,  70, "Home",   on_home);
    url_widget = window_add_text_input(win, 162, 6, 760,
                                       "about:home", on_url_submit);
    window_add_button   (win, 928,  6,  70, "Go",
                         on_url_submit);
    /* Canvas for HTML below */
    canvas_widget = window_add_canvas(win, 0, 36,
                                      win->w - 2 /* borders */,
                                      win->h - 36 - 18,
                                      0,
                                      canvas_paint, canvas_click);
    /* Initial page */
    render_homepage();
    int j = 0;
    const char *home = "about:home";
    while (home[j]) { current_url[j] = home[j]; j++; }
    current_url[j] = 0;
    widget_set_text(url_widget, current_url);
    return win;
}
