#pragma once
#include <stdint.h>

/* freestanding 环境自己的字符串/内存库（libc 的前身）。
 * 以后实现用户态 libc 时，这些函数会被复用。 */

void    *memset(void *dst, int val, uint32_t n);
void    *memcpy(void *dst, const void *src, uint32_t n);
uint32_t kstrlen(const char *s);
int      kstrcmp(const char *a, const char *b);
int      kstrncmp(const char *a, const char *b, uint32_t n);
void     kstrcpy(char *dst, const char *src);
