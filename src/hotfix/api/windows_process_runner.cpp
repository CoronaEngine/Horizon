#include "horizon/hotfix/windows_process_runner.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace horizon::hotfix {
namespace {

[[noreturn]] void win32_error(const char *operation, DWORD code = GetLastError()) {
    throw std::system_error(static_cast<int>(code), std::system_category(), operation);
}

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};

using Handle = std::unique_ptr<void, HandleCloser>;

struct OutputPipe {
    Handle reader;
    Handle writer;
};

OutputPipe make_output_pipe() {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE reader{};
    HANDLE writer{};
    if (!CreatePipe(&reader, &writer, &security, 64 * 1024)) {
        win32_error("CreatePipe");
    }
    OutputPipe pipe{Handle{reader}, Handle{writer}};
    if (!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) {
        win32_error("SetHandleInformation");
    }
    return pipe;
}

Handle make_closed_input() {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        win32_error("Open NUL for child stdin");
    }
    return Handle{input};
}

struct AttributeListDeleter {
    void operator()(LPPROC_THREAD_ATTRIBUTE_LIST list) const noexcept {
        DeleteProcThreadAttributeList(list);
    }
};

class InheritedHandles {
public:
    explicit InheritedHandles(std::array<HANDLE, 3> handles)
        : handles_(handles) {
        SIZE_T size{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size == 0) {
            win32_error("Get process attribute list size");
        }
        storage_.resize(size);
        auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            win32_error("InitializeProcThreadAttributeList");
        }
        attributes_.reset(list);
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles_.data(), sizeof(handles_), nullptr, nullptr)) {
            win32_error("Set child handle list");
        }
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return attributes_.get();
    }

private:
    std::array<HANDLE, 3> handles_;
    std::vector<std::byte> storage_;
    std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, AttributeListDeleter> attributes_;
};

std::wstring utf16(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    if (input.find('\0') != input.npos || input.size() > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("Invalid process argument or environment string");
    }
    const auto size = static_cast<int>(input.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), size, nullptr, 0);
    if (length == 0) {
        win32_error("Decode UTF-8 process request");
    }
    std::wstring value(length, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), size, value.data(), length)) {
        win32_error("Decode UTF-8 process request");
    }
    return value;
}

std::filesystem::path absolute_path(const std::filesystem::path &path) {
    if (path.native().find(L'\0') != std::wstring::npos) {
        throw std::invalid_argument("Process path contains a NUL character");
    }
    return std::filesystem::absolute(path);
}

void append_argument(std::wstring &line, std::wstring_view argument) {
    // Windows CRT parsing treats backslashes specially only before a quote.
    line += L" \"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        line.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        line.push_back(character);
        backslashes = 0;
    }
    line.append(backslashes * 2, L'\\');
    line.push_back(L'"');
}

std::wstring make_command_line(const std::filesystem::path &executable,
                               const std::vector<std::string> &arguments) {
    // argv[0] uses the CRT's separate executable-name parsing rules.
    if (executable.native().find(L'"') != std::wstring::npos) {
        throw std::invalid_argument("Executable path contains a quote");
    }
    std::wstring line = L"\"" + executable.native() + L"\"";
    for (const auto &argument : arguments) {
        append_argument(line, utf16(argument));
    }
    if (line.size() >= 32767) {
        throw std::invalid_argument("Windows process command line exceeds 32767 characters including NUL");
    }
    return line;
}

struct EnvironmentBlockDeleter {
    void operator()(wchar_t *block) const noexcept { FreeEnvironmentStringsW(block); }
};

struct EnvironmentNameLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const noexcept {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};

std::wstring make_environment(const std::map<std::string, std::string> &overrides) {
    if (overrides.empty()) {
        return {};
    }
    std::unique_ptr<wchar_t, EnvironmentBlockDeleter> inherited{GetEnvironmentStringsW()};
    if (!inherited) {
        win32_error("GetEnvironmentStringsW");
    }
    std::map<std::wstring, std::wstring, EnvironmentNameLess> entries;
    for (const wchar_t *next = inherited.get(); *next != L'\0';) {
        const std::wstring_view entry{next};
        next += entry.size() + 1;
        // Hidden drive entries such as =C:=C:\dir have an initial equals sign.
        const auto separator = entry.find(L'=', entry.starts_with(L'=') ? 1 : 0);
        if (separator != entry.npos) {
            entries.emplace(entry.substr(0, separator), entry.substr(separator + 1));
        }
    }
    for (const auto &[name, value] : overrides) {
        const auto key = utf16(name);
        if (key.empty() || key.find(L'=') != key.npos) {
            throw std::invalid_argument("Invalid Windows environment variable name");
        }
        entries.insert_or_assign(key, utf16(value));
    }

    std::wstring block;
    for (const auto &[name, value] : entries) {
        block.append(name).push_back(L'=');
        block.append(value).push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool drain_available(HANDLE reader, std::string &output, const char *operation) {
    DWORD available{};
    if (!PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            return false;
        }
        win32_error(operation, error);
    }
    if (available == 0) {
        return false;
    }

    // This thread is the only reader of this pipe. Read only the bytes that
    // were already queued so a writer that stays open cannot block us.
    std::array<char, 8192> buffer;
    while (available != 0) {
        DWORD count{};
        const auto size = std::min(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(reader, buffer.data(), size, &count, nullptr)) {
            win32_error(operation);
        }
        if (count == 0) {
            break;
        }
        output.append(buffer.data(), count);
        available -= count;
    }
    return true;
}

void capture_output(HANDLE process, HANDLE stdout_reader, HANDLE stderr_reader, ProcessResult &result) {
    for (;;) {
        const bool stdout_read = drain_available(stdout_reader, result.stdout_text, "Read child stdout");
        const bool stderr_read = drain_available(stderr_reader, result.stderr_text, "Read child stderr");
        const DWORD wait = WaitForSingleObject(process, stdout_read || stderr_read ? 0 : 5);
        if (wait == WAIT_OBJECT_0) {
            // Capture the direct child's final buffered output. A descendant
            // may keep the pipe open, so completion does not depend on EOF.
            drain_available(stdout_reader, result.stdout_text, "Read child stdout");
            drain_available(stderr_reader, result.stderr_text, "Read child stderr");
            return;
        }
        if (wait != WAIT_TIMEOUT) {
            win32_error("Wait for child process");
        }
    }
}

void run_process(const ProcessRequest &request, ProcessResult &result) {
    if (request.executable.empty()) {
        throw std::invalid_argument("A process executable is required");
    }
    const auto executable = absolute_path(request.executable);
    const auto directory = request.working_directory.empty()
                               ? std::filesystem::path{}
                               : absolute_path(request.working_directory);
    auto command_line = make_command_line(executable, request.arguments);
    auto environment = make_environment(request.environment_overrides);

    auto stdout_pipe = make_output_pipe();
    auto stderr_pipe = make_output_pipe();
    auto stdin_handle = make_closed_input();
    InheritedHandles inherited{{stdin_handle.get(), stdout_pipe.writer.get(), stderr_pipe.writer.get()}};

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = stdin_handle.get();
    startup.StartupInfo.hStdOutput = stdout_pipe.writer.get();
    startup.StartupInfo.hStdError = stderr_pipe.writer.get();
    startup.lpAttributeList = inherited.get();

    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                        environment.empty() ? nullptr : environment.data(),
                        directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, &child)) {
        win32_error("CreateProcessW");
    }
    Handle process{child.hProcess};
    CloseHandle(child.hThread);
    stdout_pipe.writer.reset();
    stderr_pipe.writer.reset();
    stdin_handle.reset();

    try {
        capture_output(process.get(), stdout_pipe.reader.get(), stderr_pipe.reader.get(), result);
    } catch (...) {
        TerminateProcess(process.get(), 1);
        throw;
    }
    DWORD exit_code{};
    if (!GetExitCodeProcess(process.get(), &exit_code)) {
        win32_error("GetExitCodeProcess");
    }
    // Wait before querying the code so an actual exit code of 259 is valid.
    result.exit_code = std::bit_cast<std::int32_t>(exit_code);
}

}// namespace

ProcessResult WindowsProcessRunner::run(const ProcessRequest &request) {
    ProcessResult result;
    try {
        run_process(request, result);
    } catch (const std::system_error &error) {
        result.error_message = error.what();
    } catch (const std::invalid_argument &error) {
        result.error_message = error.what();
    }
    return result;
}

}// namespace horizon::hotfix
