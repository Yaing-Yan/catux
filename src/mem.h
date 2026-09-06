#pragma once
#include <stdint.h>

/* 阶段 3a：物理内存管理
 *
 * 物理内存按 4KB 一格切成"页帧"（frame），用一张 bitmap 记账：
 * 每个页帧对应一个 bit，0=空闲 1=已占用。
 * alloc_frame/free_frame 就是这张表的"领用/归还"。 */

#define FRAME_SIZE 4096

/* 传入 kmain 收到的 multiboot 信息地址，完成初始化并打印内存地图 */
void mem_init(uint32_t mboot_addr);

/* 领用一个 4KB 页帧，返回它的物理地址；没有了返回 0 */
uint32_t alloc_frame(void);

/* 归还一个页帧（传 alloc_frame 返回的地址） */
void free_frame(uint32_t frame_addr);

/* 当前还剩多少空闲页帧（meminfo 命令用） */
uint32_t mem_free_frames(void);

/* 管辖的总页帧数 */
uint32_t mem_total_frames(void);
