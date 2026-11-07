#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <trap.h>

#define TICK_NUM 100
// 累计打印 "100 ticks"（每秒一行）的次数达到该值后，调用 sbi_shutdown() 关机。
#define SHUTDOWN_AFTER 10

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

//lab3 function2 YOUR CODE
// 根据 RISC-V 指令编码判断指令长度（字节数）：
//   最低 2 位 == 0b11  -> 32 位指令（4 字节）
//   否则              -> 16 位压缩指令（2 字节）
// 异常处理返回前必须跳过触发异常的那条指令，而非法指令/断点既可能是
// 4 字节指令也可能是 2 字节压缩指令（如 c.ebreak），因此不能一律 +4，
// 需要读取指令内容按编码动态判断。
static inline uintptr_t instruction_length(uintptr_t epc) {
    uint16_t instr = *(volatile uint16_t *)epc;
    return (instr & 0x3) == 0x3 ? 4 : 2;
}

/* interrupt_handler - 处理中断（scause 最高位为 1）。
 * 时钟中断需要：续设下一次时钟事件 + 计数 + 定期输出统计 + 达到次数后关机；
 * 其余中断只做基本类型区分并打印提示，无法识别的中断 dump 完整 trapframe。
 */
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
        case IRQ_H_SOFT:  // 2 H 态软件中断
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:  // 3 M 态软件中断
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_TIMER: // 4 用户态定时器中断
            cprintf("User timer interrupt\n");
            break;
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
        case IRQ_H_TIMER: // 6 H 态定时器中断
            cprintf("Hypervisor timer interrupt\n");
            break;
        case IRQ_M_TIMER: // 7 M 态定时器中断
            cprintf("Machine timer interrupt\n");
            break;
        case IRQ_U_EXT:   // 8 用户态外部中断
            cprintf("User external interrupt\n");
            break;
        case IRQ_S_EXT:   // 9 S 态外部中断
            cprintf("Supervisor external interrupt\n");
            break;
        case IRQ_H_EXT:   // 10 H 态外部中断
            cprintf("Hypervisor external interrupt\n");
            break;
        case IRQ_M_EXT:   // 11 M 态外部中断
            cprintf("Machine external interrupt\n");
            break;
        default:
            // 无法识别的中断：打印提示并 dump 完整 trapframe 辅助调试
            cprintf("Unknown interrupt: cause %ld\n", cause);
            print_trapframe(tf);
            break;
    }
}

/* exception_handler - 处理异常（scause 最高位为 0）。
 * 非法指令与断点需要按指令长度修正 tf->epc，避免 sret 后又执行同一条指令
 * 形成死循环；其余异常 dump 完整 trapframe 兜底。
 */
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

static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
