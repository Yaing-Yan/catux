/*
 * string.c —— freestanding 字符串/内存库（阶段 3.5 从 main.c 搬出独立）
 *
 * ⚠️ 本项目用 -fno-builtin 编译不是白加的：如果不加，
 * memset 的循环会被编译器"优化"成对 memset 的调用——
 * 自己调自己，无限递归，栈当场爆炸。
 */

#include <stdint.h>

#include "string.h"

void *memset(void *dst, int val, uint32_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)val;
    return dst;
}

void *memcpy(void *dst, const void *src, uint32_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

uint32_t kstrlen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int kstrncmp(const char *a, const char *b, uint32_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (int)(uint8_t)*a - (int)(uint8_t)*b : 0;
}

void kstrcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++))
        ;
}
