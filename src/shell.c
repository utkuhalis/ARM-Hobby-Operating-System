#include <stdint.h>
#include "shell.h"
#include "console.h"
#include "str.h"
#include "fs.h"
#include "sysinfo.h"
#include "psci.h"
#include "heap.h"

#ifdef BOARD_HAS_GIC
#include "timer.h"
#include "task.h"
#include "heap.h"
#include "user_program.h"
#endif

#define LINE_MAX  256
#define ARGV_MAX  16

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t _stack_top[];

typedef void (*cmd_fn)(int argc, char **argv);

struct cmd {
    const char *name;
    cmd_fn      fn;
    const char *help;
};

static void cmd_help(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_uname(int argc, char **argv);
static void cmd_cpuinfo(int argc, char **argv);
static void cmd_meminfo(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
#ifdef BOARD_HAS_GIC
static void cmd_ps(int argc, char **argv);
static void cmd_run(int argc, char **argv);
#endif

static const struct cmd cmds[] = {
    {"help",    cmd_help,    "list commands"},
    {"echo",    cmd_echo,    "print arguments"},
    {"clear",   cmd_clear,   "clear the screen"},
    {"ls",      cmd_ls,      "list files"},
    {"cat",     cmd_cat,     "print a file"},
    {"write",   cmd_write,   "write text into a file"},
    {"touch",   cmd_touch,   "create an empty file"},
    {"rm",      cmd_rm,      "delete a file"},
    {"uname",   cmd_uname,   "show OS info ('-a' for full)"},
    {"cpuinfo", cmd_cpuinfo, "CPU and core info"},
    {"meminfo", cmd_meminfo, "memory layout"},
    {"uptime",  cmd_uptime,  "show uptime"},
#ifdef BOARD_HAS_GIC
    {"ps",      cmd_ps,      "list kernel tasks"},
    {"run",     cmd_run,     "run a built-in user program (try 'run hello')"},
#endif
    {"halt",    cmd_halt,    "shut down the system"},
    {"reboot",  cmd_reboot,  "reboot the system"},
    {0, 0, 0},
};

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    console_puts("commands:\n");
    for (int i = 0; cmds[i].name; i++) {
        console_printf("  %-10s %s\n", cmds[i].name, cmds[i].help);
    }
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        console_puts(argv[i]);
        if (i + 1 < argc) console_putc(' ');
    }
    console_putc('\n');
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    console_puts("\x1b[2J\x1b[H");
}

static void cmd_uname(int argc, char **argv) {
    int show_all = (argc > 1 && strcmp(argv[1], "-a") == 0);
    if (show_all) {
        console_printf("Hobby ARM OS v0.2 aarch64 %s %s\n",
                       sys_board_name(),
                       sys_cpu_name(sys_read_midr()));
    } else {
        console_puts("Hobby ARM OS\n");
    }
}

static void cmd_cpuinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t midr  = sys_read_midr();
    uint64_t mpidr = sys_read_mpidr();
    uint32_t el    = sys_read_currentel();
    uint64_t freq  = sys_timer_freq();

    console_printf("model            : %s\n", sys_cpu_name(midr));
    console_printf("MIDR_EL1         : 0x%016lx\n", midr);
    console_printf("MPIDR_EL1        : 0x%016lx\n", mpidr);
    console_printf("aff0/aff1/aff2   : %lu/%lu/%lu\n",
                   (unsigned long)((mpidr >>  0) & 0xff),
                   (unsigned long)((mpidr >>  8) & 0xff),
                   (unsigned long)((mpidr >> 16) & 0xff));
    console_printf("current EL       : %u\n", el);
    console_printf("counter freq     : %lu Hz\n", freq);
    console_printf("counter value    : %lu\n", sys_timer_count());
}

static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t kstart = (uint64_t)(uintptr_t)_kernel_start;
    uint64_t kend   = (uint64_t)(uintptr_t)_kernel_end;
    uint64_t stop   = (uint64_t)(uintptr_t)_stack_top;

    console_printf("kernel image     : 0x%016lx .. 0x%016lx (%lu bytes)\n",
                   kstart, kend, kend - kstart);
    console_printf("kernel + stack   : 0x%016lx .. 0x%016lx (%lu bytes)\n",
                   kstart, stop, stop - kstart);
    console_printf("filesystem store : %d slots x %d bytes = %d bytes total\n",
                   FS_MAX_FILES, FS_MAX_DATA, FS_MAX_FILES * FS_MAX_DATA);
    console_printf("files in use     : %d / %d\n", fs_count(), FS_MAX_FILES);
    console_printf("kernel heap      : %lu bytes total, %lu bytes in use\n",
                   (unsigned long)heap_total(), (unsigned long)heap_used());
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t s = sys_uptime_seconds();
#ifdef BOARD_HAS_GIC
    console_printf("up %lu seconds  (%lu kernel ticks @ %u Hz)\n",
                   s, timer_ticks(), timer_hz());
#else
    console_printf("up %lu seconds\n", s);
#endif
}

static void cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_file_t *f = fs_at(i);
        if (!f) continue;
        console_printf("  %-32s %u bytes\n", f->name, f->size);
        n++;
    }
    if (n == 0) console_puts("  (empty)\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        console_puts("usage: cat <name>\n");
        return;
    }
    fs_file_t *f = fs_find(argv[1]);
    if (!f) {
        console_printf("no such file: %s\n", argv[1]);
        return;
    }
    for (uint32_t i = 0; i < f->size; i++) {
        console_putc((char)f->data[i]);
    }
    if (f->size == 0 || f->data[f->size - 1] != '\n') {
        console_putc('\n');
    }
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        console_puts("usage: write <name> <text...>\n");
        return;
    }

    char buf[FS_MAX_DATA];
    uint32_t pos = 0;
    for (int i = 2; i < argc; i++) {
        size_t l = strlen(argv[i]);
        if (pos + l + 1 >= FS_MAX_DATA) break;
        memcpy(&buf[pos], argv[i], l);
        pos += (uint32_t)l;
        if (i + 1 < argc) buf[pos++] = ' ';
    }
    if (pos < FS_MAX_DATA) buf[pos++] = '\n';

    if (fs_write(argv[1], buf, pos) == 0) {
        console_printf("wrote %u bytes to %s\n", pos, argv[1]);
    } else {
        console_puts("write failed (filesystem full or invalid name)\n");
    }
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        console_puts("usage: touch <name>\n");
        return;
    }
    if (fs_find(argv[1])) {
        return;
    }
    if (fs_write(argv[1], "", 0) != 0) {
        console_puts("touch failed (filesystem full or invalid name)\n");
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        console_puts("usage: rm <name>\n");
        return;
    }
    if (fs_delete(argv[1]) == 0) {
        console_printf("removed %s\n", argv[1]);
    } else {
        console_printf("no such file: %s\n", argv[1]);
    }
}

#ifdef BOARD_HAS_GIC
static const char *task_state_str(int s) {
    switch (s) {
    case 0: return "ready";
    case 1: return "running";
    case 2: return "dead";
    default: return "?";
    }
}

extern uint64_t kernel_ticker_beats(void);

static void cmd_run(int argc, char **argv) {
    if (argc < 2) {
        console_puts("usage: run <program>\n");
        console_puts("  programs: hello\n");
        return;
    }
    if (strcmp(argv[1], "hello") == 0) {
        int id = task_spawn("hello", (void (*)(void *))user_main_hello, NULL);
        if (id < 0) {
            console_puts("run: spawn failed\n");
            return;
        }
        console_printf("spawned task id %d (kernel mode, syscalls via SVC)\n", id);
        for (int i = 0; i < 6; i++) task_yield();
    } else {
        console_printf("unknown program: %s\n", argv[1]);
    }
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    task_t *t = task_first();
    console_printf("  %-4s %-16s %-10s\n", "id", "name", "state");
    while (t) {
        console_printf("  %-4d %-16s %-10s\n",
                       t->id, t->name, task_state_str(t->state));
        t = t->next;
    }
    console_printf("\nticker beats   : %lu\n", kernel_ticker_beats());
    console_printf("kernel ticks   : %lu\n", timer_ticks());
}
#endif

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    console_puts("system halted.\n");
    psci_system_off();
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    console_puts("rebooting...\n");
    psci_system_reset();
}

static int parse_argv(char *line, char **argv, int max) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') break;
        if (argc >= max - 1) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    argv[argc] = 0;
    return argc;
}

void shell_run(void) {
    static char line[LINE_MAX];
    static char *argv[ARGV_MAX];

    for (;;) {
        console_puts("hobby# ");
        int n = console_readline(line, sizeof(line));
        if (n < 0) continue;

        int argc = parse_argv(line, argv, ARGV_MAX);
        if (argc == 0) continue;

        const struct cmd *match = 0;
        for (int i = 0; cmds[i].name; i++) {
            if (strcmp(argv[0], cmds[i].name) == 0) {
                match = &cmds[i];
                break;
            }
        }

        if (!match) {
            console_printf("unknown command: %s (try 'help')\n", argv[0]);
            continue;
        }

        match->fn(argc, argv);
    }
}
