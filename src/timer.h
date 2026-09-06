#pragma once

/* PIT 可编程间隔定时器（IRQ0）——内核的心跳。
 * 配置为 100Hz：每秒滴答 100 次，ticks/100 即运行秒数。 */
void timer_init(void);
