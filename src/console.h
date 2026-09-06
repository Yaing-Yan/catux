#pragma once
#include <stdint.h>

/* 阶段 1 的输出子系统 API —— 实现在 console.c
 * 所有输出同时送上两路：VGA 屏幕（给人看）+ 串口（给日志看） */

void console_init(void);               /* 清屏 + 初始化串口 */

/* 迷你 printf。支持的占位符：%c %s %d %x %%
 * 例：kprintf("magic = %x, line %d\n", 0x2BADB002, 42); */
void kprintf(const char *fmt, ...);

/* 设置后续打印的颜色。
 * 取值：0黑 1蓝 2绿 3青 4红 5品红 6棕 7浅灰，8~15 是对应的亮色 */
void vga_set_color(uint8_t fg, uint8_t bg);

/* 输出单个字符（屏幕+串口镜像） */
void kputc(char ch);

/* 清屏并复位光标 */
void console_clear(void);

/* 退格：光标左移一格并擦掉那个字符（屏幕和串口同步擦） */
void console_backspace(void);
