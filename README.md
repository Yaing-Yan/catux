# CatUX

一个从零开始手写、迅速放弃并使用GLM-5.3-Flash继续完成的 Unix-like 内核，目标是最终做成一个可引导的发行版。
这也是一场“从只会 C 语法到写出操作系统”的教学式开发旅程——每一行
代码都配有讲解注释，路线图见 [ROADMAP.md](ROADMAP.md)。

## 当前进度

- ✅ 引导：Multiboot + 自建内核栈，C 世界入口
- ✅ 控制台：VGA 文本驱动（光标/滚动/颜色）+ 串口日志 + 迷你 printf
- ✅ 中断：IDT/异常处理、PIC 重映射、PIT 时钟心跳、PS/2 键盘
- ✅ 内存：multiboot 内存地图、物理页帧分配器（bitmap）、
      分页（虚拟内存）、内核堆 kmalloc/kfree（first-fit 空闲链表）

## 构建 & 运行

依赖：clang、GNU binutils、make、qemu（`qemu-system-i386`）。

```bash
make        # 编译内核 catux.elf
make run    # QEMU 启动：屏幕显示 VGA 输出，串口日志打进终端（Ctrl+C 退出）
```

## 路线图

物理内存 → 分页 → 堆 ✅ → 内核态迷你 shell → 文件系统 → 用户态与
系统调用 → fork/exec、管道 → 用户态 shell → 打包成 GRUB 引导的
发行版 ISO。远期：64 位、SMP、网络栈、TrueType 图形渲染
（MartianMono Nerd Font）。

详见 [ROADMAP.md](ROADMAP.md)，每阶段的原理笔记在 [docs/](docs/)。

## 许可

[MIT](LICENSE)
