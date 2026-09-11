/*
 * syscall.c —— 系统调用与用户态切换（阶段 6）
 *
 * 用户程序跑在 ring3：没有特权指令、看不到内核内存、碰端口就崩。
 * 它想干活只能"按门铃"——执行 int 0x80，CPU 通过 IDT 0x80 号门
 * 进入 ring0（这门特意开给 DPL3，普通异常门可不行），
 * 并借 TSS 换到内核栈。内核按 eax 里的调用号分发。
 *
 * 用户地址空间的布局（本阶段手工摆好）：
 *   0x40000000  一页 = 用户程序代码
 *   0x40001000  一页 = 用户栈（栈顶 0x40002000）
 * 这两页在页表里带 US 位；内核的页没有 US 位 → 用户看不见内核。
 */

#include <stdint.h>

#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "mem.h"
#include "paging.h"
#include "fs.h"
#include "heap.h"
#include "string.h"
#include "syscall.h"

#define USER_CODE_VADDR  0x40000000u
#define USER_STACK_VADDR 0x40001000u
#define USER_LIMIT       0x40002000u      /* 用户地址空间上限（不含） */

#define SYS_EXIT  0
#define SYS_WRITE 1

extern void isr128(void);
extern void enter_usermode(uint32_t entry, uint32_t user_stack);

/* 用户态程序专属的"内核栈"：ring3 发生系统调用/中断时，
 * CPU 经 TSS 换到这块栈，而不是启动栈。
 * 为什么必须独立：启动栈上还压着 shell 的活栈帧（我们要返回的
 * 调用链），系统调用帧从栈顶往下长会直接踩坏它们。
 * 真内核里每个进程都有自己的内核栈，这就是它的雏形。 */
static uint8_t syscall_stack[8192] __attribute__((aligned(16)));

void syscall_init(void)
{
    idt_set_user_gate(0x80, (uint32_t)isr128);
    tss_set_kernel_stack((uint32_t)(syscall_stack + sizeof(syscall_stack)));
}

/* 内核现场暂存 —— 手写版 setjmp/longjmp。
 * 除了 eip/esp，还必须保存"被调用者保存寄存器"（ebp/ebx/esi/edi）：
 * 它们跨越函数调用保持不变是 ABI 的承诺，我们跳出内核调用链再跳回来，
 * 必须把这份承诺兑现，否则恢复后的代码读到的全是用户态期间留下的垃圾。
 * （IF 也要手动开：中断门进来时 CPU 清了它，我们不 iret 就回不去） */
struct kernel_ctx {
    uint32_t eip, esp, ebp, ebx, esi, edi;
};
static struct kernel_ctx kctx;

/* 用户传进来的指针必须落在用户地址空间里！
 * 否则用户程序可以骗内核替它读内核内存（经典提权漏洞）。
 * 真实内核要逐页查页表权限，这里先用范围检查。 */
static int is_user_ptr(uint32_t p, uint32_t len)
{
    return p >= USER_CODE_VADDR && p + len >= p && p + len <= USER_LIMIT;
}

static int sys_write(uint32_t buf, uint32_t len)
{
    if (!is_user_ptr(buf, len)) {
        kprintf("[syscall] write: bad user pointer 0x%x, blocked!\n", buf);
        return -1;
    }
    for (uint32_t i = 0; i < len; i++)
        kputc(*(const char *)(uintptr_t)(buf + i));
    return (int)len;
}

__attribute__((noreturn)) static void sys_exit(int code)
{
    kprintf("\n[syscall] exit(%d): user program finished, back to the kernel shell\n",
            code);
    /* 恢复内核数据段（进用户态前改成了用户段），
     * 再恢复 callee-saved 寄存器 + esp，最后 sti + 跳回保存点 */
    __asm__ volatile("mov $0x10, %%ax\n\tmov %%ax, %%ds\n\tmov %%ax, %%es" ::: "eax");
    __asm__ volatile(
        "mov %0, %%esp\n\t"
        "mov %1, %%ebp\n\t"
        "mov %2, %%ebx\n\t"
        "mov %3, %%esi\n\t"
        "mov %4, %%edi\n\t"
        "sti\n\t"
        "jmp *%5"
        : : "m"(kctx.esp), "m"(kctx.ebp), "m"(kctx.ebx),
            "m"(kctx.esi), "m"(kctx.edi), "m"(kctx.eip));
    __builtin_unreachable();
}

/* 汇编桩调用这里；改 r->eax 就是给用户程序设置返回值
 * （popa 会把改过的值恢复到 eax） */
uint32_t syscall_dispatch(uint32_t num, uint32_t err, struct regs *r)
{
    (void)num;
    (void)err;

    switch (r->eax) {
    case SYS_EXIT:
        sys_exit((int)r->ebx);
        break;
    case SYS_WRITE:
        r->eax = (uint32_t)sys_write(r->ecx, r->edx);   /* 参数：ebx=fd, ecx=buf, edx=len */
        break;
    default:
        kprintf("[syscall] unknown call %d\n", r->eax);
        r->eax = (uint32_t)-1;
        break;
    }
    return 0;   /* 0 = 不换任务，iret 回用户态继续跑 */
}

void user_run(void)
{
    char *blob = kmalloc(4096);
    if (!blob) {
        kprintf("[user] out of memory\n");
        return;
    }
    int n = fs_read("hello.bin", blob, 4096);
    if (n <= 0) {
        kprintf("[user] program 'hello.bin' not found in initrd\n");
        kfree(blob);
        return;
    }

    uint32_t code_frame = alloc_frame();
    uint32_t stack_frame = alloc_frame();
    if (!code_frame || !stack_frame) {
        kprintf("[user] out of frames\n");
        kfree(blob);
        return;
    }

    /* 映射用户页：带 PAGE_USER，用户可读写。
     * 内核的页没有这个位 → ring3 永远碰不到内核内存 */
    paging_map_page(USER_CODE_VADDR, code_frame, PAGE_RW | PAGE_USER);
    paging_map_page(USER_STACK_VADDR, stack_frame, PAGE_RW | PAGE_USER);

    memcpy((void *)(uintptr_t)USER_CODE_VADDR, blob, (uint32_t)n);
    kfree(blob);

    kprintf("[user] loaded %d bytes at 0x%x, stack 0x%x, entering ring3...\n",
            n, USER_CODE_VADDR, USER_LIMIT);

    /* 保存内核现场（sys_exit 会跳回来），然后一去不回 */
    kctx.eip = (uint32_t)&&resume;
    __asm__ volatile(
        "mov %%esp, %0\n\t"
        "mov %%ebp, %1\n\t"
        "mov %%ebx, %2\n\t"
        "mov %%esi, %3\n\t"
        "mov %%edi, %4"
        : "=m"(kctx.esp), "=m"(kctx.ebp), "=m"(kctx.ebx),
          "=m"(kctx.esi), "=m"(kctx.edi));

    enter_usermode(USER_CODE_VADDR, USER_LIMIT);   /* 栈顶 */

resume:
    kprintf("[user] kernel context restored.\n");
}
