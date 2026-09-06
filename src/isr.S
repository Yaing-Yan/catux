/*
 * isr.S —— 异常入口桩（阶段 2a）
 *
 * 完整链路：CPU 发生异常 → 查 IDT 第 num 号门 → 跳到 isrN（本文件）
 * → 把参数补齐到 C 调用约定 → call isr_dispatch（C 写的分发器）。
 *
 * CPU 硬件行为：触发异常时自动往栈上压 3 个值（eip/cs/eflags），
 * 部分异常还会多压一个"错误码"。有的压有的不压，这就麻烦了——
 * 所以每个桩先用宏统一：没有错误码的补一个假的 0，
 * 保证所有异常进 C 函数时栈布局一模一样。
 */

.intel_syntax noprefix

/* 无错误码的异常：补一个假错误码，再压异常编号 */
.macro ISR_NOERR n
.global isr\n
isr\n:
    push 0
    push \n
    jmp isr_common
.endm

/* 带错误码的异常：CPU 已压了错误码，只需压编号 */
.macro ISR_ERR n
.global isr\n
isr\n:
    push \n
    jmp isr_common
.endm

/* 公共出口：此时栈上（从顶往下）是 编号、错误码，
 * 正好等于 cdecl 调用约定的两个参数 (num, err)，
 * call 指令再压上返回地址，C 函数就能直接读参数了。 */
isr_common:
    call isr_dispatch
.hang:
    cli
    hlt                     /* 阶段 2a：异常处理后不返回现场，就地挂机。
                             * 2b 加入时钟中断时，这里会换成保存寄存器 + iret */
    jmp .hang

/* CPU 定义的 32 个异常。带错误码的：8,10,11,12,13,14,17 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* ============ 硬件中断（IRQ）路径：阶段 2b 新增 ============
 * 和异常的本质区别：中断处理完必须回到被打断的地方继续跑
 * （时钟每 10ms 打断一次，要是回不去，内核干一毫秒活就死了）。
 * 所以：保存全部寄存器 → 处理 → 恢复 → iret 原路返回。 */
.macro IRQ n
.global irq\n
irq\n:
    push 0                       /* IRQ 不压错误码，补 0 统一栈布局 */
    push \n+32                   /* 重映射后：向量号 = IRQ 编号 + 32 */
    jmp irq_common
.endm

irq_common:
    pusha                        /* 保存 8 个通用寄存器（32 字节，本函数的"存档"） */
    mov  eax, [esp+32]           /* pusha 之上：向量号 */
    mov  edx, [esp+36]           /* pusha 之上：错误码（恒 0） */
    push edx                     /* cdecl 传参：从右往左，先 err 后 num */
    push eax
    call irq_dispatch            /* C 分发器（idt.c，TODO 5） */
    add  esp, 8                  /* 扔掉两个参数 */
    popa                         /* 读档：8 个寄存器原样恢复 */
    add  esp, 8                  /* 扔掉桩压的向量号+错误码 */
    iretd                        /* 弹出 eip/cs/eflags，回到被打断的代码。
                                  * ⚠️ GAS 怪癖：必须写 iretd（32位）！
                                  * 裸写 iret 会被当成 16 位的 iretw，
                                  * 只弹一半的栈，返回即 GP 崩溃 */

IRQ 0                            /* 时钟 */
IRQ 1                            /* 键盘 */
IRQ 2                            /* 级联（从片挂在这针上） */
IRQ 3                            /* 串口2 */
IRQ 4                            /* 串口1 */
IRQ 5                            /* 并口2/声卡 */
IRQ 6                            /* 软驱 */
IRQ 7                            /* 并口1 */
IRQ 8                            /* RTC 实时时钟（从片） */
IRQ 9
IRQ 10
IRQ 11
IRQ 12                           /* PS/2 鼠标 */
IRQ 13                           /* 协处理器 */
IRQ 14                           /* 主硬盘 */
IRQ 15                           /* 从硬盘 */
