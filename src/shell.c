/*
 * shell.c —— 内核态迷你 shell（阶段 3.5）
 *
 * 交互循环：打印提示符 → 读一行（支持退格）→ 按空格拆词 →
 * 查命令表执行 → 循环。所有函数都跑在 ring0，直接调内核 API；
 * 阶段 7 的用户态 shell 会跑在 ring3，一切输入输出都得走系统
 * 调用——但解析逻辑和命令表设计可以原样继承。
 */

#include <stdint.h>

#include "console.h"
#include "keyboard.h"
#include "timer.h"
#include "mem.h"
#include "heap.h"
#include "fs.h"
#include "syscall.h"
#include "io.h"
#include "string.h"
#include "shell.h"

#define LINE_MAX 96
#define ARGV_MAX 8

static char line[LINE_MAX];

/* ==================== 命令实现 ====================
 * 命令表 cmds 定义在本文件下方，cmd_help 通过前向声明的
 * list_cmds() 访问它——C 要求"先声明后使用"，这个套路很常用 */

struct cmd {
    const char *name;
    const char *desc;
    void (*fn)(int argc, char **argv);
};

static void list_cmds(void);   /* 前向声明：实现在命令表后面 */

static void cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("CatUX built-in commands:\n");
    list_cmds();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kprintf("\n");
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("uptime: %d seconds\n", timer_uptime());
}

static void cmd_meminfo(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("physical frames: %d free / %d total (4KB each)\n",
            mem_free_frames(), mem_total_frames());
    kprintf("kernel heap    : %d used / %d free bytes\n",
            kheap_used(), kheap_free());
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_clear();
}

static void cmd_ring3(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("[shell] launching user program in ring3...\n");
    user_run();
}

static void cmd_about(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf(" /\\_/\\ \n");
    kprintf("( o.o )  CatUX v0.0.8\n");
    kprintf(" > ^ <   by GLM-5.3-Flash and Yaing Yan\n");
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("rebooting...\n");
    /* 8042 键盘控制器的老把戏：往状态口写 0xFE 触发 CPU 复位线 */
    outb(0x64, 0xFE);
    __asm__ volatile("hlt");   /* 万一没重启成功：挂住别乱跑 */
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc; (void)argv;
    fs_ls();
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("usage: cat <file>\n");
        return;
    }
    char *buf = kmalloc(4096);        /* 注意：shell 也在用堆了 */
    if (!buf) {
        kprintf("cat: out of memory\n");
        return;
    }
    int n = fs_read(argv[1], buf, 4096);
    if (n < 0)
        kprintf("cat: %s: no such file\n", argv[1]);
    else
        kprintf("%s\n", buf);
    kfree(buf);
}

static const struct cmd cmds[] = {
    { "help",    "list commands",               cmd_help    },
    { "echo",    "print arguments",             cmd_echo    },
    { "uptime",  "seconds since boot",          cmd_uptime  },
    { "meminfo", "frames & heap usage",         cmd_meminfo },
    { "ls",      "list files on initrd",        cmd_ls      },
    { "cat",     "print a file: cat <file>",    cmd_cat     },
    { "ring3",   "run hello.bin in user mode",  cmd_ring3   },
    { "clear",   "clear the screen",            cmd_clear   },
    { "about",   "who am i",                    cmd_about   },
    { "reboot",  "reboot the machine",          cmd_reboot  },
};

static void list_cmds(void)
{
    for (uint32_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        kprintf("  %-10s %s\n", cmds[i].name, cmds[i].desc);
}

/* ==================== 解析与执行 ==================== */

/* 就地把一行按空格拆词：line 被改写（空格换成 '\0'），
 * argv[] 指向各词开头，返回词数。argv[0] 是命令名。 */
static int split(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ')          /* 跳过前导空格 */
            p++;
        if (!*p)
            break;
        argv[argc++] = p;          /* 词的开头 */
        while (*p && *p != ' ')    /* 走到词尾 */
            p++;
        if (*p)
            *p++ = 0;              /* 补刀 '\0'，这个词到此为止 */
    }
    return argc;
}

static void execute(int argc, char **argv)
{
    for (uint32_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        if (kstrcmp(argv[0], cmds[i].name) == 0) {
            cmds[i].fn(argc, argv);
            return;
        }
    }
    kprintf("catux: unknown command: %s (try `help`)\n", argv[0]);
}

/* ==================== 主循环 ==================== */

void shell_run(void)
{
    for (;;) {
        vga_set_color(0x0B, 0x00);          /* 青色提示符 */
        kprintf("catux> ");
        vga_set_color(0x0F, 0x00);

        uint32_t len = 0;
        for (;;) {
            char c = kb_getchar();
            if (c == '\n') {                /* 回车：结算这一行 */
                kprintf("\n");
                break;
            }
            if (c == '\b') {                /* 退格：删一个字符 */
                if (len) {
                    len--;
                    console_backspace();
                }
                continue;
            }
            if (c >= 0x20 && c <= 0x7E && len < LINE_MAX - 1) {
                line[len++] = c;
                kputc(c);                   /* 回显 */
            }
            /* 其它（Tab、方向键……）先忽略 */
        }
        line[len] = 0;

        char *argv[ARGV_MAX];
        int argc = split(line, argv, ARGV_MAX);
        if (argc)                           /* 空行：直接再来一遍 */
            execute(argc, argv);
    }
}
