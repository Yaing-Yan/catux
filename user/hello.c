/*
 * hello.c —— CatUX 的第一个用户态程序（阶段 6）
 *
 * 这个文件编译成独立二进制，装在 initrd 里，由内核加载到
 * 虚拟地址 0x40000000，然后 iret 进 ring3 执行。
 *
 * ring3 意味着：
 *   - 不能执行特权指令（cli/hlt/out… 一碰就 General Protection）
 *   - 页表不带 US 位的内存（整个内核）对它不可见
 *   - 想输出、想退出，只能 int 0x80 请内核代劳
 */

#define SYS_EXIT  0
#define SYS_WRITE 1

/* eax=调用号, ebx/ecx/edx=参数, 返回值在 eax */
static inline int syscall3(int num, int a, int b, int c)
{
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c));
    return ret;
}

static int strlen_(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void _start(void)
{
    const char *msg = "hello from ring3! I have no hardware privileges.\n";
    syscall3(SYS_WRITE, 0, (int)msg, strlen_(msg));

    /* 故意犯规：请内核把内核地址 0x100000 的内容打印出来。
     * 内核会检查指针范围并拒绝——这就是"用户态隔离"的日常 */
    syscall3(SYS_WRITE, 0, 0x100000, 16);

    const char *bye = "asking kernel to exit(42) now...\n";
    syscall3(SYS_WRITE, 0, (int)bye, strlen_(bye));

    syscall3(SYS_EXIT, 42, 0, 0);

    for (;;)          /* exit 不该返回；真回来了就转圈 */
        ;
}
