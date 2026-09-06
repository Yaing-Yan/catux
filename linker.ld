/*
 * linker.ld —— 告诉链接器：我们的内核应该长什么样
 *
 * 普通程序链接时遵循的是操作系统的约定；我们现在自己就是操作系统，
 * 所以约定得自己定：
 *   - ". = 1M;"    内核加载到物理地址 1MB 处（低 1MB 被 BIOS 和硬件占用）
 *   - multiboot 头放在最前面（引导程序只在文件前 8KB 里搜索它）
 *   - 各段按 4KB 对齐排好（以后开启分页时，页就按 4KB 划分）
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    _kernel_start = .;        /* 内核占用的内存范围，第 3 阶段管理内存时用 */

    .text : ALIGN(4K)
    {
        KEEP(*(.multiboot))   /* KEEP：防止链接器把"没人引用"的 multiboot 头删掉 */
        *(.text .text.*)
    }

    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }   /* 只读数据：字符串常量等 */

    .data   : ALIGN(4K) { *(.data .data.*) }        /* 已初始化的全局变量 */

    .bss    : ALIGN(4K) { *(COMMON) *(.bss .bss.*) } /* 未初始化的全局变量/栈 */

    _kernel_end = .;

    /DISCARD/ : { *(.eh_frame .comment .note .note.*) }
}
