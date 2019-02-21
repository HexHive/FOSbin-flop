//
// Created by derrick on 12/4/18.
//
#include "pin.H"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <csignal>
#include <cstdlib>
#include <vector>
#include <limits.h>
#include <unistd.h>
#include "FuzzResults.h"
#include "fosbin-zergling.h"
#include <string.h>

#define USER_MSG_TYPE   1000

CONTEXT snapshot;
//CONTEXT preexecution;

//KNOB <ADDRINT> KnobStart(KNOB_MODE_WRITEONCE, "pintool", "target", "0", "The target address of the fuzzing target");
//KNOB <uint32_t> FuzzCount(KNOB_MODE_WRITEONCE, "pintool", "fuzz-count", "4", "The number of times to fuzz a target");
//KNOB <uint32_t> FuzzTime(KNOB_MODE_WRITEONCE, "pintool", "fuzz-time", "0",
//                         "The number of minutes to fuzz. Ignores fuzz-count if greater than 0.");
KNOB <uint64_t> MaxInstructions(KNOB_MODE_WRITEONCE, "pintool", "ins", "1000000",
                                "The max number of instructions to run per fuzzing round");
//KNOB <std::string> KnobOutName(KNOB_MODE_WRITEONCE, "pintool", "out", "fosbin-fuzz.log",
//                               "The name of the file to write "
//                               "logging output");
//KNOB <std::string> ContextsToUse(KNOB_MODE_APPEND, "pintool", "contexts", "", "Contexts to use for fuzzing");
//KNOB <uint32_t> HardFuzzCount(KNOB_MODE_WRITEONCE, "pintool", "hard-limit", "0",
//                              "The most fuzzing rounds regardless of time or segfaults. For debug purposes.");
//KNOB <std::string> SharedLibraryFunc(KNOB_MODE_WRITEONCE, "pintool", "shared-func", "",
//                                     "Shared library function to fuzz.");
KNOB <uint32_t> PrintToScreen(KNOB_MODE_WRITEONCE, "pintool", "print", "1",
                              "Print log messages to screen along with file");
KNOB <uint32_t> WatchDogTimeout(KNOB_MODE_WRITEONCE, "pintool", "watchdog", "20000", "Watchdog timeout in "
                                                                                     "milliseconds");
//KNOB<bool> OnlyOutputContexts(KNOB_MODE_WRITEONCE, "pintool", "only-output", "false", "Only output contexts and exit");
KNOB <std::string> KnobContextOutFile(KNOB_MODE_WRITEONCE, "pintool", "ctx-out", "",
                                      "Filename of which to output accepted contexts");
KNOB <std::string> KnobInPipe(KNOB_MODE_WRITEONCE, "pintool", "in-pipe", "", "Filename of in pipe");
KNOB <std::string> KnobOutPipe(KNOB_MODE_WRITEONCE, "pintool", "out-pipe", "", "Filename of out pipe");


RTN target = RTN_Invalid();
TLS_KEY log_key;
FBZergContext preContext;
FBZergContext currentContext;
FBZergContext expectedContext;

std::string shared_library_name;

std::ofstream infofile;
std::ifstream contextFile;
std::vector<struct X86Context> fuzzing_run;
bool fuzzed_input = false;

ZergCommandServer *cmd_server;
int internal_pipe_in[2];
int internal_pipe_out[2];
int cmd_out;
int cmd_in;
fd_set exe_fd_set_in;
fd_set exe_fd_set_out;

void wait_to_start();

void report_failure(zerg_cmd_result_t reason);

INT32 usage() {
    std::cerr << "FOSBin Zergling -- Causing Havoc in small places" << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

VOID log_message(std::stringstream &message) {
    if (message.str().empty()) {
        return;
    }
    if (infofile.is_open()) {
        infofile << message.str() << std::endl;
    }
    if (PrintToScreen.Value()) {
        std::cout << message.str() << std::endl;
    }
    message.str(std::string());
}

VOID log_error(std::stringstream &message) {
    if (infofile.is_open()) {
        infofile << message.str() << std::endl;
    }
    if (PrintToScreen.Value()) {
        std::cout << message.str() << std::endl;
    }
    PIN_WriteErrorMessage(message.str().c_str(), USER_MSG_TYPE, PIN_ERR_FATAL, 0);
}

VOID log_message(const char *message) {
    std::stringstream ss;
    ss << message;
    log_message(ss);
}

VOID log_error(const char *message) {
    std::stringstream ss;
    ss << message;
    log_error(ss);
}

ZergMessage *read_from_cmd_server() {
    ZergMessage *result = new ZergMessage();
    if (result->read_from_fd(internal_pipe_in[0]) == 0) {
        log_message("Could not read from command pipe");
    }
    return result;
}

int write_to_cmd_server(ZergMessage &msg) {
    std::cout << "Writing " << msg.str() << " to server" << std::endl;
    size_t written = msg.write_to_fd(internal_pipe_out[1]);
    if (written == 0) {
        log_message("Could not write to command pipe");
    }
    return written;
}

INS INS_FindByAddress(ADDRINT addr) {
    PIN_LockClient();
    RTN rtn = RTN_FindByAddress(addr);
    if (!RTN_Valid(rtn)) {
        return INS_Invalid();
    }

    INS ret = INS_Invalid();
    RTN_Open(rtn);
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if (INS_Address(ins) == addr) {
            ret = ins;
            break;
        }
    }
    RTN_Close(rtn);
    PIN_UnlockClient();
    return ret;
}

//VOID read_new_context() {
//    if (contextFile && contextFile.is_open() && contextFile.peek() == EOF) {
////        std::cout << "Closing contextFile" << std::endl;
//        contextFile.close();
////        std::cout << "contextFile closed" << std::endl;
//        curr_context_file_num++;
//    }
//
//    if (curr_context_file_num >= ContextsToUse.NumberOfValues()) {
//        if (FuzzCount.Value() == 0 && totalInputContextsFailed > 0) {
//            log_message("Context Test Failed");
//            PIN_ExitApplication(1);
//        } else if (FuzzCount.Value() == 0 && totalInputContextsFailed == 0) {
//            log_message("Context Test Success");
//            PIN_ExitApplication(0);
//        }
//        return;
//    }
//
////    std::cout << "Reading new context" << std::endl;
//    if ((!contextFile || !contextFile.is_open()) && curr_context_file_num < ContextsToUse.NumberOfValues()) {
//        std::stringstream ss;
//        ss << "Opening " << ContextsToUse.Value(curr_context_file_num);
//        log_message(ss);
//        contextFile.open(ContextsToUse.Value(curr_context_file_num).c_str(), ios::in | ios::binary);
//        inputContextFailed = 0;
//        inputContextPassed = 0;
//    }
//
//    contextFile >> preContext;
////    log_message("preContext:");
////    preContext.prettyPrint();
////    std::cout << "Read precontext" << std::endl;
//    contextFile >> expectedContext;
////    log_message("expectedContext:");
////    expectedContext.prettyPrint();
////    std::cout << "Read expectedcontext" << std::endl;
////    std::cout << "Done reading context" << std::endl;
//}

//VOID reset_to_context(CONTEXT *ctx, bool readNewContext) {
////    fuzz_count++;
////    std::cout << "fuzz_count = " << std::dec << fuzz_count << " orig_fuzz_count = " << orig_fuzz_count << std::endl;
//
//    if (HardFuzzCount.Value() > 0 && hard_count++ >= HardFuzzCount.Value()) {
//        std::stringstream ss;
//        ss << "Hit hard limit of " << std::dec << hard_count - 1 << std::endl;
//        log_message(ss);
//        PIN_ExitApplication(0);
//    }
//
//    if (curr_context_file_num < ContextsToUse.NumberOfValues() && readNewContext) {
//        read_new_context();
//    }
//
//    if (!timed_fuzz()) {
////        std::cout << "curr_context_file_num: " << std::dec << curr_context_file_num << std::endl;
////        std::cout << "orig_fuzz_count: " << orig_fuzz_count << std::endl;
//        if (curr_context_file_num >= ContextsToUse.NumberOfValues() && orig_fuzz_count >= FuzzCount.Value()) {
//            std::stringstream ss;
//            ss << "Stopping fuzzing at " << std::dec << orig_fuzz_count << " of " << FuzzCount.Value()
//               << std::endl;
//            log_message(ss);
//            PIN_ExitApplication(0);
//        }
//    } else {
//        if (time(NULL) >= fuzz_end_time) {
//            std::stringstream ss;
//            ss << "Stopping fuzzing after " << std::dec << FuzzTime.Value() << " minute"
//               << (FuzzTime.Value() > 1 ? "s" : "") << std::endl;
//            ss << "Total fuzzing iterations: " << std::dec << fuzz_count - 1 << std::endl;
//            log_message(ss);
//            PIN_ExitApplication(0);
//        }
//    }
//
//    PIN_SetContextReg(ctx, LEVEL_BASE::REG_RIP, RTN_Address(target));
//    fuzzing_run.clear();
//}

//VOID reset_context(CONTEXT *ctx) {
//    reset_to_context(ctx, true);
//}
//
//VOID reset_to_preexecution(CONTEXT *ctx) {
//    reset_to_context(ctx, false);
//}

uint64_t gen_random() {
    return ((((ADDRINT) rand() << 0) & 0x000000000000FFFFull) |
            (((ADDRINT) rand() << 16) & 0x00000000FFFF0000ull) |
            (((ADDRINT) rand() << 32) & 0x0000FFFF00000000ull) |
            (((ADDRINT) rand() << 48) & 0xFFFF000000000000ull)
    );
}

uint8_t *find_byte_at_random_offset(uint8_t *buffer, size_t buffer_size, size_t write_size) {
    uint64_t offset = 0;
    uint8_t *result;
    if (buffer + write_size >= buffer + buffer_size) {
        return buffer;
    }
    do {
        offset = gen_random() % buffer_size;
        result = buffer + offset;
    } while (!(result + write_size <= buffer + buffer_size));
    return result;
}

size_t flip_bit_at_random_offset(uint8_t *buffer, size_t size) {
    int bit_to_flip = rand() % CHAR_BIT;
    uint8_t *loc = find_byte_at_random_offset(buffer, size, sizeof(uint8_t));

    *(loc) ^= (1u << bit_to_flip);
    return sizeof(uint8_t);
}

size_t set_interesting_byte_at_random_offset(uint8_t *buffer, size_t size) {
    int8_t interestingvalues[] = {0, -1, 1, CHAR_MIN, CHAR_MAX};

    uint8_t *loc = find_byte_at_random_offset(buffer, size, sizeof(int8_t));
    int8_t value = interestingvalues[rand() % (sizeof(interestingvalues) / sizeof(int8_t))];
    *loc = (uint8_t) value;
    return sizeof(int8_t);
}

size_t set_interesting_word_at_random_offset(uint8_t *buffer, size_t size) {
    int32_t interestingvalues[] = {0, -1, 1, INT_MIN, INT_MAX};
    if (size < sizeof(int32_t)) {
        return set_interesting_byte_at_random_offset(buffer, size);
    }

    int32_t *loc = (int32_t *) find_byte_at_random_offset(buffer, size, sizeof(int32_t));
    int32_t value = interestingvalues[rand() % (sizeof(interestingvalues) / sizeof(int32_t))];
    *loc = value;
    return sizeof(uint32_t);
}

size_t set_interesting_dword_at_random_offset(uint8_t *buffer, size_t size) {
    if (size < sizeof(int64_t)) {
        return set_interesting_word_at_random_offset(buffer, size);
    }

    int64_t interestingvalues[] = {0, -1, 1, LONG_MIN, LONG_MAX};
    int64_t value = interestingvalues[rand() % (sizeof(interestingvalues) / sizeof(int64_t))];
    int64_t *loc = (int64_t *) find_byte_at_random_offset(buffer, size, sizeof(int64_t));
    *loc = value;
    return sizeof(uint64_t);
}

size_t inc_random_byte_at_random_offset(uint8_t *buffer, size_t size) {
    uint8_t *loc = find_byte_at_random_offset(buffer, size, sizeof(int8_t));
    *loc += 1;
    return sizeof(int8_t);
}

size_t inc_random_word_at_random_offset(uint8_t *buffer, size_t size) {
    int32_t *loc = (int32_t *) find_byte_at_random_offset(buffer, size, sizeof(int32_t));
    *loc += 1;
    return sizeof(int32_t);
}

size_t inc_random_dword_at_random_offset(uint8_t *buffer, size_t size) {
    int64_t *loc = (int64_t *) find_byte_at_random_offset(buffer, size, sizeof(int64_t));
    *loc += 1;
    return sizeof(int64_t);
}

size_t set_random_byte_at_random_offset(uint8_t *buffer, size_t size) {
    uint8_t *loc = find_byte_at_random_offset(buffer, size, sizeof(uint8_t));
    uint8_t value = (uint8_t) rand();
    *loc = value;
    return sizeof(uint8_t);
}

size_t set_random_word_at_random_offset(uint8_t *buffer, size_t size) {
    if (size < sizeof(uint32_t)) {
        return set_random_byte_at_random_offset(buffer, size);
    }

    uint32_t *loc = (uint32_t *) find_byte_at_random_offset(buffer, size, sizeof(uint32_t));
    *loc = (uint32_t) gen_random();
    return sizeof(uint32_t);
}

size_t set_random_dword_at_random_offset(uint8_t *buffer, size_t size) {
    if (size < sizeof(uint64_t)) {
        return set_random_word_at_random_offset(buffer, size);
    }

    uint64_t *loc = (uint64_t *) find_byte_at_random_offset(buffer, size, sizeof(uint64_t));
    *loc = gen_random();
    return sizeof(uint64_t);
}

size_t fuzz_strategy(uint8_t *buffer, size_t size) {
    int choice = rand() % 10;
    if (choice == 0) {
        return flip_bit_at_random_offset(buffer, size);
    } else if (choice == 1) {
        return set_interesting_byte_at_random_offset(buffer, size);
    } else if (choice == 2) {
        return set_interesting_word_at_random_offset(buffer, size);
    } else if (choice == 3) {
        return set_interesting_dword_at_random_offset(buffer, size);
    } else if (choice == 4) {
        return inc_random_byte_at_random_offset(buffer, size);
    } else if (choice == 5) {
        return inc_random_word_at_random_offset(buffer, size);
    } else if (choice == 6) {
        return inc_random_dword_at_random_offset(buffer, size);
    } else if (choice == 7) {
        return set_random_byte_at_random_offset(buffer, size);
    } else if (choice == 8) {
        return set_random_word_at_random_offset(buffer, size);
    } else if (choice == 9) {
        return set_random_dword_at_random_offset(buffer, size);
    }

    return 0;
}

VOID fuzz_registers() {
    for (REG reg : FBZergContext::argument_regs) {
        AllocatedArea *aa = preContext.find_allocated_area(reg);
        if (aa == nullptr) {
            ADDRINT value = preContext.get_value(reg);
            fuzz_strategy((uint8_t * ) & value, sizeof(value));
            preContext.add(reg, value);
        } else {
            aa->fuzz();
        }
    }
}

void output_context(const FBZergContext &ctx) {
    std::vector < AllocatedArea * > allocs;
    PinLogger &logger = *(static_cast<PinLogger *>(PIN_GetThreadData(log_key, PIN_ThreadId())));
    logger << ctx;
}

//VOID start_fuzz_round(CONTEXT *ctx) {
//    reset_context(ctx);
//    if (curr_context_file_num >= ContextsToUse.NumberOfValues()) {
//        fuzz_registers(ctx);
//    }
//    currentContext = preContext;
////    std::cout << "==========================" << std::endl;
////    preContext.prettyPrint();
////    std::cout << "==========================" << std::endl;
////    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
////    currentContext.prettyPrint();
////    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
//    currentContext >> ctx;
////    currentContext.prettyPrint();
////    displayCurrentContext(ctx);
//    std::stringstream ss;
//    ss << "Starting round " << std::dec << (++fuzz_count) << std::endl;
//    log_message(ss);
//    PIN_SpawnInternalThread(watch_dog, &watchdogtime, 0, nullptr);
//    PIN_ExecuteAt(ctx);
//}

VOID record_current_context(ADDRINT rax, ADDRINT rbx, ADDRINT rcx, ADDRINT rdx,
                            ADDRINT r8, ADDRINT r9, ADDRINT r10, ADDRINT r11,
                            ADDRINT r12, ADDRINT r13, ADDRINT r14, ADDRINT r15,
                            ADDRINT rdi, ADDRINT rsi, ADDRINT rip, ADDRINT rbp
) {
    if (cmd_server->get_state() != ZERG_SERVER_EXECUTING) {
        wait_to_start();
    }
//    std::cout << "Recording context " << std::dec << fuzzing_run.size() << std::endl;
//    std::cout << INS_Disassemble(INS_FindByAddress(rip)) << std::endl;

    struct X86Context tmp = {rax, rbx, rcx, rdx, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15, rip, rbp};
    fuzzing_run.push_back(tmp);
//    tmp.prettyPrint(std::cout);
//    int64_t diff = MaxInstructions.Value() - fuzzing_run.size();
//    std::cout << std::dec << diff << std::endl;
    if (fuzzing_run.size() > MaxInstructions.Value()) {
        report_failure(ZCMD_TOO_MANY_INS);
        log_message("write_to_cmd 3");
        wait_to_start();
    }
}

VOID trace_execution(TRACE trace, VOID *v) {
    if (RTN_Valid(target)) {
        for (BBL b = TRACE_BblHead(trace); BBL_Valid(b); b = BBL_Next(b)) {
            for (INS ins = BBL_InsHead(b); INS_Valid(ins); ins = INS_Next(ins)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) record_current_context,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RAX,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RBX,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RCX,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RDX,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R8,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R9,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R10,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R11,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R12,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R13,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R14,
                               IARG_REG_VALUE, LEVEL_BASE::REG_R15,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RDI,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RSI,
                               IARG_INST_PTR,
                               IARG_REG_VALUE, LEVEL_BASE::REG_RBP,
                               IARG_END);
            }
        }
    }
}

//VOID end_fuzzing_round(CONTEXT *ctx, THREADID tid) {
//    if (!fuzzing_started) {
//        return;
//    }
//
//    std::stringstream ss;
//    ss << "Ending fuzzing round after executing " << std::dec << fuzzing_run.size() << " instructions";
//    log_message(ss);
////    std::cout << "Post Execution Current Context addr = 0x" << std::hex << currentContext.find_allocated_area(FBZergContext::argument_regs[0])->getAddr() << std::endl;
//
////    std::cout << "Outputting precontext" << std::endl;
////    preContext.prettyPrint();
//    output_context(preContext);
////    std::cout << "currentContext:" << std::endl;
////    displayCurrentContext(ctx);
//    currentContext << ctx;
////    std::cout << "Outputting currentContext" << std::endl;
////    currentContext.prettyPrint();
//    output_context(currentContext);
//
//    if (contextFile && contextFile.is_open()) {
//        if (currentContext == expectedContext) {
//            inputContextPassed++;
//            totalInputContextsPassed++;
//        } else {
//            inputContextFailed++;
//            totalInputContextsFailed++;
//        }
//    }
//
////    fuzz_count++;
//    if (curr_context_file_num >= ContextsToUse.NumberOfValues()) {
//        orig_fuzz_count++;
//    }
//    start_fuzz_round(ctx);
//}

//VOID begin_fuzzing(CONTEXT *ctx, THREADID tid) {
//    std::stringstream ss;
//    ss << "Beginning to fuzz thread " << tid << std::endl;
//    curr_app_thread = tid;
//    log_message(ss);
//    fuzzing_started = true;
//    PIN_SaveContext(ctx, &snapshot);
//    for (REG reg : FBZergContext::argument_regs) {
//        preContext.add(reg, (ADDRINT) 0);
//    }
//    start_fuzz_round(ctx);
//}

EXCEPT_HANDLING_RESULT globalSegfaultHandler(THREADID tid, EXCEPTION_INFO *exceptionInfo, PHYSICAL_CONTEXT
*physContext, VOID *v) {
    std::stringstream ss;
    ss << "Global segfault handler called: " << PIN_ExceptionToString(exceptionInfo);
    log_error(ss);
    PIN_SetExceptionAddress(exceptionInfo, RTN_Address(target));
    PIN_RaiseException(&snapshot, tid, exceptionInfo);
    return EHR_HANDLED;
}

VOID displayCurrentContext(const CONTEXT *ctx, UINT32 sig) {
    std::stringstream ss;
    ss << "[" << (sig != SIGSEGV ? "CONTEXT" : "SIGSEGV")
       << "]=----------------------------------------------------------" << std::endl;
    ss << std::hex << std::internal << std::setfill('0')
       << "RAX = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RAX) << " "
       << "RBX = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RBX) << " "
       << "RCX = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RCX) << std::endl
       << "RDX = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RDX) << " "
       << "RDI = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RDI) << " "
       << "RSI = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RSI) << std::endl
       << "RBP = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RBP) << " "
       << "RSP = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RSP) << " "
       << "RIP = " << std::setw(16) << PIN_GetContextReg(ctx, LEVEL_BASE::REG_RIP) << std::endl;
    ss << "+-------------------------------------------------------------------" << std::endl;
    log_message(ss);
}

ADDRINT compute_effective_address(REG base, REG idx, UINT32 scale, ADDRDELTA displacement, struct X86Context &ctx) {
    if (!REG_valid(base)) {
        std::stringstream ss;
        ss << "Invalid base" << std::endl;
        return 0;
    }

    ADDRINT ret = displacement + ctx.get_reg_value(base) + ctx.get_reg_value(idx) * scale;
    return ret;
}

ADDRINT compute_effective_address(INS ins, struct X86Context &ctx, UINT32 operand = 0) {
    REG base = INS_OperandMemoryBaseReg(ins, operand);
    REG idx = INS_OperandMemoryIndexReg(ins, operand);
    UINT32 scale = INS_OperandMemoryScale(ins, operand);
    ADDRDELTA displacement = INS_OperandMemoryDisplacement(ins, operand);
//    std::cout << INS_Disassemble(ins) << std::endl;
//    std::cout << "Base: " << REG_StringShort(base)
//        << " Idx: " << REG_StringShort(idx)
//        << " Scale: 0x" << std::hex << scale
//        << " Disp: 0x" << displacement << std::endl;
//
//    ctx.prettyPrint(std::cout);

    ADDRINT ret = compute_effective_address(base, idx, scale, displacement, ctx);
//    std::cout << "ret: " << ret << std::endl;
    return ret;
}

BOOL isTainted(REG reg, std::vector<struct TaintedObject> &taintedObjs) {
    for (struct TaintedObject &to : taintedObjs) {
        if (to.isRegister && REG_StringShort(to.reg) == REG_StringShort(reg)) {
            return true;
        }
    }
    return false;
}

BOOL isTainted(ADDRINT addr, std::vector<struct TaintedObject> &taintedObjs) {
    for (struct TaintedObject &to : taintedObjs) {
        if (!to.isRegister && to.addr == addr) {
            return true;
        }
    }
    return false;
}

VOID remove_taint(REG reg, std::vector<struct TaintedObject> &taintedObjs) {
//    std::stringstream ss;
//    ss << "\tRemoving taint from " << REG_StringShort(reg) << std::endl;
//    log_message(ss);
    for (std::vector<struct TaintedObject>::iterator it = taintedObjs.begin(); it != taintedObjs.end(); ++it) {
        struct TaintedObject &to = *it;
        if (to.isRegister && REG_StringShort(to.reg) == REG_StringShort(reg)) {
            taintedObjs.erase(it);
            return;
        }
    }
}

VOID add_taint(REG reg, std::vector<struct TaintedObject> &taintedObjs) {
//    std::stringstream ss;
//    ss << "\tAdding taint to " << REG_StringShort(reg) << std::endl;
//    log_message(ss);
    struct TaintedObject to;
    to.isRegister = true;
    to.reg = reg;
    taintedObjs.push_back(to);
}

VOID remove_taint(ADDRINT addr, std::vector<struct TaintedObject> &taintedObjs) {
//    std::stringstream ss;
//    ss << "\tRemoving taint from 0x" << std::hex << addr << std::endl;
//    log_message(ss);
    for (std::vector<struct TaintedObject>::iterator it = taintedObjs.begin(); it != taintedObjs.end(); ++it) {
        struct TaintedObject &to = *it;
        if (!to.isRegister && addr == to.addr) {
            taintedObjs.erase(it);
            return;
        }
    }
}

VOID add_taint(ADDRINT addr, std::vector<struct TaintedObject> &taintedObjs) {
//    std::stringstream ss;
//    ss << "\tAdding taint to 0x" << std::hex << addr << std::endl;
//    log_message(ss);
    struct TaintedObject to;
    to.isRegister = false;
    to.addr = addr;
    taintedObjs.push_back(to);
}

BOOL inline is_rbp(REG reg) {
    return LEVEL_BASE::REG_RBP == reg;
}

BOOL create_allocated_area(struct TaintedObject &to, ADDRINT faulting_address) {
    if (to.isRegister) {
        /* Fuzzing is done with currentContext */
        AllocatedArea *aa = currentContext.find_allocated_area(to.reg);
        if (aa == nullptr) {
            aa = new AllocatedArea();
//            std::cout << "Creating allocated area for "
//                      << REG_StringShort(to.reg) << " at 0x"
//                      << std::hex << aa->getAddr() << std::endl;
            preContext.add(to.reg, aa);
            currentContext = preContext;
        } else {
            if (!aa->fix_pointer(faulting_address)) {
                std::stringstream ss;
                ss << "Could not fix pointer in register " << REG_StringShort(to.reg) << std::endl;
                log_error(ss);
            }
            AllocatedArea *tmp = preContext.find_allocated_area(to.reg);
            aa->reset_non_ptrs(*tmp);
//            currentContext.prettyPrint();
            *tmp = *aa;
            preContext.add(to.reg, tmp);
//            preContext.prettyPrint();
//            std::cout << "Fixed pointer" << std::endl;
        }
    } else {
        IMG img = IMG_FindByAddress(to.addr);
        SEC s = SEC_Invalid();
        for (s = IMG_SecHead(img); SEC_Valid(s); s = SEC_Next(s)) {
            ADDRINT sec_start = SEC_Address(s);
            ADDRINT sec_end = sec_start + SEC_Size(s);
            if (to.addr >= sec_start && to.addr < sec_end) {
                break;
            }
        }
        std::stringstream ss;
        ss << "Cannot taint non-registers. ";
        if (SEC_Valid(s)) {
            ss << "Address 0x" << std::hex << to.addr << " is in section " << SEC_Name(s) << "of image " << IMG_Name
                    (img);
        } else if (IMG_Valid(img)) {
            ss << "Address 0x" << std::hex << to.addr << " could not be found in a section but is in image " <<
               IMG_Name(img);
        } else {
            ss << "Address 0x" << std::hex << to.addr << " could not be found in an image";
        }
        log_message(ss);
        return false;
    }

//    preContext.prettyPrint();
    return true;
}

//BOOL catchOtherFault(THREADID tid, INT32 sig, CONTEXT *ctx, BOOL hasHandler, const EXCEPTION_INFO *pExceptInfo,
//                     VOID *v) {
//    if (curr_context_file_num < ContextsToUse.NumberOfValues()) {
//        inputContextFailed++;
//        totalInputContextsFailed++;
//
//        reset_context(ctx);
//        currentContext = preContext;
//        log_message("Input context failed...trying a new one");
//        currentContext >> ctx;
//
//        return false;
//    }
//
//    return true;
//}

BOOL catchSegfault(THREADID tid, INT32 sig, CONTEXT *ctx, BOOL hasHandler, const EXCEPTION_INFO *pExceptInfo, VOID *v) {
////    std::cout << PIN_ExceptionToString(pExceptInfo) << std::endl;
////    std::cout << "Fuzzing run size: " << std::dec << fuzzing_run.size() << std::endl;
////    displayCurrentContext(ctx);
////    currentContext.prettyPrint();

    if (!fuzzed_input) {
        log_message("write_to_cmd 4");
        report_failure(ZCMD_FAILED_CTX);
        wait_to_start();
    } else if (PIN_GetExceptionClass(PIN_GetExceptionCode(pExceptInfo)) != EXCEPTCLASS_ACCESS_FAULT) {
        log_message("write_to_cmd 5");
        report_failure(ZCMD_ERROR);
        wait_to_start();
    }

//
//    if (PIN_GetExceptionClass(PIN_GetExceptionCode(pExceptInfo)) != EXCEPTCLASS_ACCESS_FAULT) {
////        std::stringstream msg;
////        msg << "Invalid segfault: " << PIN_ExceptionToString(pExceptInfo);
////        log_message(msg);
//        reset_context(ctx);
//        fuzz_registers(ctx);
//        currentContext = preContext;
//        goto finish;
//    }
//
    {
        ADDRINT faulting_addr = -1;
        if (!PIN_GetFaultyAccessAddress(pExceptInfo, &faulting_addr)) {
            std::stringstream msg;
            INS faulty_ins = INS_FindByAddress(fuzzing_run.back().rip);
            msg << "Could not find faulty address for instruction at 0x" << std::hex << INS_Address(faulty_ins)
                << " (" << INS_Disassemble(faulty_ins) << ")";
            log_error(msg);
        } else {
//            std::stringstream msg;
//            currentContext.prettyPrint();
//            displayCurrentContext(ctx);
//            msg << "Faulting address: 0x" << std::hex << faulting_addr;
//            log_message(msg);
        }
        std::stringstream log;
        std::vector<struct TaintedObject> taintedObjs;
        REG taint_source = REG_INVALID();
        INS last_taint_ins = INS_Invalid();
        for (std::vector<struct X86Context>::reverse_iterator it = fuzzing_run.rbegin();
             it != fuzzing_run.rend(); it++) {
            log.str(std::string());
            struct X86Context &c = *it;
            INS ins = INS_FindByAddress(c.rip);
            if (!INS_Valid(ins)) {
                log << "Could not find failing instruction at 0x" << std::hex << c.rip << std::endl;
                log_error(log);
            }

//            log << RTN_Name(RTN_FindByAddress(INS_Address(ins)))
//                      << "(0x" << std::hex << INS_Address(ins) << "): " << INS_Disassemble(ins) << std::endl;
//            log << "\tINS_IsMemoryRead: " << (INS_IsMemoryRead(ins) ? "true" : "false") << std::endl;
//            log << "\tINS_HasMemoryRead2: " << (INS_HasMemoryRead2(ins) ? "true" : "false") << std::endl;
//            log << "\tINS_IsMemoryWrite: " << (INS_IsMemoryWrite(ins) ? "true" : "false") << std::endl;
//            log << "\tCategory: " << CATEGORY_StringShort(INS_Category(ins)) << std::endl;
//            log << "\tINS_MaxNumRRegs: " << INS_MaxNumRRegs(ins) << std::endl;
//            for (unsigned int i = 0; i < INS_MaxNumRRegs(ins); i++) {
//                log << "\t\t" << REG_StringShort(INS_RegR(ins, i)) << std::endl;
//            }
//            log << "\tINS_MaxNumWRegs: " << INS_MaxNumWRegs(ins) << std::endl;
//            for (unsigned int i = 0; i < INS_MaxNumWRegs(ins); i++) {
//                log << "\t\t" << REG_StringShort(INS_RegW(ins, i)) << std::endl;
//            }
//            log_message(log);

            if (it == fuzzing_run.rbegin()) {
                for (UINT32 i = 0; i < INS_OperandCount(ins); i++) {
                    REG possible_source = INS_OperandMemoryBaseReg(ins, i);
//                    std::cout << std::dec << i << ": " << REG_StringShort(possible_source) << std::endl;
                    if (REG_valid(possible_source) &&
                        compute_effective_address(ins, fuzzing_run.back(), i) == faulting_addr) {
                        taint_source = possible_source;
                        break;
                    }
                }

                if (!REG_valid(taint_source)) {
                    std::stringstream ss;
                    ss << "Could not find valid base register for instruction: " << INS_Disassemble(ins);
                    log_error(ss);
                }
                add_taint(taint_source, taintedObjs);
                continue;
            }

            if (INS_IsLea(ins) || INS_Category(ins) == XED_CATEGORY_DATAXFER) {
                REG wreg = REG_INVALID();
                ADDRINT writeAddr = 0;
                if (INS_OperandIsReg(ins, 0)) {
                    wreg = INS_OperandReg(ins, 0);
//                    log << "\tWrite register is " << REG_StringShort(wreg) << std::endl;
                } else if (INS_OperandIsMemory(ins, 0)) {
//                    log << "\tOperandIsMemory" << std::endl;
                    wreg = INS_OperandMemoryBaseReg(ins, 0);
                    if (!REG_valid(wreg)) {
                        writeAddr = compute_effective_address(ins, c);
                    }
                } else {
//                    log << "Write operand is not memory or register: " << INS_Disassemble(ins) << std::endl;
//                    log_error(log);
                    continue;
                }
                log_message(log);

                if (REG_valid(wreg) && !isTainted(wreg, taintedObjs)) {
//                    log << "\tWrite register is not tainted" << std::endl;
//                    log_message(log);
                    continue;
                } else if (!REG_valid(wreg) && !isTainted(writeAddr, taintedObjs)) {
//                    log << "\tWrite address is not tainted" << std::endl;
//                    log_message(log);
                    continue;
                }

                REG rreg = REG_INVALID();
                ADDRINT readAddr = 0;

                if (INS_OperandIsReg(ins, 1)) {
                    rreg = INS_OperandReg(ins, 1);
//                    log << "\tRead register is " << REG_StringShort(rreg) << std::endl;
                } else if (INS_OperandIsMemory(ins, 1)) {
                    rreg = INS_OperandMemoryBaseReg(ins, 1);
                    if (!REG_valid(rreg)) {
                        readAddr = compute_effective_address(ins, c, 1);
                    }
                } else if (INS_OperandIsImmediate(ins, 1)) {
                    continue;
                } else if (INS_OperandIsAddressGenerator(ins, 1) || INS_MemoryOperandIsRead(ins, 1)) {
                    rreg = INS_OperandMemoryBaseReg(ins, 1);
//                    log << "\tRead register is " << REG_StringShort(rreg) << std::endl;
                } else {
                    log << "Read operand is not a register, memory address, or immediate: " << INS_Disassemble(ins) <<
                        std::endl;
                    log << "OperandIsAddressGenerator: " << INS_OperandIsAddressGenerator(ins, 1) << std::endl;
                    log << "OperandIsFixedMemop: " << INS_OperandIsFixedMemop(ins, 1) << std::endl;
                    log << "OperandIsImplicit: " << INS_OperandIsImplicit(ins, 1) << std::endl;
                    log << "Base register: " << REG_StringShort(INS_MemoryBaseReg(ins)) << std::endl;
                    log << "Category: " << CATEGORY_StringShort(INS_Category(ins)) << std::endl;
                    for (UINT32 i = 0; i < INS_OperandCount(ins); i++) {
                        log << "Operand " << std::dec << i << " reg: " << REG_StringShort(INS_OperandReg(ins, i)) <<
                            std::endl;
                    }

                    log_message(log);
                    continue;
                }

                log_message(log);
                last_taint_ins = ins;
                faulting_addr = compute_effective_address(ins, c, 1);
                if (REG_valid(wreg)) {
                    if (REG_valid(rreg)) {
                        if (isTainted(wreg, taintedObjs) && !isTainted(rreg, taintedObjs)) {
                            remove_taint(wreg, taintedObjs);
                            add_taint(rreg, taintedObjs);
                        }
                    } else {
                        if (isTainted(wreg, taintedObjs) && !isTainted(readAddr, taintedObjs)) {
                            remove_taint(wreg, taintedObjs);
                            add_taint(readAddr, taintedObjs);
                        }
                    }
                } else {
                    if (REG_valid(rreg)) {
                        if (isTainted(writeAddr, taintedObjs) && !isTainted(rreg, taintedObjs)) {
                            remove_taint(wreg, taintedObjs);
                            add_taint(rreg, taintedObjs);
                        }
                    } else {
                        if (isTainted(writeAddr, taintedObjs) && !isTainted(readAddr, taintedObjs)) {
                            remove_taint(wreg, taintedObjs);
                            add_taint(readAddr, taintedObjs);
                        }
                    }
                }
            }
        }

        if (taintedObjs.size() > 0) {
            struct TaintedObject taintedObject = taintedObjs.back();
//            if (taintedObject.isRegister) {
//                log << "Tainted register: " << REG_StringShort(taintedObject.reg) << std::endl;
//            } else {
//                log << "Tainted address: 0x" << std::hex << taintedObject.addr << std::endl;
//            }

            /* Find the last write to the base register to find the address of the bad pointer */
//            INS ins = INS_FindByAddress(fuzzing_run.back().rip);
//            REG faulting_reg = taint_source;
////            std::cout << "Faulting reg: " << REG_StringShort(faulting_reg) << std::endl;
//            for (std::vector<struct X86Context>::reverse_iterator it = fuzzing_run.rbegin();
//                 it != fuzzing_run.rend(); it++) {
//                if (it == fuzzing_run.rbegin()) {
//                    continue;
//                }
//                ins = INS_FindByAddress(it->rip);
////                std::cout << INS_Disassemble(ins) << std::endl;
//                if (INS_RegWContain(ins, faulting_reg)) {
////                it->prettyPrint(std::cout);
////                    std::cout << "Write instruction: " << INS_Disassemble(ins) << std::endl;
////                    faulting_addr = compute_effective_address(ins, *it, 1);
////                    std::cout << "Faulting addr: 0x" << std::hex << faulting_addr << std::endl;
//                    break;
//                }
//            }

            if (!create_allocated_area(taintedObject, faulting_addr)) {
//                for(auto &it : fuzzing_run) {
//                    std::cout << INS_Disassemble(INS_FindByAddress(it.rip)) << std::endl;
//                }
//                reset_to_preexecution(ctx);
//                fuzz_registers(ctx);
//                goto finish;
                log_message("write_to_cmd 6");
                report_failure(ZCMD_ERROR);
                wait_to_start();
            }
        } else {
            log_message("Taint analysis failed for the following context: ");
            displayCurrentContext(ctx);
            log_message("write_to_cmd 7");
            report_failure(ZCMD_ERROR);
            wait_to_start();
        }

//        reset_to_preexecution(ctx);
    }

//    preContext.prettyPrint();
//    currentContext.prettyPrint();
    currentContext >> ctx;
//    fuzz_registers(ctx);
//    PIN_SaveContext(ctx, &preexecution);
//    displayCurrentContext(ctx);
//    log_message("Ending segfault handler");
    return false;
}

//VOID ImageLoad(IMG img, VOID *v) {
//    if (!IMG_Valid(img)) {
//        return;
//    }
//
////    std::cout << "Image " << IMG_Name(img) << " loaded" << std::endl;
//
//    if (SharedLibraryFunc.Value() == "") {
//        if (!IMG_IsMainExecutable(img)) {
//            return;
//        }
//        ADDRINT offset = IMG_LoadOffset(img);
//        ADDRINT target_addr = KnobStart.Value() + offset;
//        target = RTN_FindByAddress(target_addr);
//        if (!RTN_Valid(target)) {
//            std::stringstream ss;
//            ss << "Could not find target at 0x" << std::hex << target_addr << " (0x" << offset << " + 0x" <<
//               KnobStart.Value() << ")" << std::endl;
//            log_error(ss);
//        }
//
////        ADDRINT main_addr = IMG_Entry(img);
////        RTN main = RTN_FindByAddress(main_addr);
//        RTN main = RTN_FindByName(img, "main");
//        if (RTN_Valid(main)) {
//            RTN_Open(main);
//            std::stringstream msg;
//            msg << "Adding call to begin_fuzzing to " << RTN_Name(main) << "(0x" << std::hex << RTN_Address(main)
//                << ")";
//            log_message(msg);
//            INS_InsertCall(RTN_InsHead(main), IPOINT_BEFORE, (AFUNPTR) begin_fuzzing, IARG_CONTEXT, IARG_THREAD_ID,
//                           IARG_END);
//            RTN_Close(main);
//        } else {
//            std::stringstream ss;
//            ss << "Could not find main!" << std::endl;
//            log_error(ss);
//        }
//
//    } else {
//        if (IMG_Name(img) == shared_library_name) {
////            std::cout << shared_library_name << " has been loaded" << std::endl;
//            bool found = false;
//            for (SEC s = IMG_SecHead(img); SEC_Valid(s) && !found; s = SEC_Next(s)) {
//                for (RTN f = SEC_RtnHead(s); RTN_Valid(f); f = RTN_Next(f)) {
////                    std::cout << "Found " << RTN_Name(f) << std::endl;
//                    if (RTN_Name(f) == SharedLibraryFunc.Value()) {
//                        target = f;
//                        found = true;
//                        break;
//                    }
//                }
//            }
//            if (!found) {
////            std::cerr << "Could not find target " << SharedLibraryFunc.Value() << " in shared library " << SharedLibraryName.Value() << std::endl;
//                exit(1);
//            } else {
////                std::cout << "Found " << SharedLibraryFunc.Value() << std::endl;
//            }
//        } else if (!IMG_IsMainExecutable(img)) {
////            std::cout << "Irrelevant image" << std::endl;
//            return;
//        } else {
//            /* The loader program calls dlopen on the shared library, and then immediately returns,
//             * so add a call at the return statement to start fuzzing the shared library function
//             */
//            RTN main = RTN_FindByName(img, "main");
//            if (!RTN_Valid(main)) {
//                log_error("Invalid main in fb-loader");
//            }
//            RTN_Open(main);
//            std::stringstream ss;
//            ss << "Instrumenting loader main...";
//            for (INS ins = RTN_InsHead(main); INS_Valid(ins); ins = INS_Next(ins)) {
//                if (INS_IsRet(ins)) {
//                    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) begin_fuzzing, IARG_CONTEXT, IARG_THREAD_ID, IARG_END);
//                }
//            }
//            ss << "done!";
//            log_message(ss);
//            RTN_Close(main);
//            return;
//        }
//    }
//    std::stringstream ss;
//    ss << "Found target: " << RTN_Name(target) << " at 0x" << std::hex << RTN_Address(target) << std::endl;
//    ss << "Instrumenting returns...";
//    RTN_Open(target);
//    for (INS ins = RTN_InsHead(target); INS_Valid(ins); ins = INS_Next(ins)) {
//        if (INS_IsRet(ins)) {
////            std::cout << "Adding end_fuzzing_round" << std::endl;
//
//            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) end_fuzzing_round, IARG_CONTEXT, IARG_THREAD_ID, IARG_END);
//        }
//    }
//    INS_InsertCall(RTN_InsTail(target), IPOINT_BEFORE, (AFUNPTR) end_fuzzing_round, IARG_CONTEXT, IARG_THREAD_ID,
//                   IARG_END);
//    RTN_Close(target);
//    ss << "done.";
//    log_message(ss);
//}

//VOID ThreadStart(THREADID tid, CONTEXT *ctx, INT32 flags, VOID *v) {
//    std::string fname;
//    if (KnobContextOutFile.Value() != "") {
//        fname = KnobContextOutFile.Value();
//    } else if (SharedLibraryFunc.Value() != "") {
//        fname = SharedLibraryFunc.Value() + "." + decstr(tid) + ".ctx";
//    } else {
//        fname = RTN_Name(target) + "." + decstr(tid) + ".ctx";
//    }
//    PinLogger *logger = new PinLogger(tid, fname);
//    PIN_SetThreadData(log_key, logger, tid);
//}

//VOID ThreadFini(THREADID tid, const CONTEXT *ctx, INT32 code, VOID *v) {
//    VOID *logger_loc = PIN_GetThreadData(log_key, tid);
//    if (logger_loc != nullptr) {
////        std::cout << "Deleting logger" << std::endl;
//        PinLogger *logger = static_cast<PinLogger *>(logger_loc);
//        delete logger;
//        PIN_SetThreadData(log_key, nullptr, tid);
//    }
//
//    if (ContextsToUse.NumberOfValues() > 0) {
//        std::stringstream ss;
//        ss << "Input Contexts Passed: " << std::dec << totalInputContextsPassed << std::endl;
//        ss << "Input Contexts Failed: " << std::dec << totalInputContextsFailed << std::endl;
//        log_message(ss);
//    }
//}

//void initialize_system(int argc, char **argv) {
//    std::stringstream ss;
//    ss << "Initializing system...";
//    srand(time(NULL));
//    std::string infoFileName = KnobOutName.Value();
//    infofile.open(infoFileName.c_str(), std::ios::out | std::ios::app);

//    log_key = PIN_CreateThreadDataKey(0);
//    PIN_AddThreadStartFunction(ThreadStart, 0);
//    PIN_AddThreadFiniFunction(ThreadFini, 0);
//    fuzz_end_time = time(NULL) + 60 * FuzzTime.Value();

//    if (FuzzTime.Value()) {
//        watchdogtime = FuzzTime.Value() * 1000 * 60 + 5;
//    } else {
//        watchdogtime = WatchDogTimeout.Value();
//    }

//    if (SharedLibraryFunc.Value() != "") {
//        if (strstr(argv[argc - 2], SHARED_LIBRARY_LOADER) == NULL) {
//            ss << "fb-load must be the program run when fuzzing shared libraries" << std::endl;
//            log_error(ss);
//        }
//        shared_library_name = argv[argc - 1];
//        IMG img = IMG_Open(shared_library_name);
//        if (!IMG_Valid(img)) {
//            ss << "Could not open " << shared_library_name << std::endl;
//            log_error(ss);
//        }
//        bool found = false;
//        for (SEC s = IMG_SecHead(img); SEC_Valid(s) && !found; s = SEC_Next(s)) {
//            for (RTN f = SEC_RtnHead(s); RTN_Valid(f); f = RTN_Next(f)) {
////                std::cout << RTN_Name(f) << std::endl;
//                if (RTN_Name(f) == SharedLibraryFunc.Value()) {
//                    found = true;
//                    break;
//                }
//            }
//        }
//        if (!found) {
//            ss << "Could not find " << SharedLibraryFunc.Value() << " in library " << shared_library_name
//               << std::endl;
//            IMG_Close(img);
//            log_error(ss);
//        }
//
//        IMG_Close(img);
//    }
//
//    if (ContextsToUse.NumberOfValues() > 0) {
//        ss << "Using contexts: " << std::endl;
//        for (size_t i = 0; i < ContextsToUse.NumberOfValues(); i++) {
//            ss << ContextsToUse.Value(i) << std::endl;
//        }
//    }
//    ss << "done!" << std::endl;
//    log_message(ss);
//}

void report_success(CONTEXT *ctx, THREADID tid) {
    currentContext << ctx;

    log_message("write_to_cmd 8");
    ZergMessage msg(ZMSG_OK);
    write_to_cmd_server(msg);

    wait_to_start();
}

void report_failure(zerg_cmd_result_t reason) {
    char *buf = strdup(ZergCommand::result_to_str(reason));
    size_t len = strlen(buf) + 1;
    ZergMessage msg(ZMSG_FAIL, len, buf);
    write_to_cmd_server(msg);
    free(buf);
}

void start_cmd_server(void *v) {
    cmd_server->start();
    delete cmd_server;
    PIN_ExitApplication(0);
}

zerg_cmd_result_t handle_set_target(ZergMessage &zmsg) {
    uintptr_t new_target_addr;
    memcpy(&new_target_addr, zmsg.data(), sizeof(new_target_addr));

    std::stringstream msg;
    msg << "Setting new target to 0x" << std::hex << new_target_addr;
    log_message(msg);
    PIN_LockClient();
    RTN new_target = RTN_FindByAddress(new_target_addr);
    if (!RTN_Valid(new_target)) {
        msg << "Could not find valid target";
        log_message(msg);
        PIN_UnlockClient();
        return ZCMD_ERROR;
    }

    if (RTN_Valid(target)) {
        PIN_RemoveInstrumentationInRange(RTN_Address(target), RTN_Address(target) + RTN_Size(target));
    }

    msg << "Found target: " << RTN_Name(new_target) << " at 0x" << std::hex << RTN_Address(new_target) << std::endl;
    log_message(msg);
    msg << "Instrumenting returns...";
    int instrument_sites = 0;
    RTN_Open(new_target);
    for (INS ins = RTN_InsHead(new_target); INS_Valid(ins); ins = INS_Next(ins)) {
        if (INS_IsRet(ins)) {
            instrument_sites++;
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) report_success, IARG_CONTEXT, IARG_THREAD_ID, IARG_END);
        }
    }
    instrument_sites++;
    INS_InsertCall(RTN_InsTail(new_target), IPOINT_BEFORE, (AFUNPTR) report_success, IARG_CONTEXT,
                   IARG_THREAD_ID, IARG_END);
    RTN_Close(new_target);
    PIN_UnlockClient();

    target = new_target;

    msg << "done. ";
    msg << "Number of instrument sites = " << std::dec << instrument_sites;
    log_message(msg);
    return ZCMD_OK;
}

zerg_cmd_result_t handle_fuzz_cmd() {
    fuzz_registers();
    return ZCMD_OK;
}

zerg_cmd_result_t handle_execute_cmd() {
    fuzzing_run.clear();
    currentContext = preContext;
    currentContext >> &snapshot;
    PIN_SetContextReg(&snapshot, LEVEL_BASE::REG_RIP, RTN_Address(target));
    std::cout << "About to start executing at "
              << std::hex << RTN_Address(target) << "(" << RTN_Name(target) << ")"
              << std::dec << " with the following context" << std::endl;
    displayCurrentContext(&snapshot);

    PIN_ExecuteAt(&snapshot);
    std::cout << "PIN_ExecuteAt returned magically" << std::endl;
    return ZCMD_ERROR;
}

zerg_cmd_result_t handle_cmd() {
    ZergMessage *msg = nullptr;
    std::stringstream log_msg;
    msg = read_from_cmd_server();
    if (!msg) {
        log_message("Could not read command from server");
        return ZCMD_ERROR;
    }

    zerg_cmd_result_t result;
    switch (msg->type()) {
        case ZMSG_SET_TGT:
            log_message("Received SetTargetCommand");
            result = handle_set_target(*msg);
            break;
        case ZMSG_FUZZ:
            log_message("Received FuzzCommand");
            result = handle_fuzz_cmd();
            break;
        case ZMSG_EXECUTE:
            log_message("Received ExecuteCommand");
            result = handle_execute_cmd();
            break;
        default:
            log_msg << "Unknown command: " << log_msg;
            log_message(log_msg);
            result = ZCMD_ERROR;
            break;
    }

    delete msg;
    return result;
}

void begin_execution(CONTEXT *ctx) {
    PIN_SaveContext(ctx, &snapshot);
    for (REG reg : FBZergContext::argument_regs) {
        preContext.add(reg, (ADDRINT) 0);
    }

    wait_to_start();
}

void wait_to_start() {
    while (true) {
        log_message("Executor waiting for command");
        if (select(FD_SETSIZE, &exe_fd_set_in, nullptr, nullptr, nullptr) > 0) {
            if (FD_ISSET(internal_pipe_in[0], &exe_fd_set_in)) {
                zerg_cmd_result_t result = handle_cmd();
                if (result == ZCMD_OK) {
                    log_message("cmd server 9");
                    ZergMessage msg(ZMSG_OK);
                    write_to_cmd_server(msg);
                } else {
                    log_message("cmd server 10");
                    report_failure(result);
                }
            }
        } else {
            PIN_ExitApplication(0);
        }
    }
}

VOID FindMain(IMG img, VOID *v) {
    if (!IMG_Valid(img) || !IMG_IsMainExecutable(img)) {
        return;
    }

    RTN main = RTN_FindByName(img, "main");
    if (RTN_Valid(main)) {
        RTN_Open(main);
        std::stringstream msg;
        msg << "Adding call to wait_to_start to " << RTN_Name(main) << "(0x" << std::hex << RTN_Address(main)
            << ")";
        log_message(msg);
        INS_InsertCall(RTN_InsHead(main), IPOINT_BEFORE, (AFUNPTR) begin_execution, IARG_CONTEXT, IARG_END);
        RTN_Close(main);
    } else {
        std::stringstream ss;
        ss << "Could not find main!" << std::endl;
        log_error(ss);
    }
}

int main(int argc, char **argv) {
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) {
        return usage();
    }

    std::stringstream ss;
    ss << "Starting Zergling as process " << PIN_GetPid() << "..." << std::endl;
    log_message(ss);

    if (pipe(internal_pipe_in) != 0 || pipe(internal_pipe_out) != 0) {
        log_error("Error creating internal pipe");
    }

    FD_ZERO(&exe_fd_set_in);
    FD_ZERO(&exe_fd_set_out);
    FD_SET(internal_pipe_in[0], &exe_fd_set_in);
    FD_SET(internal_pipe_out[1], &exe_fd_set_out);

    ss << "Opening command in pipe " << KnobInPipe.Value();
    log_message(ss);
    cmd_in = open(KnobInPipe.Value().c_str(), O_RDONLY);
    if (cmd_in < 0) {
        ss << "Could not open in pipe: " << strerror(errno);
        log_error(ss);
    }
    close(cmd_in);

    ss << "Opening command out pipe " << KnobOutPipe.Value();
    log_message(ss);
    cmd_out = open(KnobOutPipe.Value().c_str(), O_WRONLY);
    if (cmd_out < 0) {
        ss << "Could not open out pipe: " << strerror(errno);
        log_error(ss);
    }
    close(cmd_out);
    log_message("done opening command pipes");

    log_message("Creating command server");
    cmd_server = new ZergCommandServer(internal_pipe_in[1], internal_pipe_out[0], KnobInPipe.Value(),
                                       KnobOutPipe.Value());
    log_message("done");

    IMG_AddInstrumentFunction(FindMain, nullptr);
    TRACE_AddInstrumentFunction(trace_execution, nullptr);
    PIN_SpawnInternalThread(start_cmd_server, nullptr, 0, nullptr);

    PIN_InterceptSignal(SIGSEGV, catchSegfault, nullptr);

    log_message("Starting");
    PIN_StartProgram();

    return 0;
}
