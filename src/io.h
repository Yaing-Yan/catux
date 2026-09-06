#pragma once
#include <stdint.h>

/* 最底层的硬件 I/O 原语 —— 汇编实现在 boot.S。
 * CPU 通过独立的"端口"地址空间和设备寄存器对话。 */
void    outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void    io_wait(void);   /* 约 1 微秒的 I/O 延迟（老硬件反应慢，写完要等一拍） */
