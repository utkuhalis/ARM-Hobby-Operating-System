#ifndef HOBBY_OS_SYSCALL_H
#define HOBBY_OS_SYSCALL_H

#include <stdint.h>

#define SYS_WRITE        0
#define SYS_EXIT         1
#define SYS_GETPID       2
#define SYS_YIELD        3
/* GUI syscalls: user-mode program takes the whole screen and paints
 * directly via these. Used by the Minesweeper / Sudoku packages. */
#define SYS_GUI_TAKE     4   /* hides desktop, gives the app the FB */
#define SYS_GUI_RELEASE  5   /* desktop chrome resumes */
#define SYS_GUI_FILL_RECT 6  /* x0=x,x1=y,x2=w,x3=h,x4=color */
#define SYS_GUI_DRAW_TEXT 7  /* x0=x,x1=y,x2=str_ptr,x3=color,x4=scale */
#define SYS_GUI_PRESENT  8   /* commit back buffer to front */
#define SYS_GUI_POLL     9   /* x0=struct gui_event * */
#define SYS_GUI_SLEEP_MS 10  /* x0=ms */
#define SYS_GUI_FB_INFO  11  /* x0=struct gui_info * */

struct gui_event {
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t buttons;   /* bit 0 = left, bit 1 = right */
    int32_t key;       /* 0 if none */
};

struct gui_info {
    uint32_t width;
    uint32_t height;
};

struct trapframe {
    uint64_t x[31];   /* x0..x30 */
    uint64_t sp_el0;
    uint64_t elr_el1;
    uint64_t spsr_el1;
};

void sync_handler(struct trapframe *tf);

#endif
