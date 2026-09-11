/*
 * idt.c —— 中断描述符表（阶段 2a）
 *
 * 问题：异常/中断发生时，CPU 怎么知道该跳去执行哪段代码？
 * 答案：查表。这张表叫 IDT（Interrupt Descriptor Table），
 * 固定 256 项，每项是一个 8 字节的"门描述符"，记录：
 *   - 处理函数的地址
 *   - 用哪个代码段（选择子）
 *   - 门的开合状态和权限
 *
 * 向量号 0~31 被 CPU 钦定为"异常"（除零、页故障、非法指令……），
 * 32~255 留给操作系统自由安排（2b 里时钟会用 32、键盘 33）。
 *
 * 装表的指令是 lidt，它要的也不是表本身，而是一个 6 字节的
 * "表描述符"（大小 + 地址）—— 和 GDT 一个套路。
 */

#include <stdint.h>

#include "idt.h"
#include "console.h"
#include "irq.h"
#include "pic.h"
#include "task.h"

/* 门描述符：8 字节。packed 禁止编译器塞对齐空隙——
 * 硬件按字节逐个读，多一个空隙就全错位了 */
struct idt_entry {
    uint16_t base_lo;   /* 处理函数地址低 16 位 */
    uint16_t sel;       /* 代码段选择子：0x08（引导程序 GDT 的内核代码段） */
    uint8_t  zero;      /* 恒 0，硬件规定 */
    uint8_t  flags;     /* 0x8E = 存在(1) | ring0(00) | 32位中断门(0xE) */
    uint16_t base_hi;   /* 处理函数地址高 16 位 */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;     /* 表的字节数 - 1 */
    uint32_t base;      /* 表的线性地址 */
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

/* isr.S 用宏生成的 32 个汇编入口桩 */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

typedef void (*isr_func_t)(void);
static const isr_func_t isr_table[32] = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

/* isr.S 用宏生成的 16 个 IRQ 入口桩（向量号 32~47） */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static const isr_func_t irq_table[16] = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15,
};

/* 把第 n 号门指向 handler，并设置为"存在、ring0、32位中断门" */
static void idt_set_gate(int n, uint32_t handler)
{
    idt[n].base_lo = (uint16_t)(handler & 0xFFFF);
    idt[n].base_hi = (uint16_t)(handler >> 16);
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
}

/* 硬件中断用的门是"中断门"（DPL=0，只有内核能触发）；
 * 系统调用门必须开 DPL=3，否则用户执行 int 0x80 会触发 GP。*/
void idt_set_user_gate(int n, uint32_t handler)
{
    idt_set_gate(n, handler);
    idt[n].flags = 0xEE;   /* 存在 | DPL=3 | 32位中断门 */
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
        idt[i].flags = 0;              /* 先全部标记"门不存在" */

    for (int n = 0; n < 32; n++)       /* 0~31 号：CPU 异常 */
        idt_set_gate(n, (uint32_t)isr_table[n]);

    for (int n = 0; n < 16; n++)       /* 32~47 号：硬件中断（重映射后的 IRQ） */
        idt_set_gate(32 + n, (uint32_t)irq_table[n]);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));   /* 告诉 CPU：表在这儿 */
}

/* CPU 异常的名字表（0~31），isr_dispatch 打印用 */
static const char *exc_names[32] = {
    "Divide By Zero",       "Debug",              "NMI",                "Breakpoint",
    "Overflow",             "BOUND Range Exceeded","Invalid Opcode",    "Device Not Available",
    "Double Fault",         "Coprocessor Segment", "Invalid TSS",       "Segment Not Present",
    "Stack Fault",          "General Protection",  "Page Fault",        "Reserved",
    "x87 FPU Error",        "Alignment Check",     "Machine Check",     "SIMD FP Exception",
    "Virtualization",       "Control Protection",  "Reserved",          "Reserved",
    "Reserved",             "Reserved",            "Reserved",          "Reserved",
    "Reserved",             "Reserved",            "Reserved",          "Reserved",
};

/* ==================== 实现要点 4：异常分发器 ====================
 * 汇编桩会把 (num, err) 当作两个参数传进来。
 * 用你已经会用的 kprintf 实现：
 *   1. 打印一行警报，格式（绿色可以用 vga_set_color(0x0C, 0) 红字）：
 *      "!!! EXCEPTION <num>: <name> (err=<错误码，十六进制>)"
 *   2. 防御式编程：如果 num >= 32，exc_names[num] 会读越界——
 *      此时打印 "Unknown" 代替名字（想想：为什么必须先判断再取数组？）
 *   3. 打印完直接 return 即可，isr.S 的桩会接管挂机。
 * 验收：main.c 会故意算一次 1/0，屏幕上应出现你的红色警报。 */
void isr_dispatch(uint32_t num, uint32_t err)
{

    /* 实现：见上方说明 */
    vga_set_color(0x0C, 0);

    const char *name;
    if (num >= 32)
        name = "Unknown";
    else
        name = exc_names[num];
    kprintf("\n!!! EXCEPTION %d: %s (err=%x)",num,name,err);
    vga_set_color(0x0F, 0x00);
    return;
}

/* ============ IRQ 处理框架（阶段 2b）============ */

static irq_handler_t irq_handlers[16];

void irq_register(int irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;
}

/* ==================== 实现要点 5：中断分发 + EOI 应答 ====================
 * 汇编桩（isr.S 的 irq_common）把 (向量号, 错误码) 传进来。
 * 和异常不同：这里处理完要 return，桩会恢复寄存器、iret 回去
 * 继续跑被打断的代码——所以绝对不能在这里挂机！
 * 步骤：
 *   1. int n = (int)num - 32;   还原成 IRQ 编号（0~15）
 *   2. 若 n 在 0~15 且注册了处理函数，调用它：
 *      if (n >= 0 && n < 16 && irq_handlers[n])
 *          irq_handlers[n]();
 *   3. pic_send_eoi(n);   向 PIC 发"处理完毕回执"。
 *      ⚠️ 不发 EOI 的后果：PIC 认为你还在忙，之后时钟永远
 *      只响一拍就沉默。
 * 验收：屏幕每秒出现一行 [CatUX uptime: N s] */
uint32_t irq_dispatch(uint32_t num, uint32_t err, struct regs *r)
{
    (void)err;   /* IRQ 没有 CPU 错误码，恒为桩补的 0 */
    int n = (int)num - 32;
    if (n >= 0 && n < 16 && irq_handlers[n])
        irq_handlers[n]();

    if (n >= 0 && n < 16)
        pic_send_eoi(n);

    /* 时钟中断是调度点：问调度器要不要换任务。
     * 返回新任务的保存帧地址，汇编桩会跳过去恢复。 */
    if (num == 32)
        return schedule(r);
    return 0;
}
