#include <riscv.h>

    .macro SAVE_ALL

    csrw sscratch, sp

    addi sp, sp, -36 * REGBYTES
    # save x registers
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    STORE x3, 3*REGBYTES(sp)
    STORE x4, 4*REGBYTES(sp)
    STORE x5, 5*REGBYTES(sp)
    STORE x6, 6*REGBYTES(sp)
    STORE x7, 7*REGBYTES(sp)
    STORE x8, 8*REGBYTES(sp)
    STORE x9, 9*REGBYTES(sp)
    STORE x10, 10*REGBYTES(sp)
    STORE x11, 11*REGBYTES(sp)
    STORE x12, 12*REGBYTES(sp)
    STORE x13, 13*REGBYTES(sp)
    STORE x14, 14*REGBYTES(sp)
    STORE x15, 15*REGBYTES(sp)
    STORE x16, 16*REGBYTES(sp)
    STORE x17, 17*REGBYTES(sp)
    STORE x18, 18*REGBYTES(sp)
    STORE x19, 19*REGBYTES(sp)
    STORE x20, 20*REGBYTES(sp)
    STORE x21, 21*REGBYTES(sp)
    STORE x22, 22*REGBYTES(sp)
    STORE x23, 23*REGBYTES(sp)
    STORE x24, 24*REGBYTES(sp)
    STORE x25, 25*REGBYTES(sp)
    STORE x26, 26*REGBYTES(sp)
    STORE x27, 27*REGBYTES(sp)
    STORE x28, 28*REGBYTES(sp)
    STORE x29, 29*REGBYTES(sp)
    STORE x30, 30*REGBYTES(sp)
    STORE x31, 31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    # Set sscratch register to 0, so that if a recursive exception
    # occurs, the exception vector knows it came from the kernel
    csrrw s0, sscratch, x0
    csrr s1, sstatus
    csrr s2, sepc
    csrr s3, stval
    csrr s4, scause

    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

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
