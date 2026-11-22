// STARTUPINFOEXW and the process attribute lists need Windows Vista's API.
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "runtime/process_tree.hpp"

#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>

#include <vector>
#endif

namespace rl::runtime {

#ifdef Q_OS_WIN
namespace {

// ProcThreadAttributeValue(ProcThreadAttributeJobList, FALSE, TRUE, FALSE),
// Windows 10 and later; older MinGW headers do not name it.
constexpr DWORD_PTR kJobListAttribute = 0x0002000D;
constexpr DWORD kExtendedStartupInfoPresent = 0x00080000;
// The exit code QProcess::kill() gives, which QProcess reports as a crash.
constexpr UINT kKilledExitCode = 0xf291;

}  // namespace

struct ProcessTree::Native {
    HANDLE job = nullptr;
    HANDLE jobList[1] = {nullptr};
    // The extended startup information CreateProcess reads; it lives until
    // QProcess::start() returns.
    STARTUPINFOEXW startup{};
    std::vector<char> attributeBuffer;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    bool inJob = false;

    void releaseAttributes() {
        if (attributes) {
            DeleteProcThreadAttributeList(attributes);
            attributes = nullptr;
        }
    }
};
#else
struct ProcessTree::Native {};
#endif

ProcessTree::ProcessTree() : native_(new Native) {
#ifdef Q_OS_WIN
    native_->job = CreateJobObjectW(nullptr, nullptr);
    if (native_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(native_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(native_->job);
            native_->job = nullptr;
        }
    }
#endif
}

ProcessTree::~ProcessTree() {
#ifdef Q_OS_WIN
    native_->releaseAttributes();
    if (native_->job) {
        // KILL_ON_JOB_CLOSE: whatever still runs in the tree ends here.
        CloseHandle(native_->job);
    }
#endif
}

void ProcessTree::prepare(QProcess* process) {
#ifdef Q_OS_WIN
    Native* native = native_.get();
    if (!native->job || !process) {
        return;
    }
    native->releaseAttributes();
    native->inJob = false;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    if (size == 0) {
        return;
    }
    native->attributeBuffer.assign(size, 0);
    native->attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(native->attributeBuffer.data());
    if (!InitializeProcThreadAttributeList(native->attributes, 1, 0, &size)) {
        native->attributes = nullptr;
        return;
    }
    native->jobList[0] = native->job;
    if (!UpdateProcThreadAttribute(native->attributes, 0, kJobListAttribute, native->jobList, sizeof(native->jobList),
                                   nullptr, nullptr)) {
        // Before Windows 10: started() puts the program into the job.
        native->releaseAttributes();
        return;
    }
    // QProcess calls this from within start(), just before CreateProcess:
    // its STARTUPINFOW (with the redirected standard handles) is copied into
    // the extended one that carries the job.
    process->setCreateProcessArgumentsModifier([native](QProcess::CreateProcessArguments* arguments) {
        if (!native->attributes) {
            return;
        }
        native->startup = STARTUPINFOEXW{};
        native->startup.StartupInfo = *arguments->startupInfo;
        native->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        native->startup.lpAttributeList = native->attributes;
        arguments->startupInfo = &native->startup.StartupInfo;
        arguments->flags |= kExtendedStartupInfoPresent;
        native->inJob = true;
    });
#else
    Q_UNUSED(process);
#endif
}

void ProcessTree::started(QProcess* process) {
#ifdef Q_OS_WIN
    Native* native = native_.get();
    if (!process) {
        return;
    }
    process->setCreateProcessArgumentsModifier({});
    native->releaseAttributes();
    if (!native->job || native->inJob || process->state() == QProcess::NotRunning) {
        return;
    }
    // Joined right after it started: a Python still starting up has not
    // started anything of its own yet.
    const HANDLE handle =
        OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process->processId()));
    if (handle) {
        native->inJob = AssignProcessToJobObject(native->job, handle) != FALSE;
        CloseHandle(handle);
    }
#else
    Q_UNUSED(process);
#endif
}

void ProcessTree::terminate() {
#ifdef Q_OS_WIN
    if (native_->job) {
        TerminateJobObject(native_->job, kKilledExitCode);
    }
#endif
}

}  // namespace rl::runtime
