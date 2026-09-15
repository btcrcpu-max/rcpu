// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <common/system.h>

#include <logging.h>
#include <util/string.h>
#include <util/time.h>

#ifndef WIN32
#include <sys/stat.h>
#else
#include <compat/compat.h>
#include <codecvt>
#endif

#ifdef HAVE_MALLOPT_ARENA_MAX
#include <malloc.h>
#endif

#include <cstdlib>
#include <locale>
#include <stdexcept>
#include <string>
#include <thread>

// Application startup time (used for uptime calculation)
const int64_t nStartupTime = GetTime();

#ifndef WIN32
std::string ShellEscape(const std::string& arg)
{
    std::string escaped = arg;
    ReplaceAll(escaped, "'", "'\"'\"'");
    return "'" + escaped + "'";
}
#endif

#if HAVE_SYSTEM

#ifndef WIN32
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <wordexp.h>
#else
#include <windows.h>
#endif

// !RCPU FIX H-2: Replace system() with fork+execvp (POSIX) / CreateProcess (Win).
// The original implementation passed the raw command string to system(), which
// invokes /bin/sh -c <string>. If the configuration source is compromised (e.g.
// rcpu.conf is writable by an attacker, or a wallet notification string contains
// shell metacharacters), this leads to arbitrary command execution.
// The new implementation tokenizes the command string and executes the program
// directly via execvp, bypassing the shell entirely. This eliminates shell
// interpretation of metacharacters (;, |, &&, $(), backticks, etc.).
void runCommand(const std::string& strCommand)
{
    if (strCommand.empty()) return;

#ifndef WIN32
    // Tokenize the command string on whitespace into argv.
    // This intentionally does NOT support shell features (pipes, redirects,
    // variable expansion). If the user needs those, they should write a script
    // and invoke the interpreter explicitly (e.g. /bin/sh /path/to/script.sh).
    std::vector<std::string> tokens;
    {
        std::string current;
        bool in_single = false, in_double = false;
        for (size_t i = 0; i < strCommand.size(); ++i) {
            char ch = strCommand[i];
            if (ch == '\'' && !in_double) { in_single = !in_single; continue; }
            if (ch == '"' && !in_single) { in_double = !in_double; continue; }
            if (!in_single && !in_double && (ch == ' ' || ch == '\t' || ch == '\n')) {
                if (!current.empty()) { tokens.push_back(std::move(current)); current.clear(); }
                continue;
            }
            current += ch;
        }
        if (!current.empty()) tokens.push_back(std::move(current));
    }

    if (tokens.empty()) return;

    std::vector<const char*> argv;
    argv.reserve(tokens.size() + 1);
    for (const auto& t : tokens) {
        argv.push_back(t.c_str());
    }
    argv.push_back(nullptr); // execvp expects a NULL-terminated array

    pid_t pid = fork();
    if (pid == -1) {
        LogPrintf("runCommand error: fork() failed: %s\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        // Child process: exec the program directly, no shell.
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        // If execvp returns, it failed.
        _exit(127);
    }

    // Parent: wait for child to finish.
    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        LogPrintf("runCommand error: waitpid() failed for '%s'\n", strCommand);
        return;
    }

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exit_code != 0) {
        LogPrintf("runCommand: '%s' exited with code %d\n", argv[0], exit_code);
    }
#else
    // Windows: use CreateProcessW for direct execution without cmd.exe.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // CreateProcessW can take a command line string directly. We pass the
    // program name as the first token and the remainder as lpCommandLine.
    // CreateProcessW does NOT invoke cmd.exe, so shell metacharacters
    // (^, &, |, >) are NOT interpreted.
    std::wstring wcmd = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t>()
        .from_bytes(strCommand);

    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        LogPrintf("runCommand error: CreateProcessW failed (error %lu)\n",
                  GetLastError());
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        LogPrintf("runCommand: exited with code %lu\n", exit_code);
    }
#endif
}
#endif

void SetupEnvironment()
{
#ifdef HAVE_MALLOPT_ARENA_MAX
    // glibc-specific: On 32-bit systems set the number of arenas to 1.
    // By default, since glibc 2.10, the C library will create up to two heap
    // arenas per core. This is known to cause excessive virtual address space
    // usage in our usage. Work around it by setting the maximum number of
    // arenas to 1.
    if (sizeof(void*) == 4) {
        mallopt(M_ARENA_MAX, 1);
    }
#endif
    // On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
    // may be invalid, in which case the "C.UTF-8" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
    try {
        std::locale(""); // Raises a runtime error if current locale is invalid
    } catch (const std::runtime_error&) {
        setenv("LC_ALL", "C.UTF-8", 1);
    }
#elif defined(WIN32)
    // Set the default input/output charset is utf-8
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

#ifndef WIN32
    constexpr mode_t private_umask = 0077;
    umask(private_umask);
#endif
}

bool SetupNetworking()
{
#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion ) != 2 || HIBYTE(wsadata.wVersion) != 2)
        return false;
#endif
    return true;
}

int GetNumCores()
{
    return std::thread::hardware_concurrency();
}

// Obtain the application startup time (used for uptime calculation)
int64_t GetStartupTime()
{
    return nStartupTime;
}
