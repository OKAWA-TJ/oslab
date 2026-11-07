# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 3: 中断与异常处理 |
| **小组成员** | 2411590-李幸之、2411839-侯大川、2411207-孙曌宣 |
| **完成日期** | 2026-11-07 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2411590-李幸之 | 功能模块一（`RESTORE_ALL` / `__alltraps`）、功能模块二（`interrupt_handler` / `exception_handler`） |
| 2411839-侯大川 | 功能模块一（`RESTORE_ALL` / `__alltraps`）、功能模块二（`interrupt_handler` / `exception_handler`） |
| 2411207-孙曌宣 | 功能模块一（`RESTORE_ALL` / `__alltraps`）、功能模块二（`interrupt_handler` / `exception_handler`） |

> 本实验每位成员独立完成核心编程任务（Trap 上下文保存与恢复、Trap 入口建立、中断/异常处理函数实现），随后汇总形成本报告。

---

## 一、实验目的

1. 理解 **RISC-V 的中断与异常机制**：掌握 Trap 的硬件行为（`sepc`/`scause`/`stval`/`sstatus`/`stvec` 等 CSR 的作用），区分「中断（异步、外部触发）」与「异常（同步、当前指令触发）」。
2. 理解 **中断前后上下文环境的保存与恢复**：掌握 `struct trapframe`（中断帧）的内存布局，以及 `SAVE_ALL` / `RESTORE_ALL` 这一对汇编宏如何「保存现场 → 恢复现场」。
3. 理解 **Trap 入口与 C 语言处理函数的衔接**：掌握 `__alltraps` 如何按 RISC-V 调用约定把 trapframe 指针传给 `trap()`，以及 `sret` 如何完成特权级返回。
4. 处理 **最简单的断点中断（`ebreak`）与时钟中断**：通过处理非法指令、断点、软件中断、定时器中断，验证中断处理系统的正确性。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2411590-李幸之 | DeepSeek Harness（DSH，终端 Agent） | DeepSeek Flash（`deepseek-V41-flash`） | WSL2 / Ubuntu（Linux 6.18.33.2），工具链 `riscv64-unknown-elf-*`，QEMU `qemu-system-riscv64` |
| 2411839-侯大川 | Claude Code（VS Code 扩展，终端 Agent） | DeepSeek V4 Pro（`deepseek-v4-pro`） | WSL2 / Ubuntu（Linux 6.18.33.2），工具链 `riscv64-unknown-elf-*`，QEMU `qemu-system-riscv64` |
| 2411207-孙曌宣 | Codex（桌面版） | GPT-6 | Ubuntu 25.10（Linux 6.17.0-41-generic，x86_64）；RISC-V GCC 10.2.0、GDB 10.1、QEMU 7.0.0 |

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章围绕「**让内核学会：被打断 → 保存现场 → 用 C 处理 → 恢复现场 → 接着跑**」这一核心主题展开。指导书把这件事拆成三条主线：

1. **Trap 上下文保存与恢复** —— `kern/trap/trapentry.S` 的 `SAVE_ALL` / `RESTORE_ALL`。
2. **Trap 入口与返回** —— `kern/trap/trapentry.S` 的 `__alltraps` / `__trapret` + `trap.h` 的 `struct trapframe` + `trap.c` 的 `trap()`。
3. **中断与异常处理** —— `kern/trap/trap.c` 的 `trap_dispatch()` / `interrupt_handler()` / `exception_handler()`。

整条链路可以概括为一个「硬件陷入 + 软件处理」的闭环：

```
时钟/异常事件 → 硬件陷入（sepc←pc, scause←原因, stvec→__alltraps）
    → SAVE_ALL 保存现场（在栈上构造 trapframe）
    → jal trap 进入 C 语言处理函数
    → trap_dispatch 按 scause 最高位区分中断/异常
    → interrupt_handler / exception_handler 分类型处理
    → RESTORE_ALL 恢复现场
    → sret 返回原执行流
```

### 3.2 功能的逐步实现

1. **`kern/trap/trap.c` 的 `idt_init()`** —— 设置 `stvec = &__alltraps`（Direct 模式），让所有 Trap 都跳转到统一的汇编入口。
2. **`kern/driver/clock.c` 的 `clock_init()`** —— 通过 `set_csr(sie, MIP_STIP)` 使能时钟中断，`sbi_set_timer()` 设置第一个时钟事件。
3. **`kern/driver/intr.c` 的 `intr_enable()`** —— `set_csr(sstatus, SSTATUS_SIE)` 打开 S 态全局中断使能位。
4. **`kern/trap/trapentry.S` 的 `SAVE_ALL`（已提供）** —— 在栈上分配 36 个槽位，保存 32 个通用寄存器 + `sstatus/sepc/stval/scause`。
5. **`kern/trap/trapentry.S` 的 `RESTORE_ALL`（本实验实现）** —— 与 `SAVE_ALL` 严格对应，从 trapframe 恢复处理器状态。
6. **`kern/trap/trapentry.S` 的 `__alltraps`（本实验实现）** —— `move a0, sp; jal trap`，把 trapframe 指针传给 C 函数。
7. **`kern/trap/trap.c` 的 `interrupt_handler` / `exception_handler`（本实验实现）** —— 分类型处理中断与异常。
8. **`kern/sync/sync.h` + `libs/atomic.h`** —— 提供「保存并恢复中断状态」的临界区，保护内存管理数据结构（「断亦有断」）。

---

## 四、实验内容与实现

### 功能模块一：Trap 上下文恢复与 Trap 入口

模块一要求补全 `kern/trap/trapentry.S` 中的 `RESTORE_ALL` 宏与 `__alltraps` 入口，使系统形成「Trap 进入 → 上下文保存 → C 语言处理 → 上下文恢复 → Trap 返回」的完整闭环。

#### 任务一：`__alltraps` —— 建立汇编入口与 C 函数的调用关系

`SAVE_ALL` 执行结束后，`sp` 恰好指向栈上的 trapframe。按 RISC-V 调用约定，第一个参数放在 `a0`，所以把 `sp` 赋给 `a0` 再 `jal trap` 即可把 trapframe 指针传给 `trap(struct trapframe *tf)`：

```asm
    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    # 按照 RISC-V 调用约定，把 trapframe 的地址作为 trap() 的参数传入。
    # SAVE_ALL 结束后 sp 恰好指向栈上的 trapframe，因此：
    #   move a0, sp  -> a0 = &trapframe（第一个参数）
    #   jal  trap    -> 调用 trap(struct trapframe *tf)
    # trap() 是普通 C 函数，遵守调用约定，返回后 sp 仍指向 trapframe，
    # 因此随后落入 __trapret 时 RESTORE_ALL 仍能通过 sp 找到上下文。
    move  a0, sp
    jal   trap

    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
```

**关键点**：`jal trap` 会把返回地址写入 `ra`，但由于 `SAVE_ALL` 已经把原来的 `ra` 存进了 trapframe、之后 `RESTORE_ALL` 会恢复它，所以这里的 `ra` 被覆盖不影响正确性。`trap()` 返回后 `sp` 仍指向 trapframe（C 调用约定保证 callee 不改变 `sp`），因此 `__trapret` 里的 `RESTORE_ALL` 能继续以 `sp` 为基址恢复上下文。

#### 任务二：`RESTORE_ALL` —— 从 trapframe 恢复处理器状态

`RESTORE_ALL` 必须与已提供的 `SAVE_ALL` 严格对应。trapframe 布局为：偏移 `0..31` 是 `x0..x31` 通用寄存器（`struct pushregs`），偏移 `32` 是 `status`，`33` 是 `epc`，`34` 是 `badvaddr`，`35` 是 `cause`（对应 `struct trapframe`）。

恢复顺序有三个约束：

1. **先恢复 CSR**（`sstatus`/`sepc`），因为之后要借用 `s1`/`s2` 作临时寄存器，且 `sret` 依赖 `sepc`；
2. **`sp(x2)` 必须最后恢复**，因为此前的所有 `LOAD` 都以 `sp` 为基址访问 trapframe；
3. **不需要恢复 `sscratch`**——本实验始终处于 S 态，`sscratch` 的约定值是 0（`SAVE_ALL` 里 `csrrw s0, sscratch, x0` 已把它清零）。

```asm
    .macro RESTORE_ALL

    # 与 SAVE_ALL 严格对应：从栈上的 trapframe 恢复处理器状态。
    # 顺序上必须先恢复 sstatus / sepc 两个 CSR，再恢复通用寄存器，
    # 最后才能恢复 sp(x2)，因为此前的所有 LOAD 都以 sp 作为基址。

    # 先取出保存的 sstatus 与 sepc，借用 s1/s2 作临时寄存器
    LOAD s1, 32*REGBYTES(sp)       # s1 = 陷入前保存的 sstatus
    LOAD s2, 33*REGBYTES(sp)       # s2 = 陷入前保存的 sepc

    csrw sstatus, s1               # 恢复 sstatus（SIE/SPP/SPIE 等）
    csrw sepc, s2                  # 恢复 sepc，sret 将跳回该地址继续执行

    # 恢复除 x2(sp) 之外的全部通用寄存器
    LOAD x1, 1*REGBYTES(sp)        # ra
    LOAD x3, 3*REGBYTES(sp)        # gp
    LOAD x4, 4*REGBYTES(sp)        # tp
    LOAD x5, 5*REGBYTES(sp)        # t0
    LOAD x6, 6*REGBYTES(sp)        # t1
    LOAD x7, 7*REGBYTES(sp)        # t2
    LOAD x8, 8*REGBYTES(sp)        # s0
    LOAD x9, 9*REGBYTES(sp)        # s1
    LOAD x10, 10*REGBYTES(sp)      # a0
    LOAD x11, 11*REGBYTES(sp)      # a1
    LOAD x12, 12*REGBYTES(sp)      # a2
    LOAD x13, 13*REGBYTES(sp)      # a3
    LOAD x14, 14*REGBYTES(sp)      # a4
    LOAD x15, 15*REGBYTES(sp)      # a5
    LOAD x16, 16*REGBYTES(sp)      # a6
    LOAD x17, 17*REGBYTES(sp)      # a7
    LOAD x18, 18*REGBYTES(sp)      # s2
    LOAD x19, 19*REGBYTES(sp)      # s3
    LOAD x20, 20*REGBYTES(sp)      # s4
    LOAD x21, 21*REGBYTES(sp)      # s5
    LOAD x22, 22*REGBYTES(sp)      # s6
    LOAD x23, 23*REGBYTES(sp)      # s7
    LOAD x24, 24*REGBYTES(sp)      # s8
    LOAD x25, 25*REGBYTES(sp)      # s9
    LOAD x26, 26*REGBYTES(sp)      # s10
    LOAD x27, 27*REGBYTES(sp)      # s11
    LOAD x28, 28*REGBYTES(sp)      # t3
    LOAD x29, 29*REGBYTES(sp)      # t4
    LOAD x30, 30*REGBYTES(sp)      # t5
    LOAD x31, 31*REGBYTES(sp)      # t6

    # 最后恢复 sp(x2)。执行完这条后 sp 已不再指向 trapframe，
    # 不能再通过 sp 访问栈上保存的任何内容。
    LOAD x2, 2*REGBYTES(sp)        # sp
    .endm
```

**注意**：`SAVE_ALL` 中对 `x2(sp)` 的处理与其他通用寄存器不同（旧 `sp` 先经 `csrw sscratch, sp` 保存，再 `csrrw s0, sscratch, x0` 取出存入 trapframe 的偏移 2）。因此 `RESTORE_ALL` 不能把所有寄存器按完全相同的方式恢复，必须把 `sp` 放到最后单独 `LOAD`。

### 功能模块二：中断与异常处理

模块二要求补全 `kern/trap/trap.c` 中的 `interrupt_handler()` 与 `exception_handler()`，对 `trap_dispatch()` 分发过来的中断/异常进行分类处理。

#### 任务三：`interrupt_handler()` —— 处理时钟中断、软件中断等

`trap_dispatch()` 用 `(intptr_t)tf->cause < 0` 判断最高位：为 1 是中断，交给 `interrupt_handler`。此时 `scause` 的低 63 位才是中断编号，需先用算术移位把最高位去掉：

```c
void interrupt_handler(struct trapframe *tf) {
    // scause 的 bit63 是「中断/异常」标志位，低 63 位才是中断编号；
    // 用算术移位（左移丢掉 bit63，再右移）得到纯净的中断编号 cause。
    intptr_t cause = (tf->cause << 1) >> 1;

    switch (cause) {
        case IRQ_U_SOFT:  // 0 用户态软件中断
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_SOFT:  // 1 S 态软件中断
            cprintf("Supervisor software interrupt\n");
            break;
        /* ... 其余软件/定时器/外部中断的区分提示 ... */
        case IRQ_S_TIMER: // 5 S 态定时器中断（时钟中断）
            // 1) 立即设置下一次时钟事件，否则时钟中断只会触发一次
            clock_set_next_event();
            // 2) 累加时钟中断计数
            ticks++;
            // 3) 每 TICK_NUM(100) 次（约 1 秒）输出一行统计信息
            if (ticks % TICK_NUM == 0) {
                print_ticks();
                // 4) 累计输出达到 SHUTDOWN_AFTER 次后关机
                if (ticks / TICK_NUM >= SHUTDOWN_AFTER) {
                    sbi_shutdown();
                }
            }
            break;
        default:
            // 无法识别的中断：打印提示并 dump 完整 trapframe 辅助调试
            cprintf("Unknown interrupt: cause %ld\n", cause);
            print_trapframe(tf);
            break;
    }
}
```

**时钟中断的处理要点**：

1. **必须续设下一次时钟事件**（`clock_set_next_event()`）：OpenSBI 的 `sbi_set_timer` 一次只设一个事件，不续命就只会响一次。
2. **`ticks++` 累加**：每收到一次时钟中断自增一次。
3. **每 `TICK_NUM`(100) 次输出一行 `100 ticks`**：即每秒打印一行统计信息。
4. **累计输出达到 `SHUTDOWN_AFTER` 次后关机**：本实验在 `trap.c` 中补充定义了 `#define SHUTDOWN_AFTER 10`，调用 `libs/sbi.h` 提供的 `sbi_shutdown()`。

#### 任务四：`exception_handler()` —— 处理非法指令、断点等异常

异常（最高位为 0）的 `tf->cause` 直接就是异常编号。非法指令与断点必须按指令长度修正 `tf->epc`，否则 `sret` 后又执行同一条指令，形成异常死循环：

```c
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:  // 0x2 非法指令
            cprintf("Illegal instruction at 0x%lx\n", tf->epc);
            tf->epc += instruction_length(tf->epc);  // 跳过这条非法指令
            break;
        case CAUSE_BREAKPOINT:  // 0x3 断点（ebreak）
            cprintf("Breakpoint at 0x%lx\n", tf->epc);
            tf->epc += instruction_length(tf->epc);  // 跳过 ebreak
            break;
        default:
            // 本实验不要求其它异常的恢复逻辑，dump trapframe 兜底
            print_trapframe(tf);
            break;
    }
}
```

**`epc` 偏移量不能是常数**：RISC-V 有 2 字节压缩指令（如 `c.ebreak`）和 4 字节指令，不同异常对应的指令长度可能不同。因此先读取触发异常的那条指令，按最低 2 位动态判断长度：

```c
// 根据 RISC-V 指令编码判断指令长度（字节数）：
//   最低 2 位 == 0b11  -> 32 位指令（4 字节）
//   否则              -> 16 位压缩指令（2 字节）
static inline uintptr_t instruction_length(uintptr_t epc) {
    uint16_t instr = *(volatile uint16_t *)epc;
    return (instr & 0x3) == 0x3 ? 4 : 2;
}
```

### 断亦有断：临界区（`kern/sync/sync.h` + `libs/atomic.h`）

在分配/回收物理页时，会修改空闲页链表等共享数据结构，若中途被时钟中断打断、而中断处理路径又碰了内存管理，就会破坏链表一致性。因此 `pmm.c` 的 `alloc_pages/free_pages/nr_free_pages` 外层都套了「保存并恢复中断状态」的临界区：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {  // 原来开着中断吗？
        intr_disable();                     // 关掉
        return 1;                           // 记住「原来开着」
    }
    return 0;                               // 原来就关着，无需恢复
}
static inline void __intr_restore(bool flag) {
    if (flag) intr_enable();                // 只有原来开着才重新打开
}
```

其核心思想是「**保存并恢复**」而非「无脑关闭/打开」：若调用者进入临界区前本来就已经关了中断，退出时就不能 `intr_enable()`，否则会破坏调用者原有的状态（嵌套临界区场景）。`libs/atomic.h` 则用 AMO 原子指令（`amoor/amoand/amoxor`）提供位级原子操作，是后续自旋锁的底层原语——但关中断临界区只在单核上有效，AMO 才是多核可用的原子原语。

---

## 五、测试与验证

### 5.1 编译链接

执行 `make`，`kern/trap/trap.c` 与 `kern/trap/trapentry.S` 编译通过，最终链接生成 `bin/kernel` 与 `bin/ucore.img`，无任何错误与警告（`-Werror` 下通过）。

### 5.2 自动评分（`make grade`）

`tools/inject.py` 会把 `tools/test_lab3.c/h/context.S` 注入临时的 `kern/test/`，并按「断点返回 / 非法指令返回 / 定时器持续中断」三组用例依次编译运行，结果 **100/100**：

```
Running trap entry, register restore, and breakpoint EPC...
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
Running illegal instruction EPC advance and return...
  - illegal instruction EPC advance and return: PASS (25/25)
Running re-armed supervisor timer and repeated trap return...
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

三组用例分别验证了：

- **Test 1（`GRADE_LAB3_CONTEXT`，35 分）**：`test_lab3_register_context` 预置 `t0~t6/s0~s1/a1~a7/s2~s11` 等 30 个寄存器为特定值后执行 `ebreak`，断点返回后逐寄存器校验值未被破坏（验证 `__alltraps` 建立入口、`RESTORE_ALL` 完整恢复寄存器），并输出 `Breakpoint at ...`（验证 `exception_handler` 正确处理断点）。
- **Test 2（`GRADE_LAB3_ILLEGAL`，25 分）**：`test_lab3_illegal_instruction` 用 `.word 0x0000007b`（4 字节非法指令）触发非法指令异常，要求输出 `Illegal instruction at ...` 且 `epc` 前进 4 字节；`test_lab3_handler_fallbacks` 直接调用 `interrupt_handler`（软件中断、cause=15）与 `exception_handler`（cause=20），要求输出 `Supervisor software interrupt`、`Unknown interrupt: cause 15` 与 `print_trapframe` 的 `cause 0x00000014`。
- **Test 3（`GRADE_LAB3_TIMER`，40 分）**：使能中断后等待 `ticks` 涨到 200，要求持续输出 `100 ticks`（每 100 次）且最终 `ticks=200`（验证时钟中断的续命与 `ticks++`）。

### 5.3 运行（`make qemu`）

正常运行时，内核初始化后使能中断，每秒打印一行 `100 ticks`：

```
(THU.CST) os is loading ...
...
++ setup timer interrupts
100 ticks
100 ticks
...
```

---

## 六、实验总结与收获

### 6.1 本实验重要知识点与 OS 原理对照

| 本实验知识点 | OS 原理对应 | 含义、关系与差异 |
| --- | --- | --- |
| Trap = 中断 + 异常；`scause` 最高位区分 | 中断/异常的分类 | 中断异步（时钟），异常同步（非法指令、断点）；硬件用 bit63 标记并填低 63 位编号 |
| `stvec` / `sepc` / `scause` / `stval` / `sstatus` | 中断向量与现场保存 | 陷入时硬件自动填 `sepc`（返回地址）、`scause`（原因）、`stval`；`stvec` 指向入口，`sret` 完成返回 |
| `struct trapframe`（36 槽） | 进程控制块（PCB）里的上下文 | trapframe 是「被打断现场」的内存快照，与 PCB 保存的寄存器现场同构 |
| `SAVE_ALL` / `RESTORE_ALL` | 上下文切换 | 通过「压栈保存 + 出栈恢复」实现现场的保存与恢复，是进程切换/系统调用的基础 |
| `sret` 特权指令 | 中断/异常返回 | 恢复 `pc←sepc`、恢复 `SIE←SPIE`，是从内核返回被打断执行流的硬件机制 |
| `interrupt_handler` / `exception_handler` | 中断处理程序 / 异常处理程序 | 按 `scause` 分类分发，是「中断处理」在 OS 中的落地 |
| 时钟中断续命（`clock_set_next_event`） | 定时器 / 时钟机制 | 硬件定时器一次只触发一个事件，OS 需在处理函数里设置下一次，形成持续节拍 |
| 修正 `tf->epc` 跳过异常指令 | 异常处理与恢复 | 异常返回不能重复执行出错指令，需按指令长度前进 `epc` |
| 临界区（`sync.h` 保存/恢复中断） | 互斥与临界区 | 关中断保护共享数据结构，单核下最简单的原子性保证；`atomic.h` 的 AMO 是多核原语 |

### 6.2 OS 原理中重要、但本实验未覆盖的知识点

1. **系统调用与用户态陷入**：本实验所有 Trap 都是 S 态自陷（`sscratch` 恒为 0），没有涉及 U 态 `ecall`、用户栈/内核栈切换。
2. **中断嵌套与优先级**：只处理了最简单的单层中断，没有多级中断嵌套、中断屏蔽/优先级管理。
3. **外设中断（UART、磁盘等）**：只验证了软件中断与定时器中断，外部设备中断的 `PLIC` 等控制器未涉及。
4. **多核中断处理 / 自旋锁**：`sync.h` 的关中断临界区只在单核有效；跨核的同步需要 `atomic.h` 的 AMO 指令配合自旋锁（lab4+）。
5. **中断处理程序与调度器的交互**：本实验没有进程/调度，时钟中断尚未承担「时间片轮转」的职责。

### 6.3 AI 协作开发的经验与遇到的 bug

**实现过程中遇到并解决的问题：**

1. **`RESTORE_ALL` 中 `sp` 的恢复顺序**：一开始容易把所有寄存器「一视同仁」地 `LOAD` 回去，但 `SAVE_ALL` 对 `x2(sp)` 的处理方式与其它寄存器不同（旧 `sp` 先存 `sscratch` 再转存 trapframe 偏移 2），且所有 `LOAD` 都以 `sp` 为基址。解决：先把 `sstatus/sepc` 写回 CSR，再恢复其余 GPR，最后单独恢复 `sp`。这是「trapframe 布局 ↔ 保存 ↔ 恢复」三者一致性的关键。
2. **异常不修正 `epc` 导致死循环**：初版 `exception_handler` 只打印不前进 `epc`，`sret` 回到同一条非法指令/`ebreak`，形成异常无限循环。解决：按指令长度 `tf->epc += instruction_length(...)`。
3. **一律 `epc += 4` 在压缩指令上跳错**：`ebreak` 常被汇编成 2 字节的 `c.ebreak`（0x9002），若固定 `+4` 会跳到下一条指令中间。解决：读触发指令的低 2 位动态判断长度（`(instr & 0x3) == 0x3 ? 4 : 2`）。
4. **时钟中断忘记续命 → 只响一次**：`sbi_set_timer` 一次只设一个事件，处理函数里必须 `clock_set_next_event()`，否则 `ticks` 停在 1。解决：在 `IRQ_S_TIMER` 分支首行续设下一次事件。
5. **把 `scause` 直接当编号比较**：中断的 `scause` 带 bit63，若不加处理直接与 `IRQ_S_TIMER` 比较会失配。解决：用 `(tf->cause << 1) >> 1` 掩掉最高位再 `switch`。
6. **`SHUTDOWN_AFTER` 未定义**：骨架里只有 `TICK_NUM`，指导书提到的 `SHUTDOWN_AFTER` 需自行补充。解决：在 `trap.c` 定义 `#define SHUTDOWN_AFTER 10` 并按 `ticks / TICK_NUM` 统计累计输出次数后调用 `sbi_shutdown()`。

**AI 协作的体会：**

- **先让 AI 复述「约定」再写实现**：`struct trapframe` 布局、`SAVE_ALL` 的保存顺序、RISC-V 调用约定是硬约束，AI 只有先对齐这些约定，`RESTORE_ALL` 才不会与 `SAVE_ALL` 错位。
- **评分脚本是比文档更硬的规格**：`grade.sh` 用 `grep -F` 精确匹配 `Unknown interrupt: cause 15`、`cause 0x00000014` 等字符串，反向约束了处理函数的输出格式与分类逻辑。
- **分层验证依然有效**：先 `make` 看编译、再 `make qemu` 看 `100 ticks`、最后 `make grade` 看注入测试，三层逐步缩小问题范围，与 lab2 的调试方法论一致。
