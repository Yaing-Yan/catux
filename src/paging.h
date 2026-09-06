#pragma once
#include <stdint.h>

/* 页表项/页目录项的属性位（低 12 位是标志，高 20 位是物理地址 >> 12） */
#define PAGE_PRESENT (1 << 0)    /* 该页在内存里 */
#define PAGE_RW      (1 << 1)    /* 1=可写  0=只读 */

/* 开启分页：恒等映射低 16MB + 建立 VGA 别名窗口，最后翻 cr0 的 PG 位 */
void paging_init(void);

/* 建立一条映射：虚拟地址 virt → 物理地址 phys（都必须 4KB 对齐） */
void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
