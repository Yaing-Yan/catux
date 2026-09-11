/*
 * console.c —— CatUX 的输出子系统（阶段 1）
 *
 * 架构：所有输出都经过 emit()，一个字符同时送上两路、永远镜像：
 *     VGA 文本屏幕 0xB8000  ←──┐
 *                              emit()
 *     串口 COM1（日志/调试） ←──┘
 *
 * ⚠️ 下方"实现要点"注释记录了三处关键实现的设计思路
 *    （硬件光标 / 滚动 / 递归打印数字），维护时先读它们。
 */

#include <stdarg.h>
#include <stdint.h>

#include "console.h"
#include "io.h"
#include "string.h"

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define COM1 0x3F8

static int cur_row;               /* 软件光标位置（我们自己记账） */
static int cur_col;
static uint8_t cur_color = 0x0F;  /* 高4位背景/低4位前景：0x0F = 黑底白字 */

/* ==================== 串口（阶段 0 讲过，从 main.c 搬了过来）==================== */
static void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* 关串口中断 */
    outb(COM1 + 3, 0x80);   /* DLAB=1，允许设波特率 */
    outb(COM1 + 0, 0x01);   /* 115200 波特 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO */
    outb(COM1 + 4, 0x0B);   /* RTS/DSR */
}

static void serial_putc(char ch)
{
    while (!(inb(COM1 + 5) & 0x20))   /* 等发送寄存器空闲 */
        ;
    outb(COM1, (uint8_t)ch);
}

/* ==================== 实现要点 1：驯服硬件光标 ====================
 * 现象：屏幕左边有个"幽灵下划线"，打印时它一动不动。
 * 原因：那根下划线是显卡里的"硬件光标"，位置存在显卡的寄存器里，
 *       BIOS 开机时设过一次，我们的内核还从来不管它。
 *
 * 操作 VGA 显卡寄存器的协议（"索引+数据"双端口，老硬件的经典设计）：
 *   outb(0x3D4, 寄存器编号);   ← 告诉显卡"我要访问几号寄存器"
 *   outb(0x3D5, 要写的值);     ← 读写该寄存器
 *   编号 0x0E = 光标线性位置的高 8 位
 *   编号 0x0F = 光标线性位置的低 8 位
 * 线性位置 = cur_row * 80 + cur_col（左上角是 0，随字符递增）
 * 位运算提示：pos >> 8 取高 8 位；pos & 0xFF 取低 8 位。
 *
 * 目标：写完后，那根下划线应该永远跟着最新输出走。 */
static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_row * VGA_COLS + cur_col);
    (void)pos;
    /* 实现要点 1：写 4 行 outb，把 pos 的高 8 位送进 0x0E 号寄存器、
     * 低 8 位送进 0x0F 号寄存器 */
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

/* ==================== 实现要点 2：滚动 ====================
 * 现象：写满 25 行后，新内容会全部叠在第 24 行（最后一行）上互相覆盖。
 * 正确行为：整个屏幕内容上移一行（最顶行被顶掉），
 *           最后一行清成空格，新内容继续在最后一行打印。
 *
 * 思路（两层 for 循环，操作 VGA_MEM 这个"数组"）：
 *   1. 对 r 从 1 到 VGA_ROWS-1，把第 r 行的每个字符
 *      复制到第 r-1 行同列的位置：
 *          VGA_MEM[(r-1)*VGA_COLS + c] = VGA_MEM[r*VGA_COLS + c];
 *   2. 把最后一行（行号 VGA_ROWS-1）整行填成
 *      (cur_color << 8) | ' '
 * cur_row 的调整 emit() 已经处理好了，不用你管。 */
static void vga_scroll(void)
{
    /* 实现：见上方说明 */
    for (int r = 1;r <= VGA_ROWS-1;r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            VGA_MEM[(r-1)*VGA_COLS + c] = VGA_MEM[r*VGA_COLS + c];
        }
    }
    for (int c = 0; c < VGA_COLS; c++)
        VGA_MEM[(VGA_ROWS-1)*VGA_COLS + c] = (cur_color << 8) | ' ';
}


/* ==================== 实现要点 3：打印无符号数 ====================
 * kprintf 的 %d 和 %x 全靠它：把 v 按 base 进制逐位打印出来。
 * 它没实现之前，屏幕上的数字都是空的。
 *
 * 提示（递归）：
 *   static const char digits[] = "0123456789abcdef";
 *   v % base            → 最低位的数值，digits[它] 就是该打印的字符
 *   v / base            → 去掉最低位后剩下的数（更高那些位）
 *   如果 v / base != 0，先递归 print_uint(v / base, base) 打印高位，
 *   然后打印 digits[v % base]。
 *   例：v=42, base=10 → 先递归打印 4，再打印 digits[2] → 屏幕出现 "42"
 *       v=255, base=16 → "ff" */
static void emit(char ch);
static void print_uint(uint32_t v, uint32_t base)
{
    /* 实现：见上方说明 */
    static const char digits[] = "0123456789abcdef";
    if (v/base != 0) {
        print_uint(v/base, base);
    }
    emit(digits[v % base]);
}

/* ==================== 以下是我写好的部分 ==================== */

void vga_set_color(uint8_t fg, uint8_t bg)
{
    cur_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

/* 核心：一个字符，两路输出 */
static void emit(char ch)
{
    serial_putc(ch);                    /* 串口：直接发走 */

    if (ch == '\n') {                   /* 屏幕：写显存 + 维护光标 */
        cur_col = 0;
        cur_row++;
    } else {
        VGA_MEM[cur_row * VGA_COLS + cur_col] =
            (uint16_t)((cur_color << 8) | (uint8_t)ch);
        if (++cur_col >= VGA_COLS) {
            cur_col = 0;
            cur_row++;
        }
    }
    if (cur_row >= VGA_ROWS) {          /* 写出屏幕底部 → 该滚动了 */
        vga_scroll();
        cur_row = VGA_ROWS - 1;
    }
    vga_update_cursor();                /* 硬件光标跟着走 */
}

void console_init(void)
{
    serial_init();
    console_clear();
}

void console_clear(void)
{
    for (int r = 0; r < VGA_ROWS; r++)          /* 整屏刷成空格 */
        for (int c = 0; c < VGA_COLS; c++)
            VGA_MEM[r * VGA_COLS + c] = (uint16_t)((cur_color << 8) | ' ');
    cur_row = 0;
    cur_col = 0;
    vga_update_cursor();
}

void kputc(char ch)
{
    emit(ch);
}

void console_backspace(void)
{
    if (cur_col == 0)
        return;                       /* 行首退格：先不支持（要滚动配合） */
    cur_col--;
    VGA_MEM[cur_row * VGA_COLS + cur_col] =
        (uint16_t)((cur_color << 8) | ' ');
    vga_update_cursor();
    serial_putc('\b');                /* 终端上也要擦：退格、空格覆盖、再退格 */
    serial_putc(' ');
    serial_putc('\b');
}

/* kprintf —— 迷你 printf。
 * 原理：扫描格式串；普通字符直接输出；遇到 % 就看下一个字符决定
 * 从参数列表（va_arg）里取什么类型的参数、按什么格式打。
 * 额外支持 %s 的宽度语法：%-10s（左对齐、占 10 列）、%8s（右对齐）。 */
void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);                  /* ap 指向第一个可变参数 */

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {                /* 普通字符：原样输出 */
            emit(*p);
            continue;
        }
        p++;                            /* 跳过 '%' */
        int left = 0, width = 0;        /* 解析标志和宽度（仅 %s 使用） */
        if (*p == '-') {
            left = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        switch (*p) {                   /* 跳过 '%'，看下一个字符 */
        case 'c':                       /* %c —— 注意：可变参数里 char 被提升成 int */
            emit((char)va_arg(ap, int));
            break;
        case 's': {                     /* %s —— 支持宽度对齐 */
            const char *s = va_arg(ap, const char *);
            int n = (int)kstrlen(s);
            if (!left)                  /* 右对齐：先补空格再打字 */
                for (int i = n; i < width; i++)
                    emit(' ');
            for (; *s; s++)
                emit(*s);
            if (left)                   /* 左对齐：先打字再补空格 */
                for (int i = n; i < width; i++)
                    emit(' ');
            break;
        }
        case 'd': {                     /* %d —— 有符号，先处理负号 */
            int n = va_arg(ap, int);
            if (n < 0) {
                emit('-');
                print_uint((uint32_t)-n, 10);
            } else {
                print_uint((uint32_t)n, 10);
            }
            break;
        }
        case 'x':                       /* %x —— 十六进制 */
            print_uint(va_arg(ap, uint32_t), 16);
            break;
        case '%':                       /* %% 打印一个 % 本身 */
            emit('%');
            break;
        default:                        /* 不认识的占位符：原样吐回去，方便发现笔误 */
            emit('%');
            emit(*p);
            break;
        }
    }
    va_end(ap);
}
