#include <hobby_sdk.h>

/* 10x10 minesweeper. Left click reveals, right click flags.
 * Reaches the framebuffer through SYS_GUI_* and reads mouse via
 * SYS_GUI_POLL. */

#define ROWS 10
#define COLS 10
#define MINES 15
#define CELL  56
#define BOARD_W (COLS * CELL)
#define BOARD_H (ROWS * CELL)

#define BG_COLOR  0x00103a4eu
#define GRID_BG   0x002c4054u
#define HIDDEN_BG 0x004a6884u
#define HIDDEN_HI 0x00608bb4u
#define REVEAL_BG 0x001b2230u
#define FLAG_BG   0x00cc5c5cu
#define MINE_BG   0x00ff5050u
#define TEXT_FG   0x00ffffffu
#define WIN_FG    0x0050cc60u

static int  board[ROWS][COLS];   /* number of mines around */
static int  is_mine[ROWS][COLS];
static int  revealed[ROWS][COLS];
static int  flagged[ROWS][COLS];
static int  game_over;
static int  game_won;
static unsigned rng = 0x12345abcu;

static unsigned rand32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void place_mines(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            board[r][c] = 0;
            is_mine[r][c] = 0;
            revealed[r][c] = 0;
            flagged[r][c]  = 0;
        }
    int placed = 0;
    while (placed < MINES) {
        int r = (int)(rand32() % ROWS);
        int c = (int)(rand32() % COLS);
        if (is_mine[r][c]) continue;
        is_mine[r][c] = 1;
        placed++;
    }
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (is_mine[r][c]) continue;
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS) continue;
                    if (is_mine[rr][cc]) n++;
                }
            board[r][c] = n;
        }
    }
    game_over = 0;
    game_won = 0;
}

static void flood(int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    if (revealed[r][c] || flagged[r][c]) return;
    revealed[r][c] = 1;
    if (board[r][c] != 0 || is_mine[r][c]) return;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if (dr || dc) flood(r + dr, c + dc);
}

static int check_win(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (!is_mine[r][c] && !revealed[r][c]) return 0;
    return 1;
}

static int board_x, board_y;

static void paint_cell(int r, int c) {
    int x = board_x + c * CELL;
    int y = board_y + r * CELL;
    if (revealed[r][c]) {
        if (is_mine[r][c]) {
            hobby_gui_fill_rect(x, y, CELL, CELL, MINE_BG);
            hobby_gui_draw_text(x + CELL/2 - 8, y + CELL/2 - 8, "*", TEXT_FG, 2);
        } else {
            hobby_gui_fill_rect(x, y, CELL, CELL, REVEAL_BG);
            if (board[r][c] > 0) {
                char num[2] = { (char)('0' + board[r][c]), 0 };
                hobby_gui_draw_text(x + CELL/2 - 8, y + CELL/2 - 16,
                                    num, TEXT_FG, 2);
            }
        }
    } else if (flagged[r][c]) {
        hobby_gui_fill_rect(x, y, CELL, CELL, FLAG_BG);
        hobby_gui_draw_text(x + CELL/2 - 8, y + CELL/2 - 16, "F", TEXT_FG, 2);
    } else {
        hobby_gui_fill_rect(x, y, CELL, CELL, HIDDEN_BG);
        hobby_gui_fill_rect(x, y, CELL, 2, HIDDEN_HI);
        hobby_gui_fill_rect(x, y, 2, CELL, HIDDEN_HI);
    }
    /* grid line */
    hobby_gui_fill_rect(x + CELL - 1, y, 1, CELL, GRID_BG);
    hobby_gui_fill_rect(x, y + CELL - 1, CELL, 1, GRID_BG);
}

static void paint_board(int fb_w) {
    hobby_gui_fill_rect(0, 0, fb_w, 80, BG_COLOR);
    hobby_gui_draw_text(20, 20, "Minesweeper", TEXT_FG, 3);

    if (game_over) {
        hobby_gui_draw_text(fb_w - 320, 24, "BOOM! click to retry",
                            MINE_BG, 2);
    } else if (game_won) {
        hobby_gui_draw_text(fb_w - 280, 24, "You win! click to restart",
                            WIN_FG, 2);
    } else {
        hobby_gui_draw_text(fb_w - 320, 24,
                            "left=reveal  right=flag",
                            TEXT_FG, 2);
    }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            paint_cell(r, c);
}

int hobby_main(void) {
    place_mines();

    hobby_gui_take();
    struct hobby_fb_info info;
    hobby_gui_fb_info(&info);
    board_x = ((int)info.width - BOARD_W) / 2;
    board_y = 100;

    /* clear once */
    hobby_gui_fill_rect(0, 0, info.width, info.height, BG_COLOR);
    paint_board((int)info.width);
    hobby_gui_present();

    int prev_buttons = 0;
    int prev_key     = 0;
    rng = (unsigned)hobby_getpid() * 2654435761u + 0x1337u;

    for (;;) {
        struct hobby_event ev;
        hobby_gui_poll(&ev);

        int press_left  = (ev.buttons & 1) && !(prev_buttons & 1);
        int press_right = (ev.buttons & 2) && !(prev_buttons & 2);
        prev_buttons = ev.buttons;

        if (ev.key == 'q' && prev_key == 0) {
            hobby_gui_release();
            hobby_exit(0);
        }
        prev_key = ev.key;

        if (press_left || press_right) {
            int cx = ev.mouse_x - board_x;
            int cy = ev.mouse_y - board_y;
            if (cx >= 0 && cy >= 0 && cx < BOARD_W && cy < BOARD_H) {
                int c = cx / CELL, r = cy / CELL;
                if (game_over || game_won) {
                    place_mines();
                } else if (press_left && !flagged[r][c]) {
                    if (is_mine[r][c]) {
                        revealed[r][c] = 1;
                        game_over = 1;
                        /* reveal everything */
                        for (int rr = 0; rr < ROWS; rr++)
                            for (int cc = 0; cc < COLS; cc++)
                                if (is_mine[rr][cc]) revealed[rr][cc] = 1;
                    } else {
                        flood(r, c);
                        if (check_win()) game_won = 1;
                    }
                } else if (press_right && !revealed[r][c]) {
                    flagged[r][c] = !flagged[r][c];
                }
            }
            paint_board((int)info.width);
            hobby_gui_present();
        }

        hobby_gui_sleep_ms(20);
    }
}
