#pragma once
#include <stdint.h>

/* 阶段 5：迷你文件系统（tar-as-FS on initrd）
 *
 * 接口刻意设计得像个真正文件系统的子集——
 * 以后换成磁盘上的 ext2-lite 时，shell 一行不用改。 */

void fs_init(uint32_t mboot_addr);   /* 从 multiboot 信息找到 initrd */

void fs_ls(void);                    /* 列出所有文件（ls 命令） */

/* 读文件内容到 buf（最多 buflen-1 字节 + '\0'）。
 * 成功返回字节数，文件不存在返回 -1。 */
int fs_read(const char *name, char *buf, uint32_t buflen);
