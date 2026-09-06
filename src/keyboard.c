/*
 * keyboard.c —— PS/2 键盘驱动（阶段 3.5：改为环形缓冲模型）
 *
 * 旧版：中断里直接回显 —— 简单，但消费者没有任何选择权。
 * 新版：中断（生产者）把字符压进环形缓冲区，shell（消费者）
 *       想什么时候读就什么时候读。这是"中断驱动 I/O"的标准形态，
 *       以后的磁盘块缓冲、网络包队列全是这个模型的变体。
 *
 * 环形缓冲：一个定长数组 + 头尾两个下标，头进尾出，
 * 走到数组末尾就绕回开头（所以叫"环形"）。
 * 本例只有一个生产者一个消费者，且都是单字节操作，
 * volatile 足够，不需要锁——多核/多生产者时要另加同步。
 */

#include <stdint.h>

#include "io.h"
#include "irq.h"
#include "keyboard.h"

#define KB_BUF_SIZE 32

static volatile char kbuf[KB_BUF_SIZE];
static volatile uint8_t khead;   /* 生产者写这里 */
static volatile uint8_t ktail;   /* 消费者读这里 */

/* Scancode Set 1 → ASCII。0 = 不产生字符（控制键） */
static const char scancode_to_char[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*', 0, ' ', 0,
    /* 58 之后是 CapsLock、F1~F12、方向键等，暂不处理（自动补 0） */
};

static void keyboard_handler(void)
{
    uint8_t sc = inb(0x60);          /* 读扫描码（读了就是确认） */
    if (sc & 0x80)                   /* 松键 */
        return;

    char c = scancode_to_char[sc];
    if (!c)
        return;

    uint8_t next = (uint8_t)((khead + 1) % KB_BUF_SIZE);
    if (next == ktail)               /* 缓冲区满：只能丢（键盘不缓冲会丢更多） */
        return;
    kbuf[khead] = c;
    khead = next;
}

char kb_getchar(void)
{
    for (;;) {
        if (ktail != khead) {
            char c = kbuf[ktail];
            ktail = (uint8_t)((ktail + 1) % KB_BUF_SIZE);
            return c;
        }
        __asm__ volatile("hlt");     /* 没键可读：睡觉，中断来了自动醒 */
    }
}

void keyboard_init(void)
{
    irq_register(1, keyboard_handler);
}
