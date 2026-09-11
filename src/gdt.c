/*
 * gdt.c —— 全局描述符表 + TSS（阶段 6）
 *
 * 之前的 GDT 是引导程序给的（内核代码/数据两个段）。要跑用户态，
 * 必须自己建表，补上：
 *   - 用户代码段、用户数据段（DPL=3，ring3 可加载）
 *   - TSS（Task State Segment）：CPU 从 ring3 进 ring0 时不会用
 *     ring3 的栈（那是用户可控的，用了等于把内核交给用户），
 *     而是查 TSS 里的 ss0/esp0 换成内核栈。TSS 就是"内核的
 *     应急栈地址登记表"。
 *
 * 段描述符还是那套 8 字节格式：base/limit 拆开放，
 * access 字节里 P|DPL|类型，gran 字节里颗粒度(4K)+32位标志。
 */

#include <stdint.h>

#include "string.h"
#include "gdt.h"

#define GDT_ENTRIES 6

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;      /* 颗粒度(4K)+32位标志+limit 高 4 位 */
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;      /* ring0 栈指针（进内核时 CPU 换到这里） */
    uint32_t ss0;       /* ring0 栈段 = 内核数据段 */
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;   /* I/O 位图偏移；>= limit 表示"无位图"=禁止 ring3 端口 I/O */
} __attribute__((packed));

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdtp;
static struct tss_entry tss;

/* boot.S 导出的内核栈顶（数值存在这个符号里） */
extern uint32_t kernel_stack_top;

static void set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].limit_lo = (uint16_t)(limit & 0xFFFF);
    gdt[i].base_lo  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].access   = access;
    gdt[i].gran     = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].base_hi  = (uint8_t)((base >> 24) & 0xFF);
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_init(void)
{
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = SEL_KDATA;
    tss.esp0 = kernel_stack_top;          /* 用户态进内核时换到这个栈 */
    tss.iomap_base = (uint16_t)sizeof(tss); /* 无 I/O 位图 → ring3 碰端口即 GP */

    set_gate(0, 0, 0, 0, 0);                        /* 0x00 空描述符（硬件要求） */
    set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);            /* 0x08 内核代码 P|DPL0|可读可执行 */
    set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);            /* 0x10 内核数据 P|DPL0|可写 */
    set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);            /* 0x18 用户代码 P|DPL3 */
    set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);            /* 0x20 用户数据 P|DPL3 */
    set_gate(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);  /* 0x28 TSS */

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdtp));

    /* 重载 cs：必须用远跳转，才能让 CS 指向新表里的代码段。
     * 1f 是"下一个局部标号"，跳过去继续往下执行 */
    __asm__ volatile("ljmp $0x08, $1f\n1:");

    /* 重载数据段寄存器 */
    __asm__ volatile("mov $0x10, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     "mov %%ax, %%es\n\t"
                     "mov %%ax, %%fs\n\t"
                     "mov %%ax, %%gs" ::: "eax");

    /* ltr：加载任务寄存器，让 CPU 知道 TSS 在哪 */
    __asm__ volatile("mov $0x28, %%ax\n\tltr %%ax" ::: "eax");
}
