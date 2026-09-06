/*
 * task.c —— 任务与调度器（阶段 4）
 *
 * 多任务的本质：每个任务一份"CPU 拍立得"（完整寄存器现场），
 * 时钟中断每 10ms 到来时：
 *
 *   1. 把当前任务的现场存起来（就存在它自己的内核栈上）
 *   2. 挑下一个任务（round-robin：轮流来）
 *   3. 跳到它上次被保存的现场，恢复寄存器，iret 回去继续跑
 *
 * 关键理解：每个任务的现场保存在它自己的内核栈上。
 * 中断帧本来就是压在"当时正在运行的任务"的栈上的，
 * 调度器只要把 esp 指到新任务栈上那帧的地址，popa+iretd
 * 自然就在新任务的地盘上完成恢复——物理上就是换个栈。
 */

#include <stdint.h>

#include "console.h"
#include "irq.h"
#include "task.h"

struct task {
    uint32_t esp;           /* 上次被打断时，栈上 regs 帧的地址 */
    uint8_t  alive;
};

static struct task tasks[MAX_TASKS];
static int n_tasks;         /* 已创建的任务数（任务 0 = main 本尊） */
static int cur;             /* 当前运行的任务下标 */

/* 给新任务的栈（4KB，16 对齐） */
static uint8_t stacks[MAX_TASKS][4096] __attribute__((aligned(16)));

void tasks_init(void)
{
    /* 任务 0 就是 kmain → shell 这条执行流，它第一次被时钟打断时
     * 现场自然会保存进 tasks[0]。这里只需登记它在册。 */
    n_tasks = 1;
    cur = 0;
    tasks[0].alive = 1;
}

void task_create(void (*entry)(void))
{
    if (n_tasks >= MAX_TASKS)
        return;

    struct task *t = &tasks[n_tasks];
    uint8_t *top = stacks[n_tasks] + sizeof(stacks[n_tasks]);

    /* 在新栈顶伪造一份"第一次被打断"的现场：
     * 寄存器全 0，eip 指向线程函数，eflags 开中断。
     * 调度器第一次切到它时，iret 就"恢复"到这个函数的开头。 */
    struct regs *r = (struct regs *)(top - sizeof(struct regs));
    r->eax = r->ecx = r->edx = r->ebx = 0;
    r->o_esp = r->ebp = r->esi = r->edi = 0;
    r->int_no = 0;
    r->err = 0;
    r->eip = (uint32_t)entry;
    r->cs = 0x08;                   /* 内核代码段 */
    r->eflags = 0x202;              /* bit1 恒 1 + IF=1（开中断） */

    t->esp = (uint32_t)r;
    t->alive = 1;
    n_tasks++;
}

uint32_t schedule(struct regs *r)
{
    if (n_tasks <= 1)
        return 0;                   /* 只有 main 一个任务：没必要切 */

    tasks[cur].esp = (uint32_t)r;   /* 现场就保存在当前任务的栈上 */

    int next = cur;
    do {                            /* round-robin：找下一个活着的任务 */
        next = (next + 1) % n_tasks;
    } while (!tasks[next].alive);
    cur = next;

    return tasks[cur].esp;          /* 0 之外的值 → 汇编桩执行切换 */
}
