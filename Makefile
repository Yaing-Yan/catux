# CatUX 构建脚本
#   make        编译内核 catux.elf
#   make run    在 QEMU 里启动（QEMU 窗口显示 VGA，串口输出打进当前终端）
#   make iso    打包成可引导光盘 catux.iso（需要 grub-mkrescue + xorriso + mtools）
#   make clean  清理所有构建产物
#
# 为什么用 clang：系统 gcc 的 cc1 无法启动（损坏），clang 内置 32 位代码生成，
# 不依赖任何多库运行时，非常适合做 freestanding 内核。

TARGET  := catux.elf
BUILD   := build
CC      := clang
LD      := ld

# -target i386-elf                产出 32 位 x86 目标文件
# -ffreestanding -nostdlib        独立环境：没有 libc，没有系统启动代码
# -fno-pic -fno-pie               内核固定加载，不做地址随机化
# -fno-stack-protector            没有 libc 提供的栈保护支持
# -fno-builtin                    别把我们的代码"优化"成对 libc 函数的调用
# -mno-mmx -mno-sse               内核初期不用 SIMD（浮点状态管理以后再说）
# -fno-asynchronous-unwind-tables 去掉给异常展开用的 .eh_frame，内核不需要
CFLAGS  := -target i386-elf -ffreestanding -fno-pic -fno-pie \
           -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
           -mno-mmx -mno-sse -Wall -Wextra -O2 -g
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

OBJS := $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/console.o $(BUILD)/idt.o \
        $(BUILD)/pic.o $(BUILD)/timer.o $(BUILD)/keyboard.o $(BUILD)/mem.o \
        $(BUILD)/paging.o $(BUILD)/heap.o $(BUILD)/fs.o $(BUILD)/task.o \
        $(BUILD)/string.o $(BUILD)/shell.o $(BUILD)/main.o

.PHONY: all run iso clean
all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

# QEMU 内置了一个 Multiboot 引导程序，能直接加载我们的 ELF——
# 开发期不用每次做光盘，等做发行版时再上 GRUB。
# -serial stdio：把串口 COM1 接到当前终端，内核日志直接打印出来。
# initrd：把 initrd/ 目录打包成 tar 当作内核的"初始磁盘"。
# 想加文件？往 initrd/ 里放，ls/cat 立刻能看见。
INITRD := initrd.tar
INITRD_SRC := $(wildcard initrd/*)

$(INITRD): $(INITRD_SRC)
	tar -cf $@ -C initrd .

run: $(TARGET) $(INITRD)
	qemu-system-i386 -kernel $(TARGET) -initrd $(INITRD) -serial stdio

iso: $(TARGET) $(INITRD)
	mkdir -p iso/boot/grub
	cp $(TARGET) iso/boot/
	cp grub.cfg iso/boot/grub/
	cp $(INITRD) iso/boot/
	grub-mkrescue -o catux.iso iso/

clean:
	rm -rf $(BUILD) $(TARGET) iso catux.iso $(INITRD)
