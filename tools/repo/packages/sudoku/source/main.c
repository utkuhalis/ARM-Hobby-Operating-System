#include <hobby_sdk.h>

/* Tiny sudoku: a single fixed puzzle. Click a cell, type 1-9 to set,
 * 0 / backspace to clear. Press 'q' to quit. Right click solves the
 * cell from the canonical solution (cheat). */

#define CELL  64
#define BOARD 9
#define BW    (CELL * BOARD)

#define BG_COLOR  0x000c1a26u
#define GRID_THIN 0x002c4054u
#define GRID_THICK 0x0064a0d0u
#define CELL_BG   0x00f4f6fau
#define CELL_SEL  0x00cce5ffu
#define GIVEN_FG  0x00141a26u
#define USER_FG   0x002c6cb8u
#define ERROR_BG  0x00ffd0d0u
#define WIN_FG    0x0050cc60u

/* puzzle: 0 = blank */
static const int puzzle[9][9] = {
    {5,3,0, 0,7,0, 0,0,0},
    {6,0,0, 1,9,5, 0,0,0},
    {0,9,8, 0,0,0, 0,6,0},

    {8,0,0, 0,6,0, 0,0,3},
    {4,0,0, 8,0,3, 0,0,1},
    {7,0,0, 0,2,0, 0,0,6},

    {0,6,0, 0,0,0, 2,8,0},
    {0,0,0, 4,1,9, 0,0,5},
    {0,0,0, 0,8,0, 0,7,9},
};
static const int solution[9][9] = {
    {5,3,4, 6,7,8, 9,1,2},
    {6,7,2, 1,9,5, 3,4,8},
    {1,9,8, 3,4,2, 5,6,7},

    {8,5,9, 7,6,1, 4,2,3},
    {4,2,6, 8,5,3, 7,9,1},
    {7,1,3, 9,2,4, 8,5,6},

    {9,6,1, 5,3,7, 2,8,4},
    {2,8,7, 4,1,9, 6,3,5},
    {3,4,5, 2,8,6, 1,7,9},
};

static int grid[9][9];
static int sel_r = 4, sel_c = 4;
static int board_x, board_y;

static void reset_grid(void) {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            grid[r][c] = puzzle[r][c];
}

static int is_solved(void) {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (grid[r][c] != solution[r][c]) return 0;
    return 1;
}

static void paint(int fb_w) {
    hobby_gui_fill_rect(0, 0, fb_w, 80, BG_COLOR);
    hobby_gui_draw_text(20, 20, "Sudoku", 0x00ffffffu, 3);
    hobby_gui_draw_text(fb_w - 360, 24,
                        "click cell, type 1-9, 0 to clear",
                        0x00b0c0d0u, 2);
    if (is_solved()) {
        hobby_gui_draw_text(fb_w - 200, 50, "Solved!", WIN_FG, 2);
    }

    /* Cells */
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int x = board_x + c * CELL;
            int y = board_y + r * CELL;
            int sel = (r == sel_r && c == sel_c);
            int err = grid[r][c] != 0 && grid[r][c] != solution[r][c];
            unsigned bg = err ? ERROR_BG : (sel ? CELL_SEL : CELL_BG);
            hobby_gui_fill_rect(x, y, CELL, CELL, bg);

            if (grid[r][c] != 0) {
                char num[2] = { (char)('0' + grid[r][c]), 0 };
                int given = puzzle[r][c] != 0;
                hobby_gui_draw_text(x + CELL/2 - 12,
                                    y + CELL/2 - 24,
                                    num,
                                    given ? GIVEN_FG : USER_FG, 3);
            }
        }
    }
    /* Grid lines */
    for (int i = 0; i <= 9; i++) {
        int x = board_x + i * CELL;
        int y = board_y + i * CELL;
        unsigned color = (i % 3 == 0) ? GRID_THICK : GRID_THIN;
        int thick = (i % 3 == 0) ? 3 : 1;
        hobby_gui_fill_rect(x, board_y, thick, BW, color);
        hobby_gui_fill_rect(board_x, y, BW, thick, color);
    }
}

int hobby_main(void) {
    reset_grid();
    hobby_gui_take();
    struct hobby_fb_info info;
    hobby_gui_fb_info(&info);
    board_x = ((int)info.width  - BW) / 2;
    board_y = 110;

    hobby_gui_fill_rect(0, 0, info.width, info.height, BG_COLOR);
    paint((int)info.width);
    hobby_gui_present();

    int prev_buttons = 0;
    int prev_key     = 0;

    for (;;) {
        struct hobby_event ev;
        hobby_gui_poll(&ev);

        int dirty = 0;
        int press_left  = (ev.buttons & 1) && !(prev_buttons & 1);
        int press_right = (ev.buttons & 2) && !(prev_buttons & 2);
        prev_buttons = ev.buttons;

        if (press_left || press_right) {
            int cx = ev.mouse_x - board_x;
            int cy = ev.mouse_y - board_y;
            if (cx >= 0 && cy >= 0 && cx < BW && cy < BW) {
                sel_c = cx / CELL;
                sel_r = cy / CELL;
                if (press_right && puzzle[sel_r][sel_c] == 0) {
                    grid[sel_r][sel_c] = solution[sel_r][sel_c];
                }
                dirty = 1;
            }
        }

        if (ev.key && ev.key != prev_key) {
            char k = (char)ev.key;
            if (k == 'q') {
                hobby_gui_release();
                hobby_exit(0);
            } else if (k == 'r') {
                reset_grid(); dirty = 1;
            } else if (k >= '1' && k <= '9' &&
                       puzzle[sel_r][sel_c] == 0) {
                grid[sel_r][sel_c] = k - '0';
                dirty = 1;
            } else if (k == '0' || k == '\b') {
                if (puzzle[sel_r][sel_c] == 0) {
                    grid[sel_r][sel_c] = 0;
                    dirty = 1;
                }
            }
        }
        prev_key = ev.key;

        if (dirty) {
            paint((int)info.width);
            hobby_gui_present();
        }

        hobby_gui_sleep_ms(20);
    }
}
