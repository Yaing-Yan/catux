# 第 1 课：从按下电源到 "Hello from bare metal"

> 本文是第 1 步（能启动的内核）的完整讲解，配合源码阅读。

## 1. 开机时到底发生了什么

```
按下电源
  │  CPU 复位：16 位实模式，只能看到 1MB 内存，没有任何保护
  ▼
BIOS（主板固件）
  │  自检硬件，然后从启动盘最前面读 512 字节（MBR）执行
  ▼
引导程序（真机=GRUB；QEMU -kernel 时=QEMU 内置的 Multiboot 引导器）
  │  1. 在内核文件前 8KB 里找 Multiboot 头（boot.S 里那三个数）
  │  2. 校验通过 → 把整个内核 ELF 加载到物理地址 1MB 处
  │  3. 把 CPU 切到 32 位保护模式（能寻址 4GB，有段/页/特权级）
  │  4. 跳到内核入口 _start，并留下两个"见面礼"：
  │       EAX = 0x2BADB002  （魔数：证明"我是 Multiboot 引导程序"）
  │       EBX = 引导信息结构体地址（内存大小、启动设备……阶段 3 用）
  ▼
我们的内核（src/boot.S 的 _start）
  │  搭好栈 → 把 EAX/EBX 按照 C 调用约定压栈
  ▼
kmain()（src/main.c）—— C 语言世界开始
```

两个魔数别记反：
- `0x1BADB002` 写在**内核文件里**，是内核的自我介绍："我是 Multiboot 内核"
- `0x2BADB002` 由**引导程序放进 EAX**，是它的回执："我按规范加载了你"

## 2. 为什么入口必须是汇编（boot.S）

C 函数调用的一切都依赖栈：局部变量、参数、返回地址全在栈上。
`call kmain` 这条指令本身就要用栈（压返回地址）。
而引导程序把控制权交给我们时，esp 指向哪是未定义的——所以第一件事是
自己划一块内存当栈（.bss 段里 16KB），把 esp 指过去，才能 call 进 C。

boot.S 三块内容：
1. **Multiboot 头**（`.multiboot` 段）：魔数/标志/校验和三个 4 字节，
   校验和 = -(魔数+标志)，保证三者之和为 0 —— 引导程序靠这个识别我们。
2. **栈**（`.bss` 段）：`stack_bottom` 到 `stack_top` 16KB。
3. **入口代码**：设 esp → push ebx（第 2 参数）→ push eax（第 1 参数）
   → call kmain。C 参数压栈顺序是从右到左（cdecl 约定）。

⚠️ 本课踩过的坑：GAS 的 Intel 语法里 `mov esp, stack_top` 是
"读 stack_top 那块**内存的内容**给 esp"，取地址必须写 `mov esp, offset stack_top`。
写错不会报任何编译错误，只会在运行时悄悄三重故障。

## 3. 链接脚本（linker.ld）——内核的建筑图纸

普通程序的链接脚本由操作系统提供；我们自己是操作系统，所以自己定：

- `. = 1M;` 内核加载到物理地址 1MB。低 1MB 是 legacy 区：
  中断向量表、BIOS 数据、VGA 显存（0xB8000）等都住在那里。
- Multiboot 头用 `KEEP(*(.multiboot))` 放在最前面——引导程序只搜前 8KB，
  而且这个段没人"引用"，不 KEEP 会被链接器当垃圾删掉。
- `.text/.rodata/.data/.bss` 各段 4KB 对齐——分页的基本单位就是 4KB。
- `ENTRY(_start)` 告诉链接器：程序从 _start 开始。

生成的 catux.elf 是 ELF32 文件：真正的"可执行格式"，
里面除了代码还带程序头表（描述"把我加载到内存哪个地址"）——引导程序照着它办。

## 4. main.c——freestanding 的世界

编译参数 `-ffreestanding -nostdlib` 意味着：没有 libc、没有 crt0、
连"入口叫 main"的约定都没有。printf、malloc、exit 全不存在。
所以我们：

- **VGA 文本模式**：显存映射在物理地址 0xB8000，
  80 列 × 25 行，每个字符占 2 字节 = `[ASCII, 颜色]`。
  颜色字节：高 4 位背景 + 低 4 位前景，0x0F = 黑底白字。
  颜色编号：0黑 1蓝 2绿 3青 4红 5品 6棕 7浅灰，8~15 是它们的亮色版。
  往这块内存写字，屏幕立刻变——这就是最原始的显卡驱动。
- **串口 COM1**：x86 除了内存还有一条"端口"地址空间，
  `out`/`in` 指令读写设备寄存器。串口寄存器排在 0x3F8 起的一排端口上：
  +0 数据、+1 中断使能、+3 线路控制（DLAB 波特率开关）、+5 状态。
  发送前先读 +5 状态位忙等（轮询），这是没有中断时唯一的同步方式。
  QEMU 用 `-serial stdio` 把 COM1 接到终端——我们今后的主调试通道。
- **memset/memcpy 必须自己提供**：编译器开优化后可能把你的循环
  悄悄优化成对 memset 的调用，而 freestanding 环境没人提供它，
  不写就会链接报错——osdev 经典坑。

## 5. 构建链条

```
src/boot.S ─clang→ build/boot.o ─┐
                                 ├─ld -T linker.ld→ catux.elf ─qemu→ 屏幕+串口
src/main.c ─clang→ build/main.o ─┘
```

clang 关键参数：
| 参数 | 作用 |
|---|---|
| -target i386-elf | 产出 32 位 x86 代码 |
| -ffreestanding | 独立环境，别假设有 libc |
| -nostdlib | 链接时不带任何系统库 |
| -fno-pic -fno-pie | 固定地址加载，不要位置无关 |
| -fno-stack-protector | 没有库提供栈保护 |
| -mno-mmx -mno-sse | 内核初期不碰 SIMD |

QEMU 能 `-kernel catux.elf` 直接启动，是因为它内置了一个符合
Multiboot 规范的引导程序。开发期用它，做发行版时再换 GRUB（`make iso`，
需要 `sudo pacman -S --needed xorriso mtools`）。

## 6. 值得记住的数字

| 数字 | 含义 |
|---|---|
| 0x1BADB002 | 内核放在文件里的 Multiboot 魔数 |
| 0x2BADB002 | 引导程序放进 EAX 的回执魔数 |
| 1M | 内核加载的物理地址 |
| 0xB8000 | VGA 文本显存 |
| 0x3F8 | 串口 COM1 基端口 |

## 7. 常用命令

```bash
make        # 编译
make run    # QEMU 启动（VGA 窗口 + 串口打到终端），Ctrl+C 退出
make iso    # 打 GRUB 光盘（需先装 xorriso mtools）
make clean  # 清理
objdump -d catux.elf      # 反汇编——没有报错弹窗时的"眼睛"
```
