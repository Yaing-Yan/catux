/*
 * pic.c —— 8259 可编程中断控制器（阶段 2b）
 *
 * CPU 只有一根"中断线"。15 个设备（时钟、键盘、磁盘……）都先汇到
 * PIC 这里，由它排队、转发给 CPU。主片管 IRQ0~7，从片管 IRQ8~15，
 * 从片自己挂在主片的第 2 号针上（所以 IRQ2 = 级联用）。
 *
 * 问题：PIC 上电默认把 IRQ0~7 映射到向量号 8~15——那里是 CPU
 * 异常的地盘（8=双故障，13=General Protection...），撞车了！
 * 所以初始化第一件事：重映射到 32~47（32~255 是留给 OS 的自由区）。
 *
 * 编程方式：往两个端口写一串 ICW（初始化命令字），顺序不能乱。
 */

#include <stdint.h>

#include "io.h"
#include "pic.h"

#define PIC1_CMD  0x20          /* 主片命令端口 */
#define PIC1_DATA 0x21          /* 主片数据端口 */
#define PIC2_CMD  0xA0          /* 从片命令端口 */
#define PIC2_DATA 0xA1          /* 从片数据端口 */

void pic_remap(void)
{
    outb(PIC1_CMD, 0x11);       /* ICW1：开始初始化，级联模式 */
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);      /* ICW2：主片 IRQ0~7 → 向量 32~39 */
    outb(PIC2_DATA, 0x28);      /* ICW2：从片 IRQ8~15 → 向量 40~47 */
    io_wait();
    outb(PIC1_DATA, 0x04);      /* ICW3：主片第 2 针接着从片 */
    outb(PIC2_DATA, 0x02);      /* ICW3：从片挂在主片第 2 针 */
    io_wait();
    outb(PIC1_DATA, 0x01);      /* ICW4：8086 模式 */
    outb(PIC2_DATA, 0x01);
    io_wait();
    outb(PIC1_DATA, 0x00);      /* OCW1：屏蔽位全开（每位 0 = 不屏蔽） */
    outb(PIC2_DATA, 0x00);
}

/* EOI（End Of Interrupt）：处理完毕的"回执"。
 * 不发的话 PIC 以为你还在处理，之后同一优先级以下的中断全部不送。 */
void pic_send_eoi(int irq)
{
    if (irq >= 8)               /* 来自从片的中断：从片也要应答 */
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);       /* 0x20 = EOI 命令 */
}
