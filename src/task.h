#pragma once
#include <stdint.h>

#include "irq.h"

/* 阶段 4：任务（内核线程）与调度器
 *
 * 任务 = 一份保存的 CPU 现场 + 一个自己的内核栈。
 * 本阶段任务全部跑在 ring0、共享同一个地址空间（还没有用户态），
 * 这正是阶段 6 之前 "进程" 的雏形。 */

#define MAX_TASKS 4

void tasks_init(void);

/* 创建一个内核线程：给它一块新栈，栈上伪造一份"第一次被打断"
 * 的现场（eip 指向线程函数），调度器切到它时 iret 直接进入函数 */
void task_create(void (*entry)(void));

/* 时钟中断时被调用：保存当前现场，挑下一个任务。
 * 返回值：新任务的保存帧地址（当前任务自己栈上那一帧的地址，
 * 汇编桩拿到它后会 mov esp 过去再 popa+iretd）。 */
uint32_t schedule(struct regs *r);
