#include <stdint.h>
#include "maker.h"
#include "fb.h"
#include "fs.h"
#include "str.h"
#include "console.h"
#include "http.h"
#include "dns.h"
#include "heap.h"

/*
 * Block-style mini IDE. The program is stored as a flat array of
 * `block`s; nested constructs (Repeat / If) use BEGIN/END markers so
 * the interpreter can walk the array without a heap-allocated AST.
 *
 * Each block has a small string `param` the user edits inline:
 *   PRINT  -> the literal text or a $var reference
 *   SET    -> "x = 5" / "x = x + 1"  (very light expression parser)
 *   IF     -> "x > 5"  -- ends at matching END
 *   REPEAT -> "10"    -- ends at matching END
 *   END    -> closes the most recent IF/REPEAT
 */

#define BLOCK_PRINT   1
#define BLOCK_SET     2
#define BLOCK_IF      3
#define BLOCK_REPEAT  4
#define BLOCK_END     5

#define MAX_BLOCKS    128
#define PARAM_MAX     48
#define PROG_NAME_MAX 32

struct mk_block {
    int  type;
    char param[PARAM_MAX];
};

struct mk_var {
    char name[16];
    int  value;
};

#define MAX_VARS 16

static struct mk_block blocks[MAX_BLOCKS];
static int             block_n;
static int             selected = -1;

static char            program_name[PROG_NAME_MAX] = "untitled";
static char            program_author[24]          = "you";

#define OUTPUT_LINES 12
#define OUTPUT_COLS  80
static char            output[OUTPUT_LINES][OUTPUT_COLS + 1];
static int             out_row, out_col;

static window_t *win;
static widget_t *name_input;
static widget_t *param_input;
static widget_t *canvas;
static widget_t *out_canvas;
static widget_t *status_lbl;

/* ---------- block helpers ---------- */

static const char *block_label(int type) {
    switch (type) {
    case BLOCK_PRINT:  return "Print";
    case BLOCK_SET:    return "Set";
    case BLOCK_IF:     return "If";
    case BLOCK_REPEAT: return "Repeat";
    case BLOCK_END:    return "End";
    }
    return "?";
}

static uint32_t block_color(int type) {
    switch (type) {
    case BLOCK_PRINT:  return 0x004a90e2u;
    case BLOCK_SET:    return 0x002caf80u;
    case BLOCK_IF:     return 0x00d8a72cu;
    case BLOCK_REPEAT: return 0x00bd5db5u;
    case BLOCK_END:    return 0x00808890u;
    }
    return 0x00606878u;
}

static void add_block(int type, const char *default_param) {
    if (block_n >= MAX_BLOCKS) return;
    blocks[block_n].type = type;
    int o = 0;
    while (default_param[o] && o < PARAM_MAX - 1) {
        blocks[block_n].param[o] = default_param[o]; o++;
    }
    blocks[block_n].param[o] = 0;
    block_n++;
    selected = block_n - 1;
}

static void delete_selected_block(void) {
    if (selected < 0 || selected >= block_n) return;
    for (int i = selected; i < block_n - 1; i++) blocks[i] = blocks[i + 1];
    block_n--;
    if (selected >= block_n) selected = block_n - 1;
}

static void move_selected(int dir) {
    int i = selected;
    if (i < 0 || i >= block_n) return;
    int j = i + dir;
    if (j < 0 || j >= block_n) return;
    struct mk_block tmp = blocks[i];
    blocks[i] = blocks[j];
    blocks[j] = tmp;
    selected = j;
}

/* ---------- output buffer ---------- */

static void out_clear(void) {
    for (int r = 0; r < OUTPUT_LINES; r++) output[r][0] = 0;
    out_row = 0; out_col = 0;
}
static void out_putc(char c) {
    if (c == '\n') {
        output[out_row][out_col] = 0;
        out_col = 0;
        out_row++;
        if (out_row >= OUTPUT_LINES) {
            for (int r = 0; r < OUTPUT_LINES - 1; r++) {
                int j = 0;
                while (output[r + 1][j] && j < OUTPUT_COLS) {
                    output[r][j] = output[r + 1][j]; j++;
                }
                output[r][j] = 0;
            }
            output[OUTPUT_LINES - 1][0] = 0;
            out_row = OUTPUT_LINES - 1;
        }
        return;
    }
    if (out_col + 1 < OUTPUT_COLS) {
        output[out_row][out_col++] = c;
        output[out_row][out_col] = 0;
    }
}
static void out_puts(const char *s) { while (*s) out_putc(*s++); }
static void out_putint(int v) {
    char buf[16]; int n = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) buf[n++] = '0';
    while (v > 0) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    if (neg) out_putc('-');
    while (n > 0) out_putc(buf[--n]);
}

/* ---------- expression / interpreter ---------- */

static struct mk_var vars[MAX_VARS];
static int           var_n;

static int find_var(const char *name) {
    for (int i = 0; i < var_n; i++) {
        int eq = 1;
        for (int j = 0; j < (int)sizeof(vars[i].name); j++) {
            if (vars[i].name[j] != name[j]) { eq = 0; break; }
            if (name[j] == 0) break;
        }
        if (eq) return i;
    }
    return -1;
}
static int get_var(const char *name) {
    int i = find_var(name);
    return i >= 0 ? vars[i].value : 0;
}
static void set_var(const char *name, int v) {
    int i = find_var(name);
    if (i >= 0) { vars[i].value = v; return; }
    if (var_n >= MAX_VARS) return;
    int j = 0;
    while (name[j] && j + 1 < (int)sizeof(vars[var_n].name)) {
        vars[var_n].name[j] = name[j]; j++;
    }
    vars[var_n].name[j] = 0;
    vars[var_n].value = v;
    var_n++;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}
static int parse_int(const char **sp) {
    const char *s = skip_ws(*sp);
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    *sp = s;
    return neg ? -v : v;
}
static int parse_word(const char **sp, char *out, int max) {
    const char *s = skip_ws(*sp);
    int o = 0;
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
           (*s >= '0' && *s <= '9') || *s == '_') {
        if (o + 1 < max) out[o++] = *s;
        s++;
    }
    out[o] = 0;
    *sp = s;
    return o;
}
static int parse_value(const char **sp) {
    const char *s = skip_ws(*sp);
    if ((*s >= '0' && *s <= '9') || *s == '-') return parse_int(sp);
    char w[16];
    if (parse_word(sp, w, sizeof(w)) > 0) return get_var(w);
    return 0;
}

/* "x = 5" / "x = x + 1" / "x = x - 1" -- set body */
static void exec_set(const char *p) {
    char name[16];
    parse_word(&p, name, sizeof(name));
    p = skip_ws(p);
    if (*p == '=') p++;
    int lhs = parse_value(&p);
    p = skip_ws(p);
    if (*p == '+' || *p == '-' || *p == '*') {
        char op = *p++;
        int rhs = parse_value(&p);
        if (op == '+') lhs = lhs + rhs;
        if (op == '-') lhs = lhs - rhs;
        if (op == '*') lhs = lhs * rhs;
    }
    set_var(name, lhs);
}

/* "x > 5" / "x == y" / "x < 10" */
static int eval_cond(const char *p) {
    int lhs = parse_value(&p);
    p = skip_ws(p);
    char op1 = *p ? *p++ : 0;
    char op2 = (*p == '=') ? *p++ : 0;
    (void)op2;
    int rhs = parse_value(&p);
    if (op1 == '>') return lhs > rhs;
    if (op1 == '<') return lhs < rhs;
    if (op1 == '=') return lhs == rhs;
    if (op1 == '!') return lhs != rhs;
    return 0;
}

static void exec_print(const char *p) {
    p = skip_ws(p);
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1] == 'n') { out_putc('\n'); p += 2; continue; }
            out_putc(*p++);
        }
        out_putc('\n');
        return;
    }
    /* otherwise treat as a var reference */
    char name[16];
    if (parse_word(&p, name, sizeof(name)) > 0) {
        out_putint(get_var(name));
    } else {
        out_puts(p);
    }
    out_putc('\n');
}

/* Find matching END for the construct starting at idx (inclusive) */
static int find_match_end(int idx) {
    int depth = 0;
    for (int i = idx; i < block_n; i++) {
        int t = blocks[i].type;
        if (t == BLOCK_REPEAT || t == BLOCK_IF) depth++;
        else if (t == BLOCK_END) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static int run_range(int start, int end);   /* fwd decl */

static int run_range(int start, int end) {
    int i = start;
    int instr = 0;
    while (i < end) {
        if (++instr > 50000) {
            out_puts("[runtime] aborted: too many instructions\n");
            return -1;
        }
        struct mk_block *b = &blocks[i];
        switch (b->type) {
        case BLOCK_PRINT: exec_print(b->param); i++; break;
        case BLOCK_SET:   exec_set(b->param);   i++; break;
        case BLOCK_REPEAT: {
            int matched = find_match_end(i);
            if (matched < 0) { out_puts("[runtime] missing End\n"); return -1; }
            const char *p = b->param;
            int n = parse_value(&p);
            for (int k = 0; k < n; k++) {
                if (run_range(i + 1, matched) < 0) return -1;
            }
            i = matched + 1;
            break;
        }
        case BLOCK_IF: {
            int matched = find_match_end(i);
            if (matched < 0) { out_puts("[runtime] missing End\n"); return -1; }
            if (eval_cond(b->param)) {
                if (run_range(i + 1, matched) < 0) return -1;
            }
            i = matched + 1;
            break;
        }
        case BLOCK_END: i++; break;
        default: i++; break;
        }
    }
    return 0;
}

static void program_run(void) {
    out_clear();
    var_n = 0;
    out_puts("[run] start\n");
    run_range(0, block_n);
    out_puts("[run] done\n");
}

/* ---------- JSON serialize / deserialize ---------- */

static int json_escape(char *out, int max, const char *s) {
    int o = 0;
    if (o + 1 >= max) return o;
    out[o++] = '"';
    while (*s) {
        if (o + 2 >= max) break;
        if (*s == '"' || *s == '\\') { out[o++] = '\\'; out[o++] = *s; }
        else if (*s == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else out[o++] = *s;
        s++;
    }
    if (o + 1 >= max) return o;
    out[o++] = '"';
    out[o] = 0;
    return o;
}

static int program_to_json(char *out, int max) {
    int o = 0;
    const char *p = "{\"name\":";
    while (*p && o + 1 < max) out[o++] = *p++;
    o += json_escape(out + o, max - o, program_name);
    p = ",\"author\":";
    while (*p && o + 1 < max) out[o++] = *p++;
    o += json_escape(out + o, max - o, program_author);
    p = ",\"summary\":\"\",\"code\":\"";
    while (*p && o + 1 < max) out[o++] = *p++;
    /* code = compact textual representation of the block list:
     *   <type>|<param>\n
     */
    for (int i = 0; i < block_n; i++) {
        char line[PARAM_MAX + 32];
        int  l = 0;
        line[l++] = (char)('0' + blocks[i].type);
        line[l++] = '|';
        for (int j = 0; blocks[i].param[j] && l + 2 < (int)sizeof(line); j++) {
            line[l++] = blocks[i].param[j];
        }
        line[l++] = '\n';
        line[l] = 0;
        for (int j = 0; line[j] && o + 4 < max; j++) {
            char c = line[j];
            if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
            else if (c == '\n')        { out[o++] = '\\'; out[o++] = 'n'; }
            else out[o++] = c;
        }
    }
    p = "\"}";
    while (*p && o + 1 < max) out[o++] = *p++;
    out[o] = 0;
    return o;
}

static void program_from_text(const char *txt) {
    block_n = 0;
    selected = -1;
    while (*txt && block_n < MAX_BLOCKS) {
        if (!(*txt >= '1' && *txt <= '5')) { txt++; continue; }
        int type = *txt - '0';
        txt++;
        if (*txt == '|') txt++;
        char param[PARAM_MAX]; int p = 0;
        while (*txt && *txt != '\n' && p + 1 < PARAM_MAX) {
            param[p++] = *txt++;
        }
        param[p] = 0;
        if (*txt == '\n') txt++;
        blocks[block_n].type = type;
        for (int j = 0; j < PARAM_MAX; j++) blocks[block_n].param[j] = param[j];
        block_n++;
    }
}

/* ---------- saving / uploading ---------- */

#define MARKETPLACE_IP   ((10u<<24) | (0u<<16) | (2u<<8) | 2u)
#define MARKETPLACE_PORT 8090

static void set_status(const char *s) {
    if (status_lbl) widget_set_text(status_lbl, s);
}

static void save_local(void) {
    /* serialize block list into a small text blob and persist as
     * /maker/<program_name> */
    char body[FS_MAX_DATA];
    int o = 0;
    for (int i = 0; i < block_n && o + PARAM_MAX + 4 < (int)sizeof(body); i++) {
        body[o++] = (char)('0' + blocks[i].type);
        body[o++] = '|';
        for (int j = 0; blocks[i].param[j] && o + 2 < (int)sizeof(body); j++) {
            body[o++] = blocks[i].param[j];
        }
        body[o++] = '\n';
    }
    body[o] = 0;

    /* Build path "maker/<name>" -- folder must already exist or the
     * desktop hides the file; we create the folder marker too. */
    if (!fs_find("maker/")) (void)fs_write("maker/", "", 0);
    char path[FS_MAX_NAME + 1];
    int q = 0;
    const char *p = "maker/";
    while (*p && q + 1 < (int)sizeof(path)) path[q++] = *p++;
    for (int j = 0; program_name[j] && q + 1 < (int)sizeof(path); j++) {
        path[q++] = program_name[j];
    }
    path[q] = 0;

    if (fs_write(path, body, (uint32_t)o) == 0) {
        char msg[80]; int mo = 0;
        const char *m = "saved -> /";
        while (*m) msg[mo++] = *m++;
        for (int j = 0; path[j] && mo + 1 < (int)sizeof(msg); j++)
            msg[mo++] = path[j];
        msg[mo] = 0;
        set_status(msg);
    } else {
        set_status("save failed");
    }
}

static void upload_remote(void) {
    char *body = (char *)kalloc(8192);
    if (!body) { set_status("upload: out of memory"); return; }
    int n = program_to_json(body, 8192);
    static uint8_t resp[1024];
    int code = 0;
    int r = http_post(MARKETPLACE_IP, MARKETPLACE_PORT,
                      "10.0.2.2", "/community",
                      "application/json",
                      (const uint8_t *)body, (uint32_t)n,
                      resp, sizeof(resp) - 1, &code);
    kfree(body);
    if (r < 0) {
        set_status("upload failed (server unreachable?)");
        return;
    }
    if (code != 200) {
        char msg[40]; int mo = 0;
        const char *m = "upload HTTP "; while (*m) msg[mo++] = *m++;
        msg[mo++] = (char)('0' + (code/100)%10);
        msg[mo++] = (char)('0' + (code/10)%10);
        msg[mo++] = (char)('0' + code%10);
        msg[mo] = 0;
        set_status(msg);
        return;
    }
    set_status("uploaded! see Community.");
}

/* ---------- UI: callbacks ---------- */

static void cb_palette(window_t *w, widget_t *self) {
    (void)w;
    int t = (int)(uintptr_t)self->canvas_ctx;
    /* canvas_ctx is repurposed here -- we set it per palette button
     * to hold the block type. */
    add_block(t,
              t == BLOCK_PRINT ? "\"hello\"" :
              t == BLOCK_SET   ? "x = 0" :
              t == BLOCK_IF    ? "x > 0" :
              t == BLOCK_REPEAT? "3" : "");
    set_status("block added");
}

static void cb_run(window_t *w, widget_t *self) {
    (void)w; (void)self;
    program_run();
    set_status("ran program");
}

static void cb_save(window_t *w, widget_t *self) {
    (void)w; (void)self;
    if (name_input) {
        const char *n = widget_input_text(name_input);
        int j = 0;
        while (n[j] && j + 1 < PROG_NAME_MAX) {
            program_name[j] = n[j]; j++;
        }
        program_name[j] = 0;
    }
    save_local();
}

static void cb_upload(window_t *w, widget_t *self) {
    cb_save(w, self);
    set_status("uploading...");
    upload_remote();
}

static void cb_clear(window_t *w, widget_t *self) {
    (void)w; (void)self;
    block_n = 0;
    selected = -1;
    out_clear();
    set_status("cleared");
}

static void cb_delete(window_t *w, widget_t *self) {
    (void)w; (void)self;
    delete_selected_block();
}
static void cb_up(window_t *w, widget_t *self) {
    (void)w; (void)self;
    move_selected(-1);
}
static void cb_down(window_t *w, widget_t *self) {
    (void)w; (void)self;
    move_selected(+1);
}

static void cb_param_submit(window_t *w, widget_t *self) {
    (void)w;
    if (selected < 0 || selected >= block_n) return;
    const char *t = widget_input_text(self);
    int j = 0;
    while (t[j] && j + 1 < PARAM_MAX) {
        blocks[selected].param[j] = t[j]; j++;
    }
    blocks[selected].param[j] = 0;
    set_status("block updated");
}

/* ---------- canvas paint + click ---------- */

#define ROW_H 28

static void canvas_paint(window_t *w, widget_t *self, int abs_x, int abs_y) {
    (void)w;
    int cw = self->w, ch = self->h;
    fb_fill_rect((uint32_t)abs_x, (uint32_t)abs_y, (uint32_t)cw, (uint32_t)ch,
                 0x00f6f7fau);

    if (block_n == 0) {
        fb_draw_string_ui((uint32_t)(abs_x + 12), (uint32_t)(abs_y + 12),
                          "Click a palette block on the left to begin.",
                          0x00606878u);
        return;
    }
    int depth = 0;
    for (int i = 0; i < block_n; i++) {
        int y = abs_y + 8 + i * ROW_H;
        if (y + ROW_H > abs_y + ch) break;
        int t = blocks[i].type;
        int local_depth = depth;
        if (t == BLOCK_END && depth > 0) local_depth = depth - 1;
        int indent = local_depth * 18;
        int row_x = abs_x + 8 + indent;
        int row_w = cw - 16 - indent;
        int sel = (i == selected);
        uint32_t bg = sel ? 0x00cce5ffu : 0x00ffffffu;
        fb_fill_rect((uint32_t)row_x, (uint32_t)y, (uint32_t)row_w, ROW_H, bg);
        fb_fill_rect((uint32_t)row_x, (uint32_t)y, 6, ROW_H, block_color(t));
        fb_draw_string_ui((uint32_t)(row_x + 14), (uint32_t)(y + 4),
                          block_label(t), 0x00141a26u);
        fb_draw_string_ui((uint32_t)(row_x + 80), (uint32_t)(y + 4),
                          blocks[i].param, 0x00141a26u);
        if (t == BLOCK_REPEAT || t == BLOCK_IF) depth++;
        else if (t == BLOCK_END && depth > 0)  depth--;
    }
}

static void canvas_click(window_t *w, widget_t *self,
                         int local_x, int local_y, int btn) {
    (void)w; (void)btn;
    if (local_y < 8) return;
    int idx = (local_y - 8) / ROW_H;
    if (idx < 0 || idx >= block_n) return;
    selected = idx;
    /* Pre-fill the param input with the selected block's param. */
    if (param_input) {
        int j = 0;
        while (blocks[idx].param[j] && j + 1 < WIDGET_INPUT_MAX) {
            param_input->input[j] = blocks[idx].param[j]; j++;
        }
        param_input->input[j] = 0;
        param_input->input_len = j;
    }
    (void)self; (void)local_x;
}

static void out_canvas_paint(window_t *w, widget_t *self,
                             int abs_x, int abs_y) {
    (void)w;
    int cw = self->w, ch = self->h;
    fb_fill_rect((uint32_t)abs_x, (uint32_t)abs_y, (uint32_t)cw, (uint32_t)ch,
                 0x000d1220u);
    fb_draw_string_ui((uint32_t)(abs_x + 8), (uint32_t)(abs_y + 6),
                      "Output:", 0x00b0b8c8u);
    int lh = (int)fb_text_ui_line_height();
    int y = abs_y + 6 + lh + 6;
    for (int r = 0; r < OUTPUT_LINES; r++) {
        if (y + lh > abs_y + ch) break;
        if (output[r][0]) {
            fb_draw_string_ui((uint32_t)(abs_x + 8), (uint32_t)y,
                              output[r], 0x00d6dde9u);
        }
        y += lh + 2;
    }
}

/* ---------- public ---------- */

window_t *maker_window(void) {
    if (win) return win;
    win = window_create_widget("Maker", 100, 60, 1180, 720);
    if (!win) return 0;

    /* Top bar: name input + run/save/upload/clear */
    name_input = window_add_text_input(win, 12, 6, 220,
                                       "untitled", cb_save);
    {
        int j = 0;
        while (program_name[j] && j + 1 < WIDGET_INPUT_MAX) {
            name_input->input[j] = program_name[j]; j++;
        }
        name_input->input[j] = 0;
        name_input->input_len = j;
    }
    window_add_button(win, 240,  4,  80, "Run",      cb_run);
    window_add_button(win, 326,  4,  80, "Save",     cb_save);
    window_add_button(win, 412,  4, 100, "Upload",   cb_upload);
    window_add_button(win, 518,  4,  80, "Clear",    cb_clear);

    /* Palette on the left */
    static const char *pal_labels[] = {
        "+ Print", "+ Set", "+ If", "+ Repeat", "+ End",
    };
    static const int pal_types[] = {
        BLOCK_PRINT, BLOCK_SET, BLOCK_IF, BLOCK_REPEAT, BLOCK_END,
    };
    for (int i = 0; i < 5; i++) {
        widget_t *b = window_add_button(win, 12, 40 + i * 36, 130,
                                        pal_labels[i], cb_palette);
        b->canvas_ctx = (void *)(uintptr_t)pal_types[i];
    }

    /* Selected-block edit row at the bottom */
    window_add_label(win, 12, 226, 130, "Edit param:");
    param_input = window_add_text_input(win, 12, 248, 130,
                                        "(select a block)", cb_param_submit);
    window_add_button(win, 12, 282, 60, "Apply", cb_param_submit);
    window_add_button(win, 76, 282, 60, "Del",   cb_delete);
    window_add_button(win, 12, 318, 60, "Up",    cb_up);
    window_add_button(win, 76, 318, 60, "Down",  cb_down);

    /* Program canvas */
    canvas = window_add_canvas(win, 154, 38, 720, 460,
                               0, canvas_paint, canvas_click);
    /* Output canvas */
    out_canvas = window_add_canvas(win, 154, 506, 720, 192,
                                   0, out_canvas_paint, 0);

    /* Status line */
    status_lbl = window_add_label(win, 884, 40, 280, "ready");
    window_add_label(win, 884, 64, 280, "Marketplace: 10.0.2.2:8090");

    out_clear();
    return win;
}
