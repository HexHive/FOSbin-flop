//
// Created by derrick on 7/8/18.
//

#include <testRun.h>
#include <memory>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>

fbf::TestRun::TestRun(std::shared_ptr<fbf::ITestCase> test, uintptr_t offset) :
    test_(test),
    test_has_run_(false),
    offset_(offset),
    result_(std::numeric_limits<int>::max()) { }

fbf::TestRun::~TestRun() = default;

const unsigned int fbf::TestRun::TIMEOUT = 2;

static void sig_handler(int signum) {
    exit(fbf::ITestCase::FAIL);
}

void fbf::TestRun::set_signals() {
    /* Change process group ID to avoid ending the parent process if kill* is called */
    signal(SIGALRM, sig_handler);
    //signal(SIGINT, sig_handler);
    alarm(TIMEOUT);
}

uintptr_t fbf::TestRun::get_offset() {
    return offset_;
}

void fbf::TestRun::run_test() {
    if (test_has_run_) {
        return;
    }
    int pipefd[2];
    test_has_run_ = true;

    LOG_INFO << "Running test "
        << get_test_name() 
        << " on offset 0x"
        << std::hex << offset_ << std::dec
        << std::endl;
    pipe(pipefd);
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("Failed to fork");
    } else if (pid == 0) {
        close(pipefd[0]);
        set_signals();
        int result = test_->run_test();
        uint64_t retVal = test_->get_value();
        write(pipefd[1], &retVal, sizeof(retVal));
        close(pipefd[1]);
        exit(result);
    } else {
        close(pipefd[1]);
        read(pipefd[0], &execution_result_, sizeof(execution_result_));
        close(pipefd[0]);
        result_ = determine_result(pid);
    }
}

test_result_t fbf::TestRun::determine_result(pid_t child) {
    waitpid(child, &pid_status_, 0);
    if (WIFSIGNALED(pid_status_)) {
        /* SIGILL, SIGSEGV, etc. caused the child to stop...not what we are looking for */
        return fbf::ITestCase::FAIL;
    } else if(WIFEXITED(pid_status_)) {
        return (WEXITSTATUS(pid_status_) == fbf::ITestCase::PASS ?
            fbf::ITestCase::PASS :
            fbf::ITestCase::NON_CRASHING);
    } else {
        std::string msg = "Unexpected child exit status: ";
        msg += pid_status_;
        throw std::runtime_error(msg.c_str());
    }
}

uint64_t fbf::TestRun::get_execution_result() {
    return execution_result_;
}

void fbf::TestRun::output_results(std::ostream &out) {
    /* TODO: Implement a better version of this */
    std::stringstream ss;
    ss << std::hex << offset_;

    if (!test_has_run_) {
        out << "Test for "
        << test_->get_test_name()
        << " at offset 0x"
        << ss.str()
        << " was not run"
        << std::endl;
        return;
    }

    out << "Result for "
        << test_->get_test_name()
        << " at offset 0x"
        << ss.str()
        << " : "
        << ((result_ == fbf::ITestCase::PASS) ? "positive -- " : "negative -- ");

    test_->output_result(out);
    out << std::endl;
}

test_result_t fbf::TestRun::get_result() {
    if (!test_has_run_) {
        run_test();
    }

    return result_;
}

const std::string fbf::TestRun::get_test_name() {
    return test_->get_test_name();
}

bool fbf::TestRun::test_crashed() {
    return WIFSIGNALED(pid_status_) != 0;
}

uintptr_t fbf::TestRun::get_location() {
    return test_->get_location();
}
