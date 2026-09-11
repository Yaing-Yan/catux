#pragma once
#include <stdint.h>

/* 阶段 6：系统调用
 * 约定（我们自己的 ABI，仿 Linux 的 int 0x80）：
 *   eax = 调用号，ebx/ecx/edx = 参数，返回值放回 eax
 *   0 = exit(code)
 *   1 = write(fd, buf, len)   —— fd 暂时忽略，全部写到控制台 */

void syscall_init(void);   /* 注册 0x80 号中断门（DPL=3） */

/* 加载 initrd 里的用户程序并跳进 ring3（shell 的 ring3 命令调用它） */
void user_run(void);
