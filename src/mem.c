/*
 * mem.c —— 物理内存管理（阶段 3a）
 *
 * 上电时谁拥有内存？——谁都不敢碰。BIOS 用了一部分（实模式下
 * 的中断向量表、EBDA），我们的内核占了一块（_kernel_start 到
 * _kernel_end），引导程序的数据也还躺在那儿。
 *
 * 第一步"摸清家底"：读 multiboot 的内存地图，打印每一区域的
 * 起止和类型（available / reserved）。
 *
 * 第二步"开始管家"：把 1MB 以上、内核之外的可用内存按 4KB 切成
 * 页帧，用 bitmap 记账。这是将来 kmalloc、页表、进程内存的
 * 全部弹药库。
 *
 * 简化说明：QEMU 的内存布局在 1MB 以上是连续的，所以我们用
 * mem_upper 算总量、从内核末尾开始整段管理；mmap 里若标了
 * 中间的空洞先不管（完整版以后逐段标记）。
 */

#include <stdint.h>

#include "multiboot.h"
#include "mem.h"
#include "console.h"

/* linker.ld 里定义的符号：内核映像的结束地址（自动对齐到段尾） */
extern uint8_t _kernel_end[];

#define MAX_FRAMES 32768        /* bitmap 容量：32768 帧 × 4KB = 128MB */

static uint8_t frame_bitmap[MAX_FRAMES / 8];   /* 每帧 1 个 bit */
static uint32_t frame_base;     /* 0 号帧对应的物理地址（内核末尾对齐 4KB） */
static uint32_t frame_total;    /* 实际管理的帧数 */
static uint32_t frame_free;     /* 空闲帧数 */

/* 把某个物理地址所在的帧标记为"已占用"（保留给引导程序等） */
static void frame_mark_used(uint32_t addr)
{
    if (addr < frame_base)
        return;
    uint32_t i = (addr - frame_base) / FRAME_SIZE;
    if (i >= frame_total)
        return;
    uint8_t mask = (uint8_t)(1 << (i % 8));
    if (!(frame_bitmap[i / 8] & mask)) {   /* 尚未占用才计数 */
        frame_bitmap[i / 8] |= mask;
        frame_free--;
    }
}

/* 打印 multiboot 内存地图 */
static void print_mmap(const struct multiboot_info *mb)
{
    if (!(mb->flags & (1 << 6))) {
        kprintf("[mem] WARNING: no memory map from loader!\n");
        return;
    }

    /* mmap 是一个变长数组：每一项的长度写在它自己的 size 字段里，
     * 所以循环推进方式是 addr += entry->size + 4 */
    for (struct multiboot_mmap *e = (struct multiboot_mmap *)ADDR64_TO32(mb->mmap_addr);
         (uint8_t *)e < (uint8_t *)ADDR64_TO32(mb->mmap_addr) + mb->mmap_length;
         e = (struct multiboot_mmap *)((uint8_t *)e + e->size + 4)) {

        kprintf("[mem] region %x - %x : %s\n",
                ADDR64_TO32(e->addr),
                ADDR64_TO32(e->addr + e->len),
                e->type == MMAP_TYPE_AVAILABLE ? "AVAILABLE" : "reserved");
    }
}

void mem_init(uint32_t mboot_addr)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)mboot_addr;

    kprintf("\n[mem] physical memory map:\n");
    print_mmap(mb);

    /* 管辖范围：内核末尾（对齐 4KB）到 1MB+mem_upper。
     * bitmap 先全 1（已占用），再算出实际帧数逐帧标 0（空闲）。 */
    frame_base = ((uint32_t)_kernel_end + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    uint32_t mem_end = 1024 * 1024 + mb->mem_upper * 1024;
    frame_total = (mem_end - frame_base) / FRAME_SIZE;
    if (frame_total > MAX_FRAMES)
        frame_total = MAX_FRAMES;

    for (uint32_t i = 0; i < sizeof(frame_bitmap); i++)
        frame_bitmap[i] = 0xFF;

    frame_free = 0;
    for (uint32_t i = 0; i < frame_total; i++) {
        frame_bitmap[i / 8] &= ~(1 << (i % 8));   /* 标记为空闲 */
        frame_free++;
    }

    /* ⚠️ 关键一步：把引导程序放进内存的模块（initrd）占用的帧
     * 标记为已占用。否则帧分配器会把它们当空闲发出去——
     * 页表一写就覆盖 initrd，文件系统读到全是他人的数据。
     * （这正是真实内核 reserve bootloader 内存的原因）
     * 要保留两块：模块数据本体 + 模块描述数组（mods_addr 处） */
    if (mb->flags & (1 << 3)) {
        for (uint32_t i = 0; i < mb->mods_count; i++) {
            const struct multiboot_mod *m =
                (const struct multiboot_mod *)(uintptr_t)(mb->mods_addr +
                    i * sizeof(struct multiboot_mod));
            uint32_t start = m->mod_start & ~0xFFFu;          /* 向下对齐 */
            uint32_t end = (m->mod_end + 0xFFF) & ~0xFFFu;    /* 向上对齐 */
            for (uint32_t a = start; a < end; a += FRAME_SIZE)
                frame_mark_used(a);
            kprintf("[mem] reserved %x-%x for initrd module %d\n",
                    start, end, i);
        }
        uint32_t arr_start = mb->mods_addr & ~0xFFFu;
        uint32_t arr_end = (uint32_t)(mb->mods_addr +
            mb->mods_count * sizeof(struct multiboot_mod) + 0xFFF) & ~0xFFFu;
        for (uint32_t a = arr_start; a < arr_end; a += FRAME_SIZE)
            frame_mark_used(a);
        kprintf("[mem] reserved %x-%x for mods array\n", arr_start, arr_end);
    }

    kprintf("[mem] managing %d frames (%d KB) from 0x%x\n",
            frame_total, frame_total * 4, frame_base);
}

/* ==================== TODO 7：页帧领用/归还 ====================
 * 两半工作都在这张 bitmap 上。回忆位运算三件套：
 *   测试第 i 位：  bitmap[i / 8] &  (1 << (i % 8))
 *   置位第 i 位：  bitmap[i / 8] |= (1 << (i % 8))
 *   清位第 i 位：  bitmap[i / 8] &= ~(1 << (i % 8))
 *
 * uint32_t alloc_frame(void)：
 *   从 i=0 开始找第一个"空闲"的 bit（找到就停，first-fit），
 *   置 1，frame_free--，返回 frame_base + i * FRAME_SIZE；
 *   找遍所有 frame_total 个都没有 → 返回 0。
 *
 * void free_frame(uint32_t frame_addr)：
 *   由地址反算帧号：i = (frame_addr - frame_base) / FRAME_SIZE，
 *   清位，frame_free++。
 *   （防御式思考：如果 frame_addr 不在管辖范围、或不是 4KB 对齐、
 *    或本来就没被分配，怎么办？先简单处理：不合理的直接 return） */

uint32_t alloc_frame(void)
{
    for (uint32_t i = 0; i < frame_total; i++) {          /* first-fit：从 0 号帧找起 */
        uint8_t mask = (uint8_t)(1 << (i % 8));
        if (!(frame_bitmap[i / 8] & mask)) {              /* 这一位是 0 = 空闲 */
            frame_bitmap[i / 8] |= mask;                  /* 领用：置 1 */
            frame_free--;
            return frame_base + i * FRAME_SIZE;           /* 帧号 → 物理地址 */
        }
    }
    return 0;                                             /* 找遍全场：内存耗尽 */
}

void free_frame(uint32_t frame_addr)
{
    /* 防御三连：范围外、没对齐 4KB、双重释放，都不理 */
    if (frame_addr < frame_base || (frame_addr & 0xFFF) != 0)
        return;

    uint32_t i = (frame_addr - frame_base) / FRAME_SIZE;
    if (i >= frame_total)
        return;

    uint8_t mask = (uint8_t)(1 << (i % 8));
    if (!(frame_bitmap[i / 8] & mask))
        return;                                           /* 本来就没分配出去 */

    frame_bitmap[i / 8] &= (uint8_t)~mask;                /* 归还：清 0 */
    frame_free++;
}

uint32_t mem_free_frames(void)
{
    return frame_free;
}

uint32_t mem_total_frames(void)
{
    return frame_total;
}
