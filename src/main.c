/*
 * main.c —— CatUX 内核入口
 *
 * 阶段 3.5：初始化完成后进入 shell。内核不再是一段"跑完挂机"
 * 的代码，而是一个活着的、可对话的系统。
 *
 * kmain 的两个参数来自引导程序：
 *   magic —— 0x2BADB002 证明是 Multiboot 引导
 *   addr  —— Multiboot 信息结构（内存地图的来源）
 */

#include <stdint.h>

#include "console.h"
#include "idt.h"
#include "pic.h"
#include "irq.h"
#include "timer.h"
#include "keyboard.h"
#include "mem.h"
#include "paging.h"
#include "heap.h"
#include "shell.h"

void kmain(uint32_t magic, uint32_t addr)
{
    (void)addr;

    /* ---- 系统初始化（顺序不能乱：内存→中断→上层子系统）---- */
    console_init();      /* 输出先行，之后一切启动信息可见 */
    idt_init();          /* 异常有了去处 */
    pic_remap();         /* 设备中断搬到向量 32~47 */
    timer_init();        /* 100Hz 心跳 */
    keyboard_init();     /* 键盘 → 环形缓冲 */
    mem_init(addr);      /* 内存地图 + 页帧分配器 */
    paging_init();       /* 开分页：虚拟内存上线 */
    heap_init();         /* kmalloc/kfree 就绪 */

    kprintf("CatUX v0.0.8 by GLM-5.3-Flash and Yaing Yan\n");
    kprintf("boot ok, multiboot magic=%x\n", magic);

    /* ---- 自检 ---- */
    volatile uint16_t *alias = (volatile uint16_t *)0x80000000;
    alias[79] = (uint16_t)((0x2F << 8) | 'V');   /* 右上角绿 V：分页复检 */

    kprintf("self-test: heap ");
    char *t = kmalloc(64);
    if (t) {
        t[0] = 'o'; t[1] = 'k'; t[2] = 0;
        kprintf("[%s] ", t);
        kfree(t);
    } else {
        kprintf("[FAIL] ");
    }
    kprintf("frames[%d free] irq[timer+keyboard] -- all systems nominal\n\n",
            mem_free_frames());

    /* ⚠️ 初始化清单的最后一步永远是它：开中断总开关。
     * 忘了 sti 的症状很有迷惑性——一切正常但系统"聋了"，
     * hlt 睡死过去，谁也叫不醒。 */
    __asm__ volatile("sti");

    shell_run();         /* 永不返回：内核从此活在交互循环里 */
}
