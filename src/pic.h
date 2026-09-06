#pragma once

/* 8259 PIC 可编程中断控制器
 * remap：把 IRQ0~15 从默认向量 8~15 搬到 32~47（避开 CPU 异常区）
 * send_eoi：处理完中断后向 PIC 应答（不发 EOI，PIC 就永远沉默） */
void pic_remap(void);
void pic_send_eoi(int irq);
