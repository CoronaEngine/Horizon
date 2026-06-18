//
// Created by Zero on 2024/8/24.
//

#include "cmd_process.h"
#include "hotfix.h"

namespace vision::inline hotfix {
CmdProcess::CmdProcess() {
    ZeroMemory(&process_info_, sizeof(process_info_));
}

void CmdProcess::CmdData::enqueue_callback() noexcept {
    if (callback) {
        HotfixSystem::instance().enqueue_function([callback = callback,
                                                   cmd = cmd,
                                                   success = bool(success)] {
            std::invoke(callback, cmd, bool(success));
        });
    }
}

void CmdProcess::change_directory(const fs::path &dir) const noexcept {
    write_input(ocarina::format("cd {}", dir.string()));
}

void CmdProcess::on_finish_cmd() const {
    if (!store_cmd_output_) {
        std::cout << "[Cmd process] Complete" << std::endl;
    }
    with_lock([&] {
        CmdData &cmd = cmd_queue_.front();
        cmd.enqueue_callback();
        cmd_queue_.pop();
        is_working_ = false;
    });
}

void CmdProcess::on_start_cmd() const {
    if (!store_cmd_output_) {
        std::cout << "[Cmd process] start" << std::endl;
    }
    is_working_ = true;
}

void CmdProcess::read_output_thread() {
    CHAR lpBuffer[1024];
    DWORD nBytesRead;
    string output;
    while (true) {
        if (!ReadFile(output_read_, lpBuffer, sizeof(lpBuffer) - 1,
                      &nBytesRead, nullptr) ||
            !nBytesRead) {
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                std::cerr << "[Cmd process error] Redirect of compile output failed on read" << std::endl;
            }
            break;
        }

        lpBuffer[nBytesRead] = 0;// Add null termination
        std::string buffer(lpBuffer);
        size_t found = buffer.find(c_CompletionToken);
        if (found != std::string::npos) {
            buffer = buffer.substr(0, found);
            on_finish_cmd();
        }

        found = buffer.find(c_StartToken);
        if (found != std::string ::npos) {
            buffer = buffer.substr(0, found);
            on_start_cmd();
        }

        if (buffer.empty()) {
            continue;
        }
        if (store_cmd_output_) {
            cmd_output_ += buffer;
            continue;
        }
        if (buffer.find(": error C") != std::string::npos ||
            buffer.find(": fatal error ") != std::string::npos) {
            cmd_queue_.front().success = false;
            std::cerr << "[Cmd process Warning]: " << buffer << std::endl;
        } else {
            std::cout << "" << buffer;
        }
    }
}

void CmdProcess::initialise() {
    //init compile process
    STARTUPINFOW startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    // Set up the security attributes struct.
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    // Create the child output pipe.
    //redirection of output
    startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    HANDLE hOutputReadTmp = nullptr, hOutputWrite = nullptr, hErrorWrite = nullptr;
    auto exit_func = [&] {
        if (hOutputReadTmp) {
            CloseHandle(hOutputReadTmp);
        }
        if (hOutputWrite) {
            CloseHandle(hOutputWrite);
        }
        if (hErrorWrite) {
            CloseHandle(hErrorWrite);
        }
    };
    if (!CreatePipe(&hOutputReadTmp, &hOutputWrite, &sa, 20 * 1024)) {
        OC_WARNING("[RuntimeCompiler] Failed to create output redirection pipe\n");
        exit_func();
        return;
    }
    startupInfo.hStdOutput = hOutputWrite;

    // Create a duplicate of the output write handle for the std error
    // write handle. This is necessary in case the child application
    // closes one of its std output handles.
    if (!DuplicateHandle(GetCurrentProcess(), hOutputWrite,
                         GetCurrentProcess(), &hErrorWrite, 0,
                         TRUE, DUPLICATE_SAME_ACCESS)) {
        OC_WARNING("[RuntimeCompiler] Failed to duplicate error output redirection pipe\n");
        exit_func();
        return;
    }
    startupInfo.hStdError = hErrorWrite;

    // Create new output read handle and the input write handles. Set
    // the Properties to FALSE. Otherwise, the child inherits the
    // properties and, as a result, non-closeable handles to the pipes
    // are created.
    if (startupInfo.hStdOutput) {
        if (!DuplicateHandle(GetCurrentProcess(), hOutputReadTmp,
                             GetCurrentProcess(),
                             &output_read_,// Address of new handle.
                             0, FALSE,     // Make it uninheritable.
                             DUPLICATE_SAME_ACCESS)) {
            OC_WARNING("[RuntimeCompiler] Failed to duplicate output read pipe\n");
            exit_func();
            return;
        }
        CloseHandle(hOutputReadTmp);
        hOutputReadTmp = nullptr;
    }

    HANDLE hInputRead, hInputWriteTmp;
    // Create a pipe for the child process's STDIN.
    if (!CreatePipe(&hInputRead, &hInputWriteTmp, &sa, 4096)) {
        OC_WARNING("[RuntimeCompiler] Failed to create input pipes\n");
        exit_func();
        return;
    }
    startupInfo.hStdInput = hInputRead;

    // Create new output read handle and the input write handles. Set
    // the Properties to FALSE. Otherwise, the child inherits the
    // properties and, as a result, non-closeable handles to the pipes
    // are created.
    if (startupInfo.hStdOutput && !DuplicateHandle(GetCurrentProcess(), hInputWriteTmp,
                                                   GetCurrentProcess(),
                                                   &input_write_,// Address of new handle.
                                                   0, FALSE,     // Make it uninheritable.
                                                   DUPLICATE_SAME_ACCESS)) {
        OC_WARNING("[RuntimeCompiler] Failed to duplicate input write pipe\n");
        exit_func();
        return;
    }

    const wchar_t *pCommandLine = L"cmd /q /K @PROMPT $";
    //CreateProcessW won't accept a const pointer, so copy to an array
    wchar_t pCmdLineNonConst[1024];
    wcscpy_s(pCmdLineNonConst, pCommandLine);
    CreateProcessW(
        nullptr,         //__in_opt     LPCTSTR lpApplicationName,
        pCmdLineNonConst,//__inout_opt  LPTSTR lpCommandLine,
        nullptr,         //__in_opt     LPSECURITY_ATTRIBUTES lpProcessAttributes,
        nullptr,         //__in_opt     LPSECURITY_ATTRIBUTES lpThreadAttributes,
        TRUE,            //__in         BOOL bInheritHandles,
        0,               //__in         DWORD dwCreationFlags,
        nullptr,         //__in_opt     LPVOID lpEnvironment,
        nullptr,         //__in_opt     LPCTSTR lpCurrentDirectory,
        &startupInfo,    //__in         LPSTARTUPINFO lpStartupInfo,
        &process_info_   //__out        LPPROCESS_INFORMATION lpProcessInformation
    );

    //launch threaded read.
    output_thread_ = std::thread(&CmdProcess::read_output_thread, this);
    output_thread_.detach();

    exit_func();
}

void CmdProcess::write_input(std::string input, const callback_t &callback) const {
    DWORD nBytesWritten;
    input = add_flag(input);
    auto length = static_cast<DWORD>(input.length());
    with_lock([&] {
        cmd_queue_.emplace(input, callback);
    });
    WriteFile(input_write_, input.c_str(), length, &nBytesWritten, nullptr);
}

void CmdProcess::cleanup_process() {
    if (process_info_.hProcess) {
        TerminateProcess(process_info_.hProcess, 0);
        TerminateThread(process_info_.hThread, 0);
        CloseHandle(process_info_.hThread);
        ZeroMemory(&process_info_, sizeof(process_info_));
        CloseHandle(input_write_);
        input_write_ = nullptr;
        CloseHandle(output_read_);
        output_read_ = nullptr;
    }
}

CmdProcess::~CmdProcess() {
    cleanup_process();
}
}// namespace vision::inline hotfix