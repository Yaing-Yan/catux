#pragma once

/* PS/2 键盘（IRQ1）
 * 中断把字符压进环形缓冲区，消费者用 kb_getchar 阻塞式读取。
 * 生产者（中断上下文）只有一个，消费者也只有一个，无锁即安全。 */

void keyboard_init(void);

/* 取下一个按键字符；没有就 hlt 睡眠，来了键自动醒来 */
char kb_getchar(void);
