#pragma once
#include <stdint.h>

/* 阶段 6：重建 GDT —— 加入用户段和 TSS */

/* 段选择子（高 13 位是 GDT 下标，低 3 位是 RPL 特权级） */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UCODE 0x18   /* | 3 = 0x1B 才是用户可见的选择子 */
#define SEL_UDATA 0x20   /* | 3 = 0x23 */
#define SEL_TSS   0x28

void gdt_init(void);

/* 设置 TSS 里记录的"ring0 栈"——ring3 中断/系统调用时 CPU 换到它。
 * 现在只有一个用户任务，用启动栈顶即可。 */
void tss_set_kernel_stack(uint32_t esp0);
