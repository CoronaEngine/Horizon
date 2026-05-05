#pragma once

#include <csignal>
#include <cstdlib>
#include <iostream>

// 系统信号处理
inline void signal_segv_handler(int signum)
{
    std::cout << "Segmentation fault (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void signal_arbt_handler(int signum)
{
    std::cout << "Abort signal (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void signal_fpe_handler(int signum)
{
    std::cout << "Floating point exception (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void signal_ill_handler(int signum)
{
    std::cout << "Illegal instruction (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void signal_int_handler(int signum)
{
    std::cout << "Interrupt signal (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void signal_term_handler(int signum)
{
    std::cout << "Termination signal (signal " << signum << ")" << std::endl;
    exit(signum);
}

inline void setupSignalHandlers()
{
    // 捕获系统信号
    signal(SIGSEGV, signal_segv_handler);
    signal(SIGABRT, signal_arbt_handler);
    signal(SIGFPE, signal_fpe_handler);
    signal(SIGILL, signal_ill_handler);
    signal(SIGINT, signal_int_handler);
    signal(SIGTERM, signal_term_handler);
}