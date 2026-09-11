#pragma once
#include <stdint.h>

/* 初始化中断描述符表 IDT（前 32 个向量 = CPU 异常）
 * 详见 docs/02-idt.md */
void idt_init(void);

/* 开一个"用户也能触发"的门（DPL=3），系统调用 int 0x80 用它 */
void idt_set_user_gate(int n, uint32_t handler);

/* 异常分发器 —— 汇编桩（isr.S）触发异常时会带着两个参数调用它：
 *   num：异常编号（0~31）
 *   err：错误码（CPU 对部分异常额外提供，没有时为补的 0）
 * 函数体见 idt.c 的 isr_dispatch（异常一律致命：打印后挂机）。 */
void isr_dispatch(uint32_t num, uint32_t err);
