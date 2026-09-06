#pragma once
#include <stdint.h>

/* 硬件中断（IRQ）处理框架
 * IRQ 编号：0=时钟 1=键盘 2=级联 14=主硬盘 ...
 * 重映射后：向量号 = IRQ + 32 */

typedef void (*irq_handler_t)(void);

/* 注册处理函数：对应 IRQ 到来时被 irq_dispatch 调用 */
void irq_register(int irq, irq_handler_t handler);

/* 汇编桩（isr.S 的 irq_common）会调用它，参数是 (向量号, 错误码)。
 * 函数体在 idt.c，是 TODO 5。 */
void irq_dispatch(uint32_t num, uint32_t err);
