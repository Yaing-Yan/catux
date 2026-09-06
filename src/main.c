/*
 * main.c —— CatUX 内核入口
 *
 * 阶段 2b：中断全面上线。时钟给内核"心跳"，键盘给内核"耳朵"，
 * 从这一版起，内核不是跑完一段代码就挂机，而是活着等事件发生。
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

/* ==================== 编译器的"私货" ====================
 * 即使你没写过这些函数，编译器开优化后也可能自己生成对它们的调用
 * （比如把大循环优化成 memset）。freestanding 环境必须自己提供。 */
void *memset(void *dst, int val, uint32_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)val;
    return dst;
}

void *memcpy(void *dst, const void *src, uint32_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void kmain(uint32_t magic, uint32_t addr)
{
    (void)addr;

    console_init();
    idt_init();          /* 异常有了去处（2a） */
    pic_remap();         /* 设备中断搬家到向量 32~47，不和异常撞车 */
    timer_init();        /* 时钟：IRQ0，100Hz 心跳 */
    keyboard_init();     /* 键盘：IRQ1 */
    mem_init(addr);      /* 摸清内存家底，启动页帧分配器（3a） */

    /* ---- 3b：开启分页 ---- */
    paging_init();       /* 恒等映射 16MB + VGA 魔法窗口，翻开 cr0.PG */

    kprintf("\nCatUX v0.0.7 by GLM-5.3-Flash and Yaing Yan\n");
    kprintf("paging ON (cr3=dir), multiboot magic=%x\n\n", magic);

    /* 魔法窗口演示：往虚拟 0x80000000 写一个字，
     * 它物理上落在 VGA 显存 0xB8000 → 屏幕右上角出现绿底白 'V'！
     * 两个不同的虚拟地址，同一块物理内存。 */
    volatile uint16_t *alias = (volatile uint16_t *)0x80000000;
    alias[79] = (uint16_t)((0x2F << 8) | 'V');
    kprintf("[paging] wrote 'V' through virtual 0x80000000 ->\n");
    kprintf("[paging] if top-right corner shows a green V, VA->PA works\n\n");

    /* ---- 3c：内核堆 ----
     * kmalloc 的地盘在虚拟 0xC0000000，页帧是现场铺进去的。
     * 注意两个地址都以 0xC000 开头却相差 0x1000+：小块的
     * "数据区"都在 16 字节块头的后面。 */
    heap_init();
    char *small = kmalloc(32);
    char *big = kmalloc(4096);
    kprintf("[heap] kmalloc(32)=0x%x  kmalloc(4096)=0x%x\n",
            (uint32_t)small, (uint32_t)big);
    small[0] = 'h'; small[1] = 'i'; small[2] = 0;
    kprintf("[heap] wrote \"%s\" into heap memory\n", small);
    kfree(big);
    kfree(small);
    kprintf("[heap] freed both. used=%d bytes, free=%d bytes\n\n",
            kheap_used(), kheap_free());

    /* 想亲眼看页故障（红字 EXCEPTION 14）？取消下面三行注释：
     * { volatile uint32_t *p = (uint32_t *)0xE0000000;  *p = 42; }
     * kprintf("unreachable\n");
     */

    /* ---- 页帧分配器验收（TODO 7 写完后这里才有数字）----
     * 领两帧、还一帧、再领一帧：最后一行应该和第一行相同
     * （first-fit 分配器会把刚归还的帧再发出去） */
    uint32_t a = alloc_frame();
    uint32_t b = alloc_frame();
    kprintf("[mem] alloc A=0x%x, B=0x%x\n", a, b);
    free_frame(a);
    kprintf("[mem] freed A, alloc C=0x%x (should equal A)\n", alloc_frame());
    kprintf("[mem] free frames left: %d\n", mem_free_frames());

    /* 上一版的 1/0 自爆测试撤了——想再看红色警报，
     * 随时把 `volatile int z = 0; kprintf("%d", 1/z);` 加回来 */

    __asm__ volatile("sti");     /* 开闸！这一行之后，中断源源不断进来，
                                  * 内核正式"活"了 */
    kprintf("type something, I can hear you now:\n\n");

    for (;;)                     /* hlt = 睡觉。中断来了硬件自动唤醒 CPU
                                  * 去跑处理函数，处理完 iret 回到这里继续睡。
                                  * 省电又优雅——真内核的空闲循环长这样 */
        __asm__ volatile("hlt");
}
