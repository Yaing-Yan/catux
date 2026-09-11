# CatUX 交接文档

> 面向下一个接手这个项目的人。目标：读完本文 + 跑通冒烟测试 ≈ 40 分钟，
> 之后你能独立改代码、定位 bug、继续推进路线图。
>
> 仓库：https://github.com/Yaing-Yan/catux （MIT）

---

## 0. 这是什么，现在在哪一步

一个从零手写的 x86 **32 位 Unix-like 内核**，目标是成长为一个可引导的发行版。
代码总量约 2600 行（含注释），中文注释密度很高，每个机制都写明了设计动机。

**已完成（v0.2.0）**：引导 → 控制台（VGA+串口）→ 中断/异常/时钟/键盘 →
内存管理（页帧/分页/堆）→ 内核态 shell → 多任务（内核线程+轮转调度）→
initrd 文件系统（ls/cat）→ **用户态 ring3 + 系统调用**。

**未完成**：真磁盘驱动、ELF 加载器、fork/exec/管道、多进程隔离、
运行真 bash、TrueType GUI（详见 §7）。

---

## 1. 快速上手

### 1.1 环境要求

| 工具 | 用途 | 备注 |
|---|---|---|
| `clang` | 编译内核与用户程序 | **不用 gcc**：本机系统 gcc 的 cc1 无法启动（见 §8.1） |
| `ld` / `objcopy` (binutils) | 链接、生成扁平二进制 | |
| `make` | 构建 | |
| `qemu-system-i386` (qemu-full) | 运行 | 只需 qemu-base 时无图形窗口，用 `-display none` 也能测串口 |
| `tar` | 打包 initrd | 系统自带 |
| `gdb`（可选） | 调试 | 配 `-s -S` 用 |

### 1.2 构建与运行

```bash
make                # 编译内核 → catux.elf（不含 initrd）
make initrd.tar     # 编译用户程序 hello.bin 并打包 initrd
make run            # 启动 QEMU：VGA 窗口 + 串口输出打到当前终端（Ctrl+C 退出）
make clean          # 清理
make iso            # 打 GRUB 光盘（需 xorriso + mtools，见 §7 备注）
```

`make run` 等价于：

```bash
qemu-system-i386 -kernel catux.elf -initrd initrd.tar -serial stdio
```

### 1.3 内核 shell 内置命令

`help` `echo` `uptime` `meminfo` `ls` `cat <file>` `ring3`（跑用户态程序）
`clear` `about` `reboot`

### 1.4 无图形/自动化测试（重要）

串口是双向的，但**键盘输入不走串口**（走 PS/2，见 §3.3）。自动化测试用
QEMU monitor 的 `sendkey` 模拟真实按键：

```bash
{ sleep 3; for k in l s ret; do printf "sendkey $k\n"; sleep 0.15; done; sleep 1; printf 'quit\n'; } \
  | timeout 15 qemu-system-i386 -kernel catux.elf -initrd initrd.tar \
      -serial file:/tmp/t.log -display none -monitor stdio >/dev/null 2>&1
cat /tmp/t.log
```

按键名：字母数字即本身，回车 `ret`，空格 `spc`，点 `dot`。

### 1.5 冒烟测试清单（改完代码必跑）

```bash
make clean && make && make initrd.tar
timeout 5 qemu-system-i386 -kernel catux.elf -initrd initrd.tar -serial stdio -display none
```

期望看到：内存地图 → `reserved ... for initrd module` → `CatUX v0.2.0` →
`self-test: heap [ok] frames[...]` → `catux>` 提示符，且每约 2 秒出现一个 `+`
（后台线程在跑）。再交互跑一次 `ls`、`cat cat.txt`、`ring3`（见 §1.4 脚本）。

---

## 2. 代码地图

```
catux/
├── Makefile              构建（含内核/用户程序/initrd 三套规则）
├── linker.ld             内核链接脚本（1MB 加载、段布局、_kernel_end）
├── grub.cfg              make iso 用
├── initrd/               打包进 initrd 的文本文件（welcome/cat/notes）
├── user/                 用户态程序（独立编译，跑在 ring3）
│   ├── hello.c           第一个用户程序：syscall 打印 + 故意越权被拒 + exit
│   └── link.ld           用户程序链接到 0x40000000
├── docs/                 各阶段原理笔记（01-boot.md、02-idt.md）
└── src/
    ├── boot.S            入口：Multiboot 头、栈、outb/inb/io_wait、kernel_stack_top
    ├── isr.S             中断桩：isr0-31（异常）、irq0-15（设备）、isr128（syscall）、enter_usermode
    ├── linker 相关       （见根目录 linker.ld）
    ├── console.c/h       输出子系统：emit() 双路镜像、VGA 驱动、串口、kprintf
    ├── io.h              端口 I/O 声明
    ├── string.c/h        freestanding 字符串/内存库（memset/kstrcmp/...）
    ├── gdt.c/h           自建 GDT + TSS（用户段、ring0 栈）
    ├── idt.c/h           IDT（256 门）、异常分发 isr_dispatch、IRQ 框架 irq_dispatch、irq_register
    ├── pic.c/h           8259 PIC 重映射到向量 32-47、EOI
    ├── timer.c/h         PIT 100Hz、timer_uptime()
    ├── keyboard.c/h      扫描码→ASCII、环形缓冲、kb_getchar()
    ├── mem.c/h           物理内存地图解析、bitmap 页帧分配器、bootloader 内存保留
    ├── paging.c/h        两级页表、按需建表、PAGE_USER、paging_map_page
    ├── heap.c/h          kmalloc/kfree（first-fit 空闲链表，堆在 0xC0000000）
    ├── fs.c/h            tar-as-FS over initrd：fs_ls / fs_read
    ├── task.c/h          内核线程、struct regs 现场、schedule()
    ├── syscall.c/h       int 0x80 分发、sys_write/sys_exit、user_run() ring3 启动器
    ├── shell.c/h         内核态 shell：行编辑、分词、命令表
    ├── multiboot.h       Multiboot 信息结构（含 mods）
    └── main.c            kmain：初始化顺序 + 自检 + shell_run
```

### 关键函数入口速查

| 想改什么 | 去哪 |
|---|---|
| 打印/颜色/光标 | `console.c`：`emit()` `kprintf()` `vga_set_color()` `console_clear()` |
| 加一条 shell 命令 | `shell.c`：写 `cmd_xxx`，加进 `cmds[]` 表 |
| 加一个中断处理 | `timer.c` 为样板：写 handler → `irq_register(irq, fn)` |
| 加一个异常处理 | `idt.c`：`isr_dispatch()` |
| 加一个系统调用 | `syscall.c`：`syscall_dispatch()` 的 switch + `syscall.h` 的编号；用户侧 `user/*.c` |
| 内存分配/释放 | `mem.c`（4KB 页帧）、`heap.c`（任意大小） |
| 映射虚拟地址 | `paging.c`：`paging_map_page(virt, phys, PAGE_RW|PAGE_USER)` |
| 文件读写 | `fs.c`：`fs_read()`；换后端时保持 `fs.h` 接口 |
| 任务切换 | `task.c`：`schedule()`；汇编侧 `isr.S` 的 `irq_common` |

---

## 3. 系统全貌

### 3.1 启动流程

```
BIOS → 引导程序（GRUB 或 QEMU -kernel 内置 Multiboot 引导器）
     → 找 Multiboot 头（boot.S 前 8KB）→ 加载内核到 1MB → 32位保护模式
     → 跳 _start（EAX=0x2BADB002, EBX=Multiboot 信息地址）
     → boot.S：设栈、把 EAX/EBX 压栈作 C 参数、call kmain
main.c kmain 的初始化顺序（顺序有依赖，别乱调）：
   console_init()   输出先行，后面才能看见日志
   gdt_init()       自建 GDT+TSS（用用户选择子之前必须完成）
   idt_init()       异常 + IRQ 门（选择子 0x08 依赖新 GDT 的布局）
   syscall_init()   0x80 门（DPL=3）+ TSS.esp0 指向专属内核栈
   pic_remap()      设备中断搬到 32-47
   timer_init()     100Hz 心跳
   keyboard_init()  键盘 → 环形缓冲
   mem_init(addr)   内存地图 + 页帧分配器（会保留 initrd 占用的帧！）
   paging_init()    恒等映射 0-64MB + VGA 别名，开 cr0.PG
   heap_init()      kmalloc 就绪
   fs_init(addr)    解析 initrd 的 tar
   tasks_init() / task_create(demo_thread)
   sti              开中断（漏了它系统"聋"，见 §5.2）
   shell_run()      永不返回
```

### 3.2 中断与异常（`isr.S` + `idt.c`）

- IDT 256 门：`0-31` CPU 异常 → `isr0..31`；`32-47` 设备中断 → `irq0..15`；
  `128 (0x80)` 系统调用 → `isr128`。
- **异常路径** `isr_common`：`call isr_dispatch(num, err)` → 打印 → `cli;hlt` 挂机
  （异常目前一律致命，没有恢复）。
- **IRQ 路径** `irq_common`：`pusha` → 调 `irq_dispatch(num, err, &regs)` →
  `mov esp, eax`（若调度器要求换任务）→ `popa` → `iretd`。
- `struct regs`（`irq.h`）**必须与栈布局严格对应**，顺序：
  `edi,esi,ebp,o_esp,ebx,edx,ecx,eax`（pusha 顺序）→ `int_no,err`（桩压）→
  `eip,cs,eflags`（CPU 压）。改 `isr.S` 的压栈顺序就必须同步改它。
- 时钟（num==32）是**唯一切换任务的点**：`irq_dispatch` 返回 `schedule(r)`。

### 3.3 键盘为什么能读到输入

PS/2 键盘走 IRQ1、端口 0x60——**与串口无关**。所以 `-serial stdio` 只能看输出，
要注入输入得用 monitor 的 `sendkey`（§1.4）。驱动把扫描码翻译成字符放进
32 字节环形缓冲，`kb_getchar()` 阻塞式读取（`hlt` 等中断）。

### 3.4 内存三层（自底向上）

| 层 | 文件 | 单位 | 说明 |
|---|---|---|---|
| 物理帧 | `mem.c` | 4KB | bitmap，`frame_base` 起（内核末尾对齐），`alloc_frame/free_frame` |
| 分页 | `paging.c` | 4KB 页 | 两级页表，**按需建表**（目录项空则 `alloc_frame` 一张表）；`PAGE_USER` 控权限 |
| 堆 | `heap.c` | 任意 | 虚拟地盘 `0xC0000000`，首次 `kmalloc` 时铺页；first-fit + 分裂/合并 |

⚠️ **恒等映射假设**：0-64MB 虚拟=物理。`paging_map_page` 建页表时按物理地址直接
访问新表、`heap_grow` 也直接摸帧——**帧分配一旦超过 64MB 就会静默出错**。
要支持更大内存，需要加"临时映射窗口"（在 `paging.c` 注释里也标了）。

### 3.5 任务与调度（`task.c`）

任务 = `struct regs` 现场 + 自己的内核栈（静态 4KB）。首次切入的现场是
**伪造**在栈顶的（`eip` 指向线程函数，`eflags=0x202`）。
`schedule()` 把当前现场存进任务结构（`esp` 指向栈上的帧），轮转挑下一个，
返回它的 `esp`；汇编桩 `mov esp, eax` 完成"灵魂切换"。
用户态任务的帧里 CPU 多压了 `ss/esp`，但因为整帧原地保存，`iretd` 自动处理。

### 3.6 文件系统（`fs.c`）

initrd = 宿主机 `tar` 打的归档，QEMU/GRUB 载入内存，Multiboot `mods[0]` 给地址。
内核把它当只读 FS：512 字节头 = 目录项，文件名 + **八进制 ASCII 大小**。
`fs_read`/`fs_ls` 是稳定接口，将来换 ATA+ext2 只改后端。

### 3.7 用户态与系统调用（`gdt.c` + `syscall.c` + `isr.S`）

- GDT：`0x08` 内核代码 / `0x10` 内核数据 / `0x18` 用户代码 / `0x20` 用户数据 / `0x28` TSS。
  **用户选择子要 |3（RPL）**：`0x1B`（cs）、`0x23`（ss/ds）。
- 进 ring3：`enter_usermode(entry, user_stack)` 手工搭 `ss,esp,eflags,cs,eip` + `iretd`。
- 出（ring3→ring0）：CPU 经 TSS 换到 `tss.esp0` 指定的**专属内核栈**
  （`syscall.c` 的 `syscall_stack`，8KB）。
- syscall ABI：`eax`=号，`ebx/ecx/edx`=参数，返回值写回 `r->eax`（`popa` 会恢复它）。
  现有：`0 exit(code)`、`1 write(fd, buf, len)`（fd 忽略；buf 做用户地址范围校验）。
- `user_run()`：从 initrd 读 `hello.bin` → 映射用户代码页/栈页（`PAGE_USER`）→
  保存内核现场（手写 setjmp，**含 callee-saved 寄存器**）→ `enter_usermode`。
- `sys_exit()`：打印 → 恢复内核现场（**含 `sti`**）→ longjmp 回 `user_run` 的 `resume:`。
  真实内核这里应销毁进程并调度下一个。

---

## 4. 虚拟地址空间地图

| 范围 | 用途 | 权限 |
|---|---|---|
| `0x00000000-0x04000000` | 恒等映射（内核代码/数据/栈/bitmap/页表/initrd） | 内核 |
| `0x000B8000` | VGA 文本显存（另在 `0x80000000` 有别名窗口） | 内核 |
| `0x08000000` | VGA 别名窗口 `0x80000000`（演示用，右上有绿 V） | 内核 |
| `0xC0000000-0xC1000000` | 内核堆（上限 16MB，页帧按需铺入） | 内核 |
| `0x40000000-0x40001000` | 用户程序代码（一页） | ring3 RW |
| `0x40001000-0x40002000` | 用户栈（栈顶 `0x40002000`） | ring3 RW |

内核所有页都**不带 `US`**，所以 ring3 碰任何内核地址都会页故障——这是隔离的根基。

---

## 5. 调试手册

### 5.1 工具

```bash
# 1) 串口日志（最常用）
qemu-system-i386 -kernel catux.elf -initrd initrd.tar -serial stdio -display none

# 2) 中断/异常现场（v=向量号，e=错误码，IP=故障地址）
qemu-system-i386 ... -d int -no-reboot 2>&1 | grep -A3 "v="

# 3) 检测三重故障重启（横幅重复出现 = 在重启循环里）
qemu-system-i386 ... -serial stdio -display none -no-reboot

# 4) 监视器：读物理内存 / 注入按键 / 看寄存器
printf 'xp /16xw 0x9500\n'        # Multiboot 信息
printf 'xp /40bc 0xb8000\n'       # VGA 显存
printf 'sendkey h\n'              # 注入按键

# 5) 反汇编（确认编译器/汇编器真的生成了你以为的指令）
objdump -d catux.elf | less
objdump -d catux.elf | grep -A20 "<irq_common>"
```

GDB（可选）：`qemu-system-i386 -kernel catux.elf -initrd initrd.tar -s -S`，
另开终端 `gdb catux.elf -ex 'target remote :1234'`。

### 5.2 已踩过的坑（症状 → 根因 → 修法）

| 症状 | 根因 | 修法 |
|---|---|---|
| 无输出、QEMU 活着 | GAS Intel 语法裸符号是**内存操作数**：`mov esp, stack_top` 变成读内存 | `mov esp, offset stack_top` |
| 中断返回即 GP（err=0x10） | 裸 `iret` 被汇编成 16 位 `iretw` | 写 `iretd` |
| 一切正常但系统"聋"、`hlt` 睡死 | 忘了 `sti` | 初始化清单最后一步 `sti` |
| initrd 读出垃圾地址（如 0x3） | 页表分配覆盖了引导程序放在内核后面的 initrd 数据**和 mods 数组** | `mem_init` 里 `frame_mark_used` 保留两块区域 |
| `cat` 找不到文件 | tar 存的路径带 `./` 前缀 | `fs.c` 的 `strip()` |
| longjmp 回内核后 Invalid Opcode（跳到 0xAB） | 手写 setjmp **没保存 ebp/ebx/esi/edi**；且没恢复 IF | 保存/恢复 callee-saved 并 `sti` |
| 用户程序跑完回内核，随后各种诡异崩溃 | syscall 用启动栈，中断帧从栈顶下长**踩坏 shell 活栈帧** | TSS.esp0 指向专属 8KB 内核栈 |
| 链接报 `undefined reference to memset` | freestanding + 编译器优化会生成 memset 调用 | `string.c` 提供；且必须 `-fno-builtin`，否则无限递归 |
| 汇编报 "BSS section cannot have non-zero bytes" | `.bss` 里放了 `.long` 初值 | 挪到 `.rodata`（见 `kernel_stack_top`） |
| `-m32` 编译失败 cc1 找不到 | 本机系统 gcc 损坏 | 全项目用 clang（`-target i386-elf`） |

### 5.3 加文件的三步（Makefile）

1. 新建 `src/foo.c` + `src/foo.h`；
2. `src/foo.c` 里 `#include` 需要的头（本项目的头都在 `src/`，直接 `"foo.h"`）；
3. 把 `$(BUILD)/foo.o` 加进 Makefile 的 `OBJS`（**最容易忘的一步**，症状是
   `undefined reference to foo_init`）。

---

## 6. 已知限制与未完成

**架构性**
- 单地址空间：用户程序与内核共用页目录，没有"每个进程一套页表"。
- 无 ELF 加载器：用户程序是固定地址的扁平二进制（`objcopy -O binary`）。
- 异常一律致命（打印后 `cli;hlt`），没有页故障按需分配、没有用户态 fault 回收。
- 单核假设：无锁、无原子操作、无 SMP 支持。
- 帧分配超 64MB 会出错（恒等映射假设，见 §3.4）。

**功能缺口**
- 磁盘：只有 initrd（内存），无 ATA/virtio 驱动。
- 进程：无 `fork/exec/wait`、无管道、无信号、无文件描述符表。
- 系统调用只有 `write/exit`（fd 参数被忽略）。
- 任务：无优先级、无 sleep/阻塞原语（`hlt` 是土办法）、任务不能退出（demo 线程死循环）。
- shell：无历史、无左右移动光标（`console_backspace` 只在行尾退格）、无引号/转义。
- 键盘：不支持 Shift/CapsLock（打不出大写和 `!@#$`），退格会显示怪字符。
- `kprintf`：无 `%u/%p/%l/%ld`、宽度只支持 `%s`。
- 无 RTC/日期、无关机（只有 `reboot`）。

**代码卫生**
- 源码里残留 `TODO 1..7` 标题注释，是教学阶段的编号；**实现早已完成**
  （`console.c` 光标/滚动/print_uint、`idt.c` 分发器、`mem.c` 帧分配器），
  接手后可把标题改成"实现要点"以免误解。

---

## 7. 下一步路线

按依赖顺序，建议这么走：

1. **ELF 加载器**：解析 initrd 里的 ELF32（`fs_read` 拿字节 → 校验 `\x7fELF`
   → 按 program header 映射段到用户地址空间）。之后用户程序不用固定 `0x40000000`。
2. **进程模型**：每进程一套页目录（现在直接换 `cr3`）+ 内核栈 +
   `fork`（复制页目录/写时复制可先不做，直接复制页）、`exec`（换程序镜像）、
   `wait`。需要给 `struct task` 扩字段（pid/页目录/状态）。
3. **系统调用扩容**：`read`（键盘/串口输入）、`open/close`、`brk`（用户堆）、
   `getpid`、`sleep`（配合定时器阻塞任务）。
4. **文件描述符表 + 管道**（`pipe` 需要把读写端接到环形缓冲）。
5. **极简 libc**（用户侧）：`crt0`（`_start` → 调 `main` → `exit`）、
   `printf`（我们已有 `kprintf` 的逻辑可移植）、`malloc`（`brk` 之上）、
   `open/read/write/close` 封装。
6. **真 bash**：以上齐了之后，尝试静态链接的 bash；需要补齐它用到的
   `termios`/`signal`/`ioctl(TIOCGWINSZ)` 等。这是长期的硬仗。
7. **ATA 磁盘驱动 + ext2-lite**：把 `fs.c` 的后端从内存换成磁盘；
   `fs.h` 接口保持不动，shell 无感。
8. **GUI 里程碑（TrueType）**：VBE 图形模式 → 线性帧缓冲 →
   移植 `stb_truetype.h` 光栅化 → 渲染 **MartianMono Nerd Font**
   （SIL OFL 许可，可自由捆绑发行）。这也是显示中文的唯一途径
   （VGA 文本模式只有 256 字形，中文必然是乱码）。

---

## 8. 开发约定与踩坑规则

### 8.1 工具链

- **用 clang 不用 gcc**：本机 gcc 的 `cc1` 无法启动（`cannot execute 'cc1'`），
  该项目自始至终用 `clang -target i386-elf`。不要"顺手改回 gcc"。
- 内核 CFLAGS 关键项（改前先懂）：
  `-ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin
   -fno-asynchronous-unwind-tables -mno-mmx -mno-sse`。
  `-fno-builtin` 是给 `string.c` 保命的（否则 `memset` 自己调自己）。
- 汇编是 **GAS Intel 语法**（`.intel_syntax noprefix`）。雷区：
  取符号地址要 `offset`；32 位返回必须 `iretd`。见 §5.2。

### 8.2 写内核代码的习惯

- 不能调用 libc；要的东西自己写（`string.c`）。
- 所有输出走 `emit()`（VGA+串口自动镜像），调试信息别忘换行。
- 中断上下文里不要做耗时/可能阻塞的事；共享数据想清楚单核下的重入
  （现在靠"中断门自动关 IF"避免嵌套）。
- 分配内存必须检查返回 0；`kfree(NULL)` 安全，双重释放会被静默拒绝。
- 新增全局缓冲区注意大小（现在是静态分配，吃 `.bss`）。

### 8.3 提交习惯

- 每个阶段一个 commit，标题格式：`CatUX vX.Y.Z: <一句话> (phase N)`。
- 提交前跑 §1.5 冒烟测试，确保零警告（`make 2>&1 | grep -E "error|warning"` 无输出）。
- 远端是 https，**网络需要走代理**（已写入 `.git/config`）：
  `http.proxy = socks5://127.0.0.1:1080`。若换环境，改这一项或删除即可。

---

## 9. 参考资料

- 项目内：`docs/01-boot.md`（引导与 Multiboot）、`docs/02-idt.md`（中断/异常）、
  `ROADMAP.md`（阶段规划与勾选状态）。
- 外部：OSDev Wiki（Multiboot、Paging、Interrupts、Bare Bones）；
  《Operating Systems: Three Easy Pieces》（进程/内存概念）；
  Intel SDM Vol.3（权威但大部头，查段/门/页表格式时用）。
