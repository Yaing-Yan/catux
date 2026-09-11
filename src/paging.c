/*
 * paging.c —— 分页：虚拟内存（阶段 3b/3c：按需建页表版）
 *
 * 从开启分页那一刻起，C 指针里的地址全部是"虚拟地址"，CPU 每次
 * 访问内存都自动查页表翻译：虚拟 → 物理。翻译不到 → 页故障（异常14）。
 *
 * x86 32 位两级页表，虚拟地址切三段：
 *
 *     31       22 21       12 11          0
 *    ┌───────────┬───────────┬─────────────┐
 *    │ 页目录索引 │ 页表索引   │  页内偏移    │
 *    │  (10位)    │  (10位)   │   (12位)    │
 *    └───────────┴───────────┴─────────────┘
 *
 * cr3 → 页目录 →（高10位）页表 →（中10位）4KB 物理页帧 →（低12位）偏移。
 * 表项低 12 位是属性（存在/可写/用户态…），高 20 位是物理帧号。
 *
 * 本版改进：页表不再静态预留，而是在 paging_map_page 里"按需创建"——
 * 目录项为空就 alloc_frame() 领一帧物理内存当新页表。内核需要映射
 * 任何新区域时，页表体系自动生长。这是对 3a 物理分配器的第一次实战调用。
 */

#include <stdint.h>

#include "console.h"
#include "mem.h"
#include "paging.h"

static uint32_t page_dir[1024] __attribute__((aligned(4096)));

void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t dir_i = virt >> 22;                 /* 高 10 位：目录索引 */
    uint32_t tbl_i = (virt >> 12) & 0x3FF;       /* 中 10 位：表内索引 */

    /* 这个目录项还没挂页表？现场领一帧建一张。
     * 注：新表直接按物理地址摸——恒等映射下"物理=虚拟"暂时成立；
     * 等帧分配超出 16MB 时要引入临时映射窗口（记在账上）。 */
    if (!(page_dir[dir_i] & PAGE_PRESENT)) {
        uint32_t t_phys = alloc_frame();
        if (!t_phys) {
            kprintf("!!! paging: out of frames for page table\n");
            return;
        }
        uint32_t *t = (uint32_t *)t_phys;
        for (int i = 0; i < 1024; i++)           /* 新表必须全清零： */
            t[i] = 0;                            /* 全 0 = 1024 个"不存在" */
        page_dir[dir_i] = t_phys | PAGE_RW | PAGE_PRESENT | (flags & PAGE_USER);
    }
    page_dir[dir_i] |= (flags & PAGE_USER);      /* 用户页需要目录项也带 US */

    uint32_t *pt = (uint32_t *)(page_dir[dir_i] & 0xFFFFF000);
    pt[tbl_i] = (phys & 0xFFFFF000) | flags | PAGE_PRESENT;

    /* TLB 缓存了旧翻译，改映射必须作废该页的缓存行 */
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_init(void)
{
    /* 恒等映射低 64MB：内核自己 + initrd 都住这儿。
     * （QEMU 把 initrd 模块放在内核镜像后面的内存里，
     *   映射范围放宽到 64MB 保证它能被直接摸到） */
    for (uint32_t p = 0; p < 64u * 1024 * 1024; p += 4096)
        paging_map_page(p, p, PAGE_RW);

    /* 魔法窗口：虚拟 0x80000000 → VGA 显存物理 0xB8000 */
    paging_map_page(0x80000000, 0xB8000, PAGE_RW);

    /* 装页目录，翻 cr0 的 PG 位。之后所有地址都走翻译 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_dir) : "memory");

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}
