#pragma once
#include <stdint.h>

/*
 * multiboot.h —— 引导程序留给我们的"开机礼盒"结构定义
 *
 * 还记得 boot.S 把 EBX 压栈、kmain 收到的 addr 参数吗？
 * 它指向这个结构。哪些字段有效由 flags 的对应位决定：
 *   bit0=1 → mem_lower/mem_upper 有效（常规内存大小，单位 KB）
 *   bit6=1 → mmap_addr/mmap_length 有效（详细内存地图）
 */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;     /* 1MB 以下的常规内存（KB），通常 640 */
    uint32_t mem_upper;     /* 1MB 以上的内存（KB）—— 主要看这个 */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;   /* 内存地图的字节长度 */
    uint32_t mmap_addr;     /* 内存地图的物理地址 */
    /* 后面还有驱动盘、配置表等字段，暂用不到 */
} __attribute__((packed));

/* 内存地图里的一项：一段连续物理内存的描述 */
struct multiboot_mmap {
    uint32_t size;          /* 本结构剩余部分的字节数（20） */
    uint64_t addr;          /* 这段内存的起始物理地址（64 位！） */
    uint64_t len;           /* 长度（64 位） */
    uint32_t type;          /* 1=可用  2=保留  3=可 reclaimed  4=NV RAM */
} __attribute__((packed));

#define MMAP_TYPE_AVAILABLE 1

/* 引导加载的模块（initrd 就是第 0 号模块） */
struct multiboot_mod {
    uint32_t mod_start;     /* 模块在内存里的起始物理地址 */
    uint32_t mod_end;       /* 结束地址（不含） */
    uint32_t cmdline;       /* 模块命令行字符串（我们不用） */
    uint32_t pad;
} __attribute__((packed));

/* QEMU 的内存都在 4GB 以内，64 位地址的高 32 位恒为 0。
 * 内核暂用 32 位物理地址，转换时断言一下高位为零。 */
#define ADDR64_TO32(x)  ({ uint64_t _x = (x); (uint32_t)_x; })
