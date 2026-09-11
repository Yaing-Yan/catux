/*
 * fs.c —— 迷你文件系统：把 tar 归档当磁盘用（阶段 5）
 *
 * tar 格式意外地适合当第一个文件系统：
 *   - 每个文件 = 512 字节头 + 数据（补齐到 512 的倍数）
 *   - 头部自带"目录项"：文件名[100]、大小（八进制 ASCII 字符串！）
 *   - 连续两个全零块表示归档结束
 * 宿主机用 tar 命令打包，引导程序塞进内存（initrd），
 * 内核里零依赖解析。1979 年的格式，2026 年当磁盘用。
 *
 * 为什么先做 initrd 而不是磁盘驱动：initrd 让"文件系统"和
 * "块设备驱动"两件事解耦——先在内存里把 FS 逻辑写对，
 * 等阶段后期的 ATA 驱动就绪，把 fs_base 换成"读磁盘扇区"
 * 就能升级到真磁盘，上层（shell/ls/cat）无感。
 */

#include <stdint.h>

#include "multiboot.h"
#include "console.h"
#include "string.h"
#include "fs.h"

/* tar ustar 头部，共 512 字节（ packed 保证没有对齐空隙） */
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];       /* 文件大小，八进制 ASCII，如 "00000000014" */
    char mtime[12];
    char chksum[8];
    char type;           /* '0'=普通文件，'5'=目录 */
    char linkname[100];
    char magic[6];
    char pad[247];       /* 补满 512（真实 tar 还有更多字段，我们不关心） */
} __attribute__((packed));

static uint8_t  *fs_base;    /* 归档在内存里的起点 */
static uint32_t  fs_size;

/* 八进制 ASCII → 数字。tar 的祖传设计 */
static uint32_t octal_to_uint(const char *s, int len)
{
    uint32_t v = 0;
    for (int i = 0; i < len && s[i]; i++)
        if (s[i] >= '0' && s[i] <= '7')
            v = v * 8 + (uint32_t)(s[i] - '0');
    return v;
}

/* 当前文件头的"大小"，返回下一个文件头的地址 */
static inline struct tar_header *step(struct tar_header *h)
{
    uint32_t sz = octal_to_uint(h->size, 12);
    return (struct tar_header *)((uint8_t *)h + 512 + ((sz + 511) & ~511u));
}

void fs_init(uint32_t mboot_addr)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)mboot_addr;

    if (!(mb->flags & (1 << 3)) || mb->mods_count == 0) {
        kprintf("[fs] WARNING: no initrd module from loader\n");
        return;
    }

    /* initrd = 第 0 号模块 */
    const struct multiboot_mod *m =
        (const struct multiboot_mod *)(uintptr_t)mb->mods_addr;
    fs_base = (uint8_t *)(uintptr_t)m->mod_start;
    fs_size = m->mod_end - m->mod_start;

    uint32_t n = 0;
    for (struct tar_header *h = (struct tar_header *)fs_base; h->name[0]; h = step(h))
        n++;

    kprintf("[fs] initrd at 0x%x (%d bytes), %d files\n",
            (uint32_t)(uintptr_t)fs_base, fs_size, n);
}

/* tar -C dir . 打包时路径带 "./" 前缀；剥掉它，也用来跳过目录项 */
static const char *strip(const char *n)
{
    while (n[0] == '.' && n[1] == '/')
        n += 2;
    return n;
}

void fs_ls(void)
{
    for (struct tar_header *h = (struct tar_header *)fs_base; h->name[0]; h = step(h)) {
        const char *name = strip(h->name);
        if (!*name)                     /* "./" 目录项：跳过 */
            continue;
        kprintf("  %-14s %d bytes\n", name, octal_to_uint(h->size, 12));
    }
}

int fs_read(const char *name, char *buf, uint32_t buflen)
{
    for (struct tar_header *h = (struct tar_header *)fs_base; h->name[0]; h = step(h)) {
        const char *hn = strip(h->name);
        if (!*hn || kstrcmp(hn, name) != 0)
            continue;

        uint32_t sz = octal_to_uint(h->size, 12);
        if (sz > buflen - 1)          /* 截断到调用者的缓冲区 */
            sz = buflen - 1;
        memcpy(buf, (uint8_t *)h + 512, sz);   /* 文件数据紧跟在 512 字节头后面 */
        buf[sz] = 0;
        return (int)sz;
    }
    return -1;
}
