#pragma once
#include <stdint.h>

/* PIT 可编程间隔定时器（IRQ0）——内核的心跳。
 * 配置为 100Hz：每秒滴答 100 次。 */
void timer_init(void);

/* 开机至今的秒数 */
uint32_t timer_uptime(void);
