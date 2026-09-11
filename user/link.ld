/* 用户程序的链接脚本：代码放到虚拟地址 0x40000000，
 * 和内核 syscall.c 里的 USER_CODE_VADDR 必须一致 */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }

    /DISCARD/ : { *(.eh_frame .comment .note .note.*) }
}
