//
// Created by derrick on 12/20/18.
//

ADDRINT X86Context::get_reg_value(REG reg) {
    switch (reg) {
        case LEVEL_BASE::REG_RAX:
            return rax;
        case LEVEL_BASE::REG_RBX:
            return rbx;
        case LEVEL_BASE::REG_RCX:
            return rcx;
        case LEVEL_BASE::REG_RDX:
            return rdx;
        case LEVEL_BASE::REG_RSI:
            return rsi;
        case LEVEL_BASE::REG_R8:
            return r8;
        case LEVEL_BASE::REG_R9:
            return r9;
        case LEVEL_BASE::REG_R10:
            return r10;
        case LEVEL_BASE::REG_R11:
            return r11;
        case LEVEL_BASE::REG_R12:
            return r12;
        case LEVEL_BASE::REG_R13:
            return r13;
        case LEVEL_BASE::REG_R14:
            return r14;
        case LEVEL_BASE::REG_R15:
            return r15;
        case LEVEL_BASE::REG_RIP:
            return rip;
        case LEVEL_BASE::REG_RBP:
            return rbp;
        default:
            return 0;
    }
}

std::ostream &operator<<(std::ostream &out, const struct X86Context &ctx) {
    out << ctx.rax << ctx.rbx << ctx.rcx << ctx.rdx << ctx.rsi
        << ctx.r8 << ctx.r9 << ctx.r10 << ctx.r11 << ctx.r12
        << ctx.r13 << ctx.r14 << ctx.r15 << ctx.rip << ctx.rbp;

    return out;
}
