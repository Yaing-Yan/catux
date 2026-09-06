/*
 * timer.c —— PIT 可编程间隔定时器（阶段 2b）
 *
 * 芯片频率固定为 1193182 Hz，我们给它一个"除数"，
 * 它就按 1193182/除数 的频率往 IRQ0 上发信号。
 * 除数 11931 ≈ 100Hz —— 内核每秒心跳 100 次。
 *
 * 这就是"时钟中断"：不管内核在干什么，每 10ms 硬件强制
 * 打断它一次。以后的任务调度器（阶段 4）全靠这颗心脏。
 */

#include <stdint.h>

#include "io.h"
#include "console.h"
#include "irq.h"
#include "timer.h"

#define PIT_FREQ 1193182

static uint32_t ticks;          /* 总滴答数 */

static void timer_tick(void)
{
    ticks++;
    if (ticks % 100 == 0)       /* 每 100 滴 = 1 秒：报一次运行时间 */
        kprintf("[CatUX uptime: %d s]\n", ticks / 100);
}

void timer_init(void)
{
    uint32_t div = PIT_FREQ / 100;

    outb(0x43, 0x36);           /* 控制字：通道0 | 先低后高 | 方波 | 16位 */
    outb(0x40, (uint8_t)(div & 0xFF));   /* 除数低 8 位 → 通道0数据口 */
    outb(0x40, (uint8_t)(div >> 8));     /* 除数高 8 位 */

    irq_register(0, timer_tick);
}
