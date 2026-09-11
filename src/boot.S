/*
 * boot.S —— CatUX 的第一段代码（内核入口）
 *
 * 开机时发生的事：
 *   BIOS → 引导程序（GRUB 或 QEMU 的 -kernel）→ 在内核文件里找到
 *   Multiboot 头 → 把内核加载到内存 1MB 处 → 切到 32 位保护模式
 *   → 跳转到 _start
 *
 * 此刻 C 语言环境还不存在：没有栈、没有库、没有变量。
 * 这里只做三件事：预留栈 → 把引导程序留下的参数摆好 → call kmain 进入 C。
 */

.intel_syntax noprefix

.set MB_MAGIC,    0x1BADB002              /* Multiboot 规范规定的魔数 */
.set MB_FLAGS,    0                       /* 对引导程序没有任何特殊要求 */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)  /* 校验和：三个数相加必须等于 0 */

/* ---- Multiboot 头 ----
 * 引导程序会在内核文件的前 8KB 内搜索这三个 4 字节整数，
 * 验证三者之和为 0 后，才承认我们是可引导的内核。 */
.section .multiboot, "a", @progbits
.balign 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM

/* ---- 内核栈 ----
 * C 语言的一切——局部变量、函数调用、参数传递——都依赖栈。
 * 在 .bss 段（只占内存、不占文件的空段）预留 16KB。 */
.section .bss
.balign 16
stack_bottom:
    .skip 16384
stack_top:

/* 把栈顶地址导出给 C（TSS 的 esp0 要用它：ring3 进内核时换到这个栈）。
 * 它是个"有初值的常量"，所以放 .rodata 而不是 .bss */
.section .rodata
.global kernel_stack_top
kernel_stack_top:
    .long stack_top

.section .text
.global _start                    /* 链接脚本指定 ENTRY(_start)，从这里开始 */
_start:
    mov  esp, offset stack_top    /* 栈指针指向栈顶（栈向低地址生长）。
                                   * GAS 的 Intel 语法里，取符号的"地址"必须加
                                   * offset 前缀；不写就是去读那块内存的内容！ */
    push ebx                      /* 第 2 个参数：multiboot 信息结构体地址（引导程序放在 EBX） */
    push eax                      /* 第 1 个参数：魔数（引导程序放在 EAX） */
    call kmain                    /* 进入 C 世界！理论上永远不再回来 */

    /* 万一 kmain 返回了：关中断 + 挂起 CPU，绝不带病乱跑 */
.hang:
    cli
    hlt
    jmp .hang

/* ---- 两个最基础的硬件 I/O 原语（以后所有驱动的地基）----
 * x86 除了内存寻址，还有一条"端口"通道：
 * out 指令往设备寄存器写字节，in 指令读字节。
 * 之后串口、键盘、磁盘……的驱动全部通过这两个 C 可调用的函数实现。 */
.global outb
outb:                             /* void outb(uint16_t port, uint8_t val) */
    mov  edx, [esp+4]             /* 栈布局：[esp]=返回地址 [esp+4]=port [esp+8]=val */
    mov  eax, [esp+8]
    out  dx, al
    ret

.global inb
inb:                              /* uint8_t inb(uint16_t port) */
    mov  edx, [esp+4]
    xor  eax, eax
    in   al, dx
    ret

.global io_wait
io_wait:                          /* 约 1 微秒延迟：往诊断端口 0x80 写个
                                   * 无关字节。0x80 是老式 POST 诊断口，
                                   * 写它没有任何副作用，纯粹用来拖时间 */
    mov  al, 0
    out  0x80, al
    ret
