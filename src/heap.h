#pragma once
#include <stdint.h>

/* 阶段 3c：内核堆
 * kmalloc：想要多少给多少（16 字节对齐）；失败返回 0
 * kfree：归还（传 kmalloc 的返回值） */
void heap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);

/* meminfo 用的统计 */
uint32_t kheap_used(void);
uint32_t kheap_free(void);
