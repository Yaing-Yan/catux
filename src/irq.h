#pragma once
#include <stdint.h>

/* 硬件中断（IRQ）处理框架
 * IRQ 编号：0=时钟 1=键盘 2=级联 14=主硬盘 ...
 * 重映射后：向量号 = IRQ + 32 */

/* 中断现场：与 isr.S 的栈布局严格对应（pusha 8 个 + 桩压 2 个 + CPU 压 3 个）。
 * 调度器靠它保存/恢复任务的完整 CPU 状态。 */
struct regs {
    uint32_t edi, esi, ebp, o_esp;   /* pusha 压入（o_esp 是 pusha 的占位副本） */
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err;            /* 汇编桩压入 */
    uint32_t eip, cs, eflags;        /* CPU 自动压入 */
};

typedef void (*irq_handler_t)(void);

/* 注册处理函数：对应 IRQ 到来时被 irq_dispatch 调用 */
void irq_register(int irq, irq_handler_t handler);

/* 汇编桩（isr.S 的 irq_common）调用它：(向量号, 错误码, 现场指针)。
 * 处理完注册的 handler 后，若是时钟中断就问调度器要不要换任务。
 * 返回值 = 新任务的保存帧地址（0 = 不切换，原路 iret 回去）。 */
uint32_t irq_dispatch(uint32_t num, uint32_t err, struct regs *r);
