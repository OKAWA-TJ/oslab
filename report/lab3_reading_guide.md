# Lab3 代码阅读指导（逐文件讲解）

> 面向：正在从 lab2 过渡到 lab3、需要把「中断 / Trap」这条链路读通的同学。
> 对照：指导书 lab3 章「项目组成和执行流」「riscv64 中断介绍」「掉进兔子洞（中断入口点）」「中断处理程序」「滴答滴答（时钟中断）」「断亦有断（中断的关闭和开启）」。
> 本仓库实际代码为 lab3 骨架 + lab2 物理内存管理，文中行号以当前仓库为准。

---

## 0. 先建立正确的心智模型

Lab3 只有一件事：**让内核学会「被打断 → 保存现场 → 用 C 处理 → 恢复现场 → 接着跑」**。

指导书把这件事拆成三条主线：

1. **Trap 上下文保存与恢复** —— `kern/trap/trapentry.S`
2. **Trap 入口与返回** —— `kern/trap/trapentry.S` + `trap.h` + `trap()`
3. **中断与异常处理** —— `kern/trap/trap.c`

先记住两个术语：

- RISC-V 里 **Trap = 中断（Interrupt）+ 异常（Exception）**。
  - 中断：异步，外部触发（如时钟）。
  - 异常：同步，当前指令触发（如非法指令、断点）。
- 本实验**不涉及用户态**，所有 Trap 都是「S 态陷入 S 态」（自陷）。所以 `sscratch` 那套「用户栈/内核栈切换」暂时不是重点，但代码里仍会看到它。

建议**读两遍**：

- 第一遍：只追执行流，不抠汇编细节，能在纸上画出闭环。
- 第二遍：逐文件精读，重点啃 `trapentry.S`。

---

## 1. 全局地图：文件职责

| 文件 | 角色 | 和 lab3 的关系 |
|---|---|---|
| `kern/init/entry.S` | 汇编入口，建页表、设栈、跳 `kern_init` | 所有 C 代码的起点 |
| `kern/init/init.c` | `kern_init()`，编排初始化顺序 | **主线**：调用 `idt_init`/`clock_init`/`intr_enable` |
| `libs/riscv.h` | CSR 读写宏、cause/中断号/特权位定义 | 与硬件对话的「词汇表」 |
| `libs/sbi.c/.h` | 通过 `ecall` 请求 M 态 OpenSBI | 设定时器、关机、串口输出 |
| `kern/driver/clock.c/.h` | 读时间、设下一次时钟事件 | 时钟中断的「上弦」 |
| `kern/driver/intr.c/.h` | 开/关全局中断（`sstatus.SIE`） | 中断总开关 |
| `kern/sync/sync.h` | `local_intr_save/restore` 临界区 | 保护内存管理数据结构（lab2 的 `pmm.c` 用到） |
| `libs/atomic.h` | 原子位操作 `set_bit/test_bit/...` | lab3 新增，配合 `sync.h` |
| `kern/trap/trap.h` | `struct pushregs` / `struct trapframe` | **Trap frame 的内存契约** |
| `kern/trap/trapentry.S` | `SAVE_ALL` / `RESTORE_ALL` / `__alltraps` / `__trapret` | **上下文保存与恢复** |
| `kern/trap/trap.c` | `idt_init` / `trap` / `trap_dispatch` / 两个 handler | **分发与处理** |
| `kern/mm/pmm.c` | lab2 的物理内存管理 | lab3 里被套上了关中断保护，是「为什么要关中断」的活例子 |

### 总执行流（务必先背下来）

```
加载/启动
  └─ kern_entry (entry.S)
       └─ kern_init (init.c)
            ├─ dtb_init / cons_init / print_kerninfo
            ├─ idt_init()          ← stvec = &__alltraps
            ├─ pmm_init()          ← lab2 的物理内存
            ├─ idt_init()          ← 再次确认 stvec
            ├─ clock_init()        ← sie 使能时钟 + 设第一个时钟事件
            └─ intr_enable()       ← sstatus.SIE = 1
                 └─ while(1)       ← 坐等时钟中断

时钟中断发生
  └─ 硬件跳到 stvec = __alltraps (trapentry.S)
       ├─ SAVE_ALL              ← 构造 trapframe
       ├─ (TODO) jal trap       ← 把 sp 作为 tf 传给 trap()
       │    └─ trap() → trap_dispatch()
       │         ├─ cause<0 → interrupt_handler()  ← 时钟/软件中断
       │         └─ cause≥0 → exception_handler()  ← 非法指令/断点
       ├─ __trapret
       ├─ RESTORE_ALL           ← 从 trapframe 恢复
       └─ sret                  ← 回到被打断处
```

---

## 2. 阅读路线（按依赖顺序，建议照此进行）

1. `libs/riscv.h` —— 先认识寄存器和位。
2. `kern/init/init.c` + `kern/init/entry.S` —— 知道从哪进、按什么顺序初始化。
3. `kern/trap/trap.c` 的 `idt_init` —— 知道中断入口被设成谁。
4. `kern/driver/intr.c`、`kern/driver/clock.c`、`libs/sbi.c` —— 知道中断如何被使能、时钟如何「上弦」。
5. `kern/trap/trap.h` —— 看懂 trapframe 布局（**这是理解汇编的钥匙**）。
6. `kern/trap/trapentry.S` —— 上下文保存/恢复与入口。
7. `kern/trap/trap.c` 的 `trap`/`trap_dispatch`/两个 handler —— 分发与处理。
8. `kern/sync/sync.h` + `kern/mm/pmm.c` —— 为什么有些操作要关中断。

---

## 3. 逐文件精读

### 3.1 `kern/init/entry.S` —— 进入 C 世界之前

只关心和 lab3 相关的几件事：

- 保存 OpenSBI 传进来的 `a0`(hartid)、`a1`(dtb 地址) 到 `boot_hartid`、`boot_dtb`。
- 用 `boot_page_table_sv39` 建立 Sv39 页表并写入 `satp`，`sfence.vma` 刷新 TLB。
- 把 `sp` 设为 `bootstacktop`，然后 `jr kern_init`。

**读它的意义**：`kern_init` 运行在虚拟地址空间、栈已就绪。后面 `print_kerninfo` 打印的 `0xffffffffc020xxxx` 这类虚拟地址就来自这里的映射。

> 本实验关注点在 Trap，`entry.S` 不要求改，读懂「它在调 `kern_init` 前已经准备好页表和栈」即可。

---

### 3.2 `kern/init/init.c` —— 主线的编排者

核心函数（当前仓库约 16 行起）：

```c
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);   // 清 BSS
    dtb_init();                      // 解析设备树，拿物理内存信息（lab3 骨架比指导书多这一步）
    cons_init();                     // 串口初始化
    const char *message = "(THU.CST) os is loading ...\0";
    cputs(message);
    print_kerninfo();                // 打印内核符号

    idt_init();                      // 设置中断入口 stvec
    pmm_init();                      // lab2：物理内存管理
    idt_init();                      // 再次设置（框架保留）
    clock_init();                    // 使能时钟中断 + 设第一个时钟事件
    intr_enable();                   // 打开 S 态全局中断

    while (1) ;                      // 之后靠中断驱动
}
```

逐句要点：

- `memset(edata, 0, end - edata)`：清 `.bss`。注意它必须在 `dtb_init()` **之前**，否则会把 `dtb_init` 已解析的内存信息清掉（骨架的注释也在强调这点）。
- `idt_init()` **出现了两次**：这是框架保留的写法。第一次在 `pmm_init` 前、第二次在开中断前。**关键是保证 `intr_enable()` 之前 `stvec` 已经指向 `__alltraps`**。
- 顺序很重要：`clock_init()` 先「上好弦」（`sie` 使能 + 设定第一个事件），`intr_enable()` 最后「打开总闸」(`sstatus.SIE`)。这样不会在还没准备好时就被中断打断。
- `while (1);` 不是死机，而是「主线程无事可做，等中断」。

---

### 3.3 `libs/riscv.h` —— 与硬件对话的词汇表

#### (1) CSR 读写宏（约 187~216 行）

```c
read_csr(reg)        → csrr   rd, reg        // 读
write_csr(reg, val)  → csrrw  rd, reg, val   // 写
set_csr(reg, bit)    → csrrs  rd, reg, bit   // 置位
clear_csr(reg, bit)  → csrrc  rd, reg, bit   // 清位
```

这些是「读-改-写」：`csrrs/csrrc` 会返回旧值，所以可用于原子地置位/清位。

#### (2) lab3 用到的位与编号

| 符号 | 值 | 含义 |
|---|---|---|
| `SSTATUS_SIE` | `0x2`（bit1） | S 态全局中断使能 |
| `MIP_STIP` | `1<<5` | S 态定时器中断挂起位（写 `sie` 用） |
| `IRQ_S_SOFT` | `1` | S 态软件中断 |
| `IRQ_S_TIMER` | `5` | S 态定时器中断 |
| `CAUSE_ILLEGAL_INSTRUCTION` | `0x2` | 非法指令异常 |
| `CAUSE_BREAKPOINT` | `0x3` | 断点异常 |
| `SSTATUS_SPP` | `0x100` | 陷入前特权级（1=S，0=U） |

#### (3) CSR 编号

```c
CSR_SSTATUS 0x100   CSR_SIE 0x104    CSR_STVEC 0x105
CSR_SSCRATCH 0x140  CSR_SEPC 0x141   CSR_SCAUSE 0x142
CSR_SBADADDR 0x143  CSR_SIP 0x144    CSR_SATP 0x180
```

> 注意：`sbadaddr` 是旧名，新工具链把它叫 `stval`（同一个 CSR 0x143）。当前仓库的 `trapentry.S` 已改为 `stval`，否则汇编会报 `unknown CSR 'sbadaddr'`。

---

### 3.4 `libs/sbi.c/.h` —— 通过 ecall 请求 M 态固件

RISC-V 的定时器、关机等由 **M 态 OpenSBI** 管理。S 态内核要用 `ecall` 主动陷入 M 态请求服务：

```c
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    // 调用约定：a7(x17)=服务号，a0~a2=参数，ecall 陷入 M 态，返回值在 a0
    asm volatile(
        "mv x17, %[sbi_type]\n"
        "mv x10, %[arg0]\n"
        "mv x11, %[arg1]\n"
        "mv x12, %[arg2]\n"
        "ecall\n"
        "mv %[ret_val], x10"
        ...
    );
}
```

- `SBI_SET_TIMER = 0`：`sbi_set_timer(stime)` —— 当 `time` 到达 `stime` 时触发一次时钟中断。
- `SBI_SHUTDOWN = 8`：`sbi_shutdown()` —— 关机（处理函数里达到次数后可能用到）。
- `SBI_CONSOLE_PUTCHAR = 1`：串口输出，`cprintf` 底层最终也走到这。

> **理解要点**：`set_csr(sie, MIP_STIP)` 只是「允许时钟中断」，真正「让定时器在某个时刻响」要靠 `sbi_set_timer()`。

---

### 3.5 `kern/driver/clock.c/.h` —— 时钟中断的「上弦」

```c
volatile size_t ticks;                 // 时钟中断计数；volatile 防止被优化掉

static inline uint64_t get_cycles(void) {
#if __riscv_xlen == 64
    uint64_t n;
    asm volatile("rdtime %0" : "=r"(n));   // 读 time CSR：开机以来的时间
    return n;
#else
    // 32 位：rdtimeh/rdtime 两次读取并处理进位，拼成 64 位
#endif
}

static uint64_t timebase = 100000;     // QEMU 10MHz → 100000 个 tick = 10ms

void clock_init(void) {
    set_csr(sie, MIP_STIP);            // 1) 允许 S 态定时器中断
    clock_set_next_event();            // 2) 设定第一次时钟事件
    ticks = 0;                         // 3) 计数器归零
}

void clock_set_next_event(void) {
    sbi_set_timer(get_cycles() + timebase);   // 现在的时刻 + timebase 触发一次
}
```

要点：

- **一次只设一个事件**：OpenSBI 的 `sbi_set_timer` 只设置「下一个」时刻，所以必须在**每次时钟中断处理时再调用一次** `clock_set_next_event()`，否则只会响一次。
- `timebase = 100000`：QEMU 频率 10MHz，10ms 一次 → 每秒约 100 次（对应 `TICK_NUM = 100`）。
- `ticks` 是**处理函数里累加**的；中断是否真的持续，就看 `ticks` 有没有涨。
- `clock.h` 只暴露 `ticks`、`clock_init`、`clock_set_next_event`。

---

### 3.6 `kern/driver/intr.c/.h` —— 中断总开关

```c
void intr_enable(void)  { set_csr(sstatus, SSTATUS_SIE);   }
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

区分两个层级：

| 寄存器 | 作用 | 类比 |
|---|---|---|
| `sstatus.SIE` | **全局** S 态中断开关 | 总闸 |
| `sie` | **按类型**使能（定时器/软件/外部） | 各支路开关 |

两者都打开，时钟中断才会进来。`intr_enable()` 是「总闸」，`clock_init()` 里的 `set_csr(sie, MIP_STIP)` 是「定时器支路」。

---

### 3.7 `kern/sync/sync.h` + `libs/atomic.h` —— 临界区

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {  // 原来开着中断吗？
        intr_disable();                     // 关掉
        return 1;                           // 记住「原来开着」
    }
    return 0;                               // 原来就关着，无需恢复
}
static inline void __intr_restore(bool flag) {
    if (flag) intr_enable();                // 只有原来开着，才重新打开
}

#define local_intr_save(x)    do { x = __intr_save(); } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

- 这是经典的「保存并恢复中断状态」临界区：**不能无脑 `intr_enable()`**，否则会把调用者原本关闭的中断强行打开。
- `do { ... } while (0)` 让宏在 `if/else` 里作为单条语句安全展开。
- 在 `kern/mm/pmm.c` 里，`alloc_pages/free_pages/nr_free_pages` 都包了这对宏，避免分配器修改空闲链表时被时钟中断打断。这就是指导书「断亦有断」一节的内容。

---

### 3.8 `kern/trap/trap.h` —— Trap frame 的「内存契约」

```c
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp, t0, t1, t2, s0, s1,
              a0, a1, a2, a3, a4, a5, a6, a7,
              s2, s3, s4, s5, s6, s7, s8, s9, s10, s11,
              t3, t4, t5, t6;          // 共 32 个 = x0..x31
};
struct trapframe {
    struct pushregs gpr;   // 32 槽
    uintptr_t status;      // sstatus  —— 槽 32
    uintptr_t epc;         // sepc     —— 槽 33
    uintptr_t badvaddr;    // stval    —— 槽 34
    uintptr_t cause;       // scause   —— 槽 35
};
```

**关键对应关系**（务必背下）：

```
结构体字段顺序  ==  寄存器编号  ==  栈槽号
zero=0, ra=1, sp=2, gp=3, tp=4, t0=5 ... t6=31
status=32, epc=33, badvaddr=34, cause=35
```

总大小 `36 * 8 = 288` 字节。`trapentry.S` 里的 `N*REGBYTES(sp)` 中的 `N` 就是这个槽号。这是汇编与 C 之间的唯一契约，**任何一方改布局，另一方必须同步改**。

---

### 3.9 `kern/trap/trapentry.S` —— 上下文切换的机器码

#### (1) `SAVE_ALL`：把 CPU 状态压成一个 `trapframe`

```asm
.macro SAVE_ALL
csrw sscratch, sp                 # ① 旧 sp 暂存到 sscratch
addi sp, sp, -36 * REGBYTES       # ② 栈顶下移 288 字节
STORE x0, 0*REGBYTES(sp)          # ③ 保存 x0
STORE x1, 1*REGBYTES(sp)          #    x1
STORE x3, 3*REGBYTES(sp)          #    x3 ... 注意跳过 x2(sp)
...
STORE x31, 31*REGBYTES(sp)
csrrw s0, sscratch, x0            # ④ s0=旧sp，同时 sscratch=0
csrr s1, sstatus                  #    s1=status
csrr s2, sepc                     #    s2=epc
csrr s3, stval                    #    s3=badvaddr（原 sbadaddr）
csrr s4, scause                   #    s4=cause
STORE s0, 2*REGBYTES(sp)          # ⑤ 旧 sp 填回槽 2
STORE s1, 32*REGBYTES(sp)         #    status
STORE s2, 33*REGBYTES(sp)         #    epc
STORE s3, 34*REGBYTES(sp)         #    badvaddr
STORE s4, 35*REGBYTES(sp)         #    cause
.endm
```

逐点解释：

- **为什么先 `csrw sscratch, sp`**：接下来要用 `sp` 当基址，而且老 `sp` 的值也要保存，所以先挪到 `sscratch`。
- **为什么跳过 x2**：`sp` 正在被当作基址用，旧 `sp` 已在 `sscratch`，稍后通过 `s0` 写回槽 2。
- **`csrrw s0, sscratch, x0`**：原子地「读 `sscratch` 到 `s0`，并把 `sscratch` 写 0」。清零的用途：若嵌套 Trap 发生，`sscratch==0` 可提示「当前已在内核态」。
- **CSR 不能直接写内存**：所以先 `csrr` 到通用寄存器，再 `STORE`。
- 做完后，`sp` 指向的这块内存就是 `struct trapframe`，`&tf == 原 sp - 288`。

栈布局：

```
高地址
   ┌──────────────┐
   │ ...          │
sp→│ slot 0 zero  │
   │ slot 1 ra    │
   │ slot 2 sp旧值 │
   │ slot 3 gp    │
   │ ...          │
   │ slot 31 t6   │
   │ slot 32 status│
   │ slot 33 epc  │
   │ slot 34 badvaddr
   │ slot 35 cause│
   └──────────────┘
低地址
```

#### (2) `__alltraps`：入口 + 调用 C（**TODO：function1**）

```asm
.globl __alltraps
.align(2)                 # 4 字节对齐（stvec Direct 模式要求）
__alltraps:
    SAVE_ALL
    #lab3 function1 YOUR CODE
    # sp should be the same as before "jal trap"
.globl __trapret
__trapret:
    RESTORE_ALL
    sret
```

需要补的是「按 RISC-V 调用约定把 `&tf` 传给 `trap()`」：

```asm
mv a0, sp        # 第一个参数 a0 = &trapframe
jal trap         # 调用 void trap(struct trapframe *tf)
```

> 注释「sp should be the same as before jal trap」是在提醒：C 函数遵守调用约定会保存/恢复 `sp`，所以 `jal trap` 前后 `sp` 不变，`__trapret` 还能用同一个基址恢复。

#### (3) `RESTORE_ALL`：恢复现场（**TODO：function1**）

这是 `SAVE_ALL` 的严格逆操作。**要点与坑**：

1. **先恢复 CSR**：从槽 32/33 读回 `sstatus`、`sepc` 并写回 CSR（`sret` 依赖它们）；`badvaddr/cause` 只是只读信息，通常不必写回。
2. **再恢复通用寄存器**：从槽 1、3…31 读回 x1、x3~x31。
3. **`x2(sp)` 特殊**：槽 2 里存的是「进 Trap 前的 sp」。**必须最后处理**——如果过早覆盖 `sp`，后面就没法再按 `sp` 访问 trapframe 了。常见写法是先把其余寄存器都 `LOAD` 回来，最后再把槽 2 的值装回 `sp`（或先 `addi sp, sp, 36*REGBYTES` 再修正，注意别丢槽 2 的值）。
4. **不要恢复 x0**：`zero` 恒为 0，通常无需写入。
5. 恢复顺序与保存顺序相反，避免用脏寄存器当临时寄存器。

> 这是本实验最容易出错的地方：**布局错一槽，所有寄存器都错位**；**sp 恢复早一步，后面全乱**。

#### (4) `__trapret` 与 `sret`

```asm
__trapret:
    RESTORE_ALL
    sret
```

`sret` 的硬件行为：

- `pc ← sepc`
- 特权级 ← `sstatus.SPP`（本实验 1=S）
- `sstatus.SIE ← sstatus.SPIE`
- 清 `SPP`（置 0）

所以「返回哪里」由 `sepc` 决定。**这也是异常处理里要改 `tf->epc` 的原因**：改 `tf->epc` 就等于改 `sepc`，从而改变 `sret` 返回的地址。

---

### 3.10 `kern/trap/trap.c` —— 分发与处理

#### (1) `idt_init`：设置中断入口

```c
void idt_init(void) {
    extern void __alltraps(void);
    write_csr(sscratch, 0);        // 内核态陷入约定：sscratch=0
    write_csr(stvec, &__alltraps); // stvec 直接指向唯一入口（Direct 模式）
}
```

- `stvec` 低 2 位为 0 表示 **Direct 模式**：所有 Trap 都跳到同一个入口 `__alltraps`。
- `sscratch = 0`：若 Trap 前在 S 态，`sscratch` 保持 0，用于和用户态陷入区分。

#### (2) `trap_in_kernel` / `print_trapframe` / `print_regs`

```c
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;   // SPP=1 表示陷入前是 S 态
}
```

`print_trapframe` 打印 `status/epc/badvaddr/cause` 和 32 个通用寄存器，是**异常兜底和调试**的主力。

#### (3) `trap_dispatch`：按 cause 分中断/异常

```c
static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {   // scause 最高位=1 → 中断
        interrupt_handler(tf);
    } else {                          // scause 最高位=0 → 异常
        exception_handler(tf);
    }
}
void trap(struct trapframe *tf) { trap_dispatch(tf); }
```

**为什么判最高位**：RISC-V 的 `scause` 最高位（bit63）是「中断(1)/异常(0)」标志，低位是原因编号。把它当作有符号数，最高位为 1 就是负数。

#### (4) 两个 handler（**TODO：function2**）

当前是占位实现（`kern/trap/trap.c` 约 83 行）：

```c
void interrupt_handler(struct trapframe *tf) { (void)tf; }
void exception_handler(struct trapframe *tf) { (void)tf; }
```

按指导书要求，实现了它们才算完成 lab3：

**`interrupt_handler` 要点**

- `tf->cause` 低 63 位是中断编号；用 `tf->cause & 0xFF` 或与 `IRQ_S_TIMER` 比较。
- **时钟中断 `IRQ_S_TIMER`**：
  - 调 `clock_set_next_event()` **设置下一次**（不设就只响一次）；
  - `ticks++`；
  - 每 `TICK_NUM`(100) 次调 `print_ticks()` 输出一行 `100 ticks`；
  - 达到规定次数（指导书提到的 `SHUTDOWN_AFTER`）后调 `sbi_shutdown()`（注：当前骨架里没有定义 `SHUTDOWN_AFTER`，需按实验要求自行定义或向助教确认）。
- **软件中断 `IRQ_S_SOFT`**：基本类型区分 + 提示输出即可。
- **无法识别**：`print_trapframe(tf)` 兜底。

**`exception_handler` 要点**

- 非法指令 `CAUSE_ILLEGAL_INSTRUCTION`(2)、断点 `CAUSE_BREAKPOINT`(3)：输出规定格式信息。
- **必须更新 `tf->epc`**，否则 `sret` 又回到同一条指令，形成异常死循环。
- **偏移量不是常数**：RISC-V 有压缩指令（2 字节），也有 4 字节指令。要读触发异常的那条指令，根据低 2 位判断长度：
  - `(instr & 0x3) == 0x3` → 4 字节，`epc += 4`；
  - 否则 → 2 字节，`epc += 2`。
  - 注意 `ebreak` 常被汇编成 2 字节的 `c.ebreak`，不能一律 `+4`。
- 其他异常：`print_trapframe(tf)` 兜底。

> 常见 bug 清单：
> ① 时钟中断里忘了设下一次 → 只打印一次；
> ② 异常忘了 `epc += 长度` → 无限触发同一异常；
> ③ 一律 `epc += 4` → 遇到 2 字节指令跳错；
> ④ 把 `cause` 直接当编号比较，忘了掩掉 bit63。

---

### 3.11 `kern/mm/pmm.c`（lab2 依赖）—— 为什么它出现在 lab3

lab3 复用了 lab2 的物理内存管理。当前仓库：

- `init_pmm_manager()` 选择 `best_fit_pmm_manager`（lab2 的实现）；
- `page_init()` 用 DTB 信息建立 `Page` 数组、初始化空闲区；
- **`alloc_pages/free_pages/nr_free_pages` 外层套了 `local_intr_save/restore`**（lab3 骨架新增的保护）。

这正好印证「断亦有断」：分配/释放会修改空闲链表等共享数据结构，若中途被时钟中断打断、而中断处理路径又碰了内存管理，就可能破坏一致性，所以用临界区保护。

---

## 4. 一次时钟中断的完整旅程（逐步骤）

1. `clock_init()` 里 `set_csr(sie, MIP_STIP)`，并 `sbi_set_timer(now + timebase)` 上弦。
2. `intr_enable()` 打开 `sstatus.SIE`。
3. 时间到达 → 硬件产生 S 态定时器中断：
   - `sepc ← 被打断指令地址`，`scause ← IRQ_S_TIMER | 最高位`，`stvec` → `__alltraps`。
4. `__alltraps` → `SAVE_ALL`：
   - 栈上形成 `trapframe`，含 32 个寄存器 + `status/epc/badvaddr/cause`。
5. （TODO）`mv a0, sp; jal trap`：进入 C。
6. `trap()` → `trap_dispatch()`：`cause` 最高位为 1 → `interrupt_handler(tf)`。
7. 处理时钟中断：`clock_set_next_event()` 续命、`ticks++`、每 100 次打印、必要时关机。
8. 返回 `__trapret` → `RESTORE_ALL`：从 `trapframe` 恢复寄存器与 `sstatus/sepc`。
9. `sret`：`pc ← sepc`，恢复 `SIE`，回到第 3 步被打断处继续执行。
10. 下一次时钟到期，重复 3~9。

---

## 5. 自测与调试

### 编译与运行

```bash
make            # 编译
make qemu       # 运行（Ctrl+A 再按 X 退出 QEMU）
```

lab3 全部完成后，正常现象是不断打印 `100 ticks`（每秒一行）。

### GDB

```bash
make debug      # 启动 QEMU 并等待 gdb
make gdb        # 另开终端连上去
```

在 GDB 里查看 trapframe（`sp` 指向它）：

```
(gdb) break trap
(gdb) info registers sp a0
(gdb) x/36gx $sp        # 看 36 个槽，对照 trap.h 的布局
```

### 评分脚本

- `tools/inject.py` 会把 `tools/test_lab3.c/h/context.S` 拷进临时的 `kern/test/`，并往 `init.c` 注入测试调用，再编译运行。
- 因此**不要把 `test_lab3.*` 提前 include 进 `init.c`**，也不要手动创建 `kern/test`，否则注入会因「骨架不干净」而失败。`make grade` 会自动完成整套流程。

---

## 6. 关键知识点 ↔ 代码位置速查

| 知识点 | 位置 |
|---|---|
| Trap = 中断 + 异常；scause 最高位区分 | `trap.c: trap_dispatch` |
| stvec Direct 模式 | `trap.c: idt_init` |
| sepc/scause/stval 由硬件在陷入时填好 | `trapentry.S: SAVE_ALL` |
| sstatus.SIE / SPIE / SPP 的作用 | `intr.c`、`trapentry.S`、`sret` |
| trapframe 布局 = 36 槽 | `trap.h` ↔ `trapentry.S` |
| 通过 ecall 请求 M 态 SBI | `libs/sbi.c: sbi_call` |
| 定时器「一次一个事件」 | `clock.c: clock_set_next_event` |
| 临界区保存/恢复中断状态 | `sync.h` ↔ `pmm.c: alloc_pages` |
| 异常返回需修正 epc | `trap.c: exception_handler`（TODO） |

---

## 7. 给你的下一步

读完本文后，按以下顺序动手：

1. 先**只**补 `trapentry.S` 的 `jal trap` 和 `RESTORE_ALL`（function1），保证 `make qemu` 能稳定不停机。
2. 再补 `trap.c` 的 `interrupt_handler`/`exception_handler`（function2），观察 `100 ticks`。
3. 最后用 `make grade` 对照自测。

两个动手点的定位：

- `kern/trap/trapentry.S`：`__alltraps` 中 `#lab3 function1 YOUR CODE`；以及 `RESTORE_ALL` 宏内。
- `kern/trap/trap.c`：`//lab3 function2 YOUR CODE` 下方（当前为占位实现）。
