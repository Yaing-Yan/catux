/*
 * keyboard.c —— PS/2 键盘驱动（阶段 2b）
 *
 * 每按、每松一个键，键盘都往端口 0x60 送一个字节："扫描码"。
 *   按下 = 该键的扫描码；松开 = 扫描码 + 0x80（最高位置 1）。
 * 扫描码是"键的位置"不是"字符"，所以需要一张表翻译成 ASCII
 * （Scancode Set 1，美式布局）。
 *
 * 注意：现在没有缓冲区——中断里直接回显。等以后有了进程，
 * 这里会改成"把码放进队列，由专门的进程慢慢读"。
 */

#include <stdint.h>

#include "io.h"
#include "console.h"
#include "irq.h"
#include "keyboard.h"

/* Scancode Set 1 → ASCII。0 = 不产生字符（控制键）。
 * 下标就是扫描码：0=无 1=ESC 2~13=数字行 14=退格 15=Tab ... */
static const char scancode_to_char[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*', 0, ' ', 0,
    /* 58 之后是 CapsLock、F1~F12、方向键等，暂不处理（自动补 0） */
};

/* ==================== TODO 6：键盘处理 ====================
 * 每次按键，IRQ1 都会触发本函数（注意：它跑在"中断上下文"里）。
 * 步骤：
 *   1. uint8_t sc = inb(0x60);   读扫描码（读了就是确认了，不用应答）
 *   2. if (sc & 0x80) return;    最高位=1 是"松键"，不管
 *   3. 查表得字符；字符非 0 就 kprintf("%c", c) 回显到屏幕
 * 验收：make run 后敲键盘，屏幕出现字母数字！ */
static void keyboard_handler(void)
{
    /* TODO 6 */
    uint8_t sc = inb(0x60);
    if (sc & 0x80) return;
    if (sc != 0) {
        kprintf("%c",scancode_to_char[sc]);
    }
}

void keyboard_init(void)
{
    irq_register(1, keyboard_handler);
}
