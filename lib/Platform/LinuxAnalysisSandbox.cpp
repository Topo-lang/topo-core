#include "LinuxAnalysisSandbox.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace topo::platform::detail {
namespace {
// ABI 3 closes truncate(2), which otherwise modifies read-only inputs.
constexpr int minimumLandlockAbi = 3;
constexpr uint64_t truncateAccess = 1ULL << 14;
constexpr uint64_t filesystemAccess =
    LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
    LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
    LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
    LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER | truncateAccess;

bool failure(std::string& diagnostic, const std::string& operation) {
    diagnostic = operation + ": " + std::strerror(errno);
    return false;
}

class Descriptor {
public:
    explicit Descriptor(int fd) : fd_(fd) {}
    ~Descriptor() {
        if (fd_ >= 0) ::close(fd_);
    }
    int get() const { return fd_; }

private:
    int fd_;
};

bool grant(int ruleset, const char* path, uint64_t access, bool directory, std::string& diagnostic) {
    const Descriptor object(::open(path, O_PATH | O_CLOEXEC));
    if (object.get() < 0) return failure(diagnostic, std::string("open sandbox grant ") + path);
    struct stat status{};
    if (::fstat(object.get(), &status) != 0) return failure(diagnostic, "stat sandbox grant");
    if (directory != static_cast<bool>(S_ISDIR(status.st_mode))) {
        diagnostic = std::string("sandbox grant has incorrect object kind: ") + path;
        return false;
    }
    struct landlock_path_beneath_attr rule{};
    rule.allowed_access = access;
    rule.parent_fd = object.get();
    if (::syscall(SYS_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) != 0)
        return failure(diagnostic, std::string("install sandbox grant ") + path);
    return true;
}

bool installSeccomp(std::string& diagnostic) {
#if defined(__x86_64__)
    constexpr uint32_t expectedArchitecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr uint32_t expectedArchitecture = AUDIT_ARCH_AARCH64;
#else
    constexpr uint32_t expectedArchitecture = 0;
    diagnostic = "Linux analysis seccomp supports only x86_64 and aarch64";
    return false;
#endif
    std::vector<sock_filter> filters;
    auto statement = [&](uint16_t code, uint32_t value) {
        filters.push_back(BPF_STMT(code, value));
    };
    auto jump = [&](uint16_t code, uint32_t value, uint8_t yes, uint8_t no) {
        filters.push_back(BPF_JUMP(code, value, yes, no));
    };
    const uint32_t denied = SECCOMP_RET_ERRNO | EPERM;
    auto deny = [&](uint32_t call, uint32_t result = SECCOMP_RET_ERRNO | EPERM) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, call, 0, 1);
        statement(BPF_RET | BPF_K, result);
    };
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
    jump(BPF_JMP | BPF_JEQ | BPF_K, expectedArchitecture, 1, 0);
    statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));
#if defined(__x86_64__)
    // x32 shares the audit architecture but uses a distinct syscall ABI.
    jump(BPF_JMP | BPF_JSET | BPF_K, 0x40000000U, 0, 1);
    statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif

    // clone3 keeps its flags in userspace memory, which classic BPF cannot
    // inspect. ENOSYS asks glibc to use clone, whose scalar flags are checked.
#ifdef SYS_clone3
    deny(SYS_clone3, SECCOMP_RET_ERRNO | ENOSYS);
#endif
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone, 0, 11);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]) + 4);
    jump(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0);
    statement(BPF_RET | BPF_K, denied);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
    constexpr uint32_t requiredThread = CLONE_THREAD | CLONE_VM | CLONE_SIGHAND;
    constexpr uint32_t allowedThread = requiredThread | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM | CLONE_SETTLS |
                                       CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID;
    jump(BPF_JMP | BPF_JSET | BPF_K, ~allowedThread, 0, 1);
    statement(BPF_RET | BPF_K, denied);
    statement(BPF_ALU | BPF_AND | BPF_K, requiredThread);
    jump(BPF_JMP | BPF_JEQ | BPF_K, requiredThread, 1, 0);
    statement(BPF_RET | BPF_K, denied);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    // Reload the syscall number for the non-clone branch.
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));

    // Use a syscall allowlist so future Linux interfaces cannot silently gain
    // filesystem, process or IPC authority. Landlock checks all path operations
    // below. In particular, metadata changes (chmod/chown/utime/xattrs), network,
    // fork, namespace changes, ptrace, SysV IPC and io_uring stay denied.
    auto allow = [&](uint32_t call) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, call, 0, 1);
        statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    };
    // Signals and resource-limit changes may address only this process.
    for (const uint32_t call : {SYS_kill, SYS_tgkill, SYS_rt_sigqueueinfo, SYS_rt_tgsigqueueinfo}) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, call, 0, 4);
        statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
        jump(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(::getpid()), 1, 0);
        statement(BPF_RET | BPF_K, denied);
        statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    // libc uses prlimit64(pid=0) for self. Handle it separately from signals:
    // kill(0) would address the entire process group.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_prlimit64, 0, 5);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
    jump(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(::getpid()), 2, 0);
    jump(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0);
    statement(BPF_RET | BPF_K, denied);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    // PR_SET_NAME is needed by runtimes; the remaining prctl operations may
    // affect process security state and are denied.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_prctl, 0, 5);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
    jump(BPF_JMP | BPF_JEQ | BPF_K, PR_SET_NAME, 2, 0);
    jump(BPF_JMP | BPF_JEQ | BPF_K, PR_GET_NAME, 1, 0);
    statement(BPF_RET | BPF_K, denied);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    // Restrict fcntl to descriptor-local operations. F_SETOWN can arrange
    // signals to unrelated processes, and advisory locking is not required.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_fcntl, 0, 14);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[1]));
    for (const uint32_t command : {F_DUPFD, F_DUPFD_CLOEXEC, F_GETFD, F_SETFD, F_GETFL, F_SETFL}) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, command, 0, 1);
        statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    statement(BPF_RET | BPF_K, denied);
#ifdef SYS_read
    allow(SYS_read);
#endif
#ifdef SYS_arch_prctl
    // Dynamic ELF startup installs thread-local storage with ARCH_SET_FS.
    // Keep this architecture interface limited to process-local FS/GS bases.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_arch_prctl, 0, 10);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
    for (const uint32_t command : {0x1001U, 0x1002U, 0x1003U, 0x1004U}) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, command, 0, 1);
        statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    statement(BPF_RET | BPF_K, denied);
#endif
#ifdef SYS_lseek
    allow(SYS_lseek);
#endif
#ifdef SYS_getcpu
    allow(SYS_getcpu);
#endif
#ifdef SYS_write
    allow(SYS_write);
#endif
#ifdef SYS_readv
    allow(SYS_readv);
#endif
#ifdef SYS_writev
    allow(SYS_writev);
#endif
#ifdef SYS_pread64
    allow(SYS_pread64);
#endif
#ifdef SYS_pwrite64
    allow(SYS_pwrite64);
#endif
#ifdef SYS_preadv
    allow(SYS_preadv);
#endif
#ifdef SYS_pwritev
    allow(SYS_pwritev);
#endif
#ifdef SYS_preadv2
    allow(SYS_preadv2);
#endif
#ifdef SYS_pwritev2
    allow(SYS_pwritev2);
#endif
#ifdef SYS_sendfile
    allow(SYS_sendfile);
#endif
#ifdef SYS_copy_file_range
    allow(SYS_copy_file_range);
#endif
#ifdef SYS_splice
    allow(SYS_splice);
#endif
#ifdef SYS_vmsplice
    allow(SYS_vmsplice);
#endif
#ifdef SYS_tee
    allow(SYS_tee);
#endif
#ifdef SYS_open
    allow(SYS_open);
#endif
#ifdef SYS_openat
    allow(SYS_openat);
#endif
#ifdef SYS_openat2
    allow(SYS_openat2);
#endif
#ifdef SYS_creat
    allow(SYS_creat);
#endif
#ifdef SYS_truncate
    allow(SYS_truncate);
#endif
#ifdef SYS_ftruncate
    allow(SYS_ftruncate);
#endif
#ifdef SYS_close
    allow(SYS_close);
#endif
#ifdef SYS_close_range
    allow(SYS_close_range);
#endif
#ifdef SYS_fsync
    allow(SYS_fsync);
#endif
#ifdef SYS_fdatasync
    allow(SYS_fdatasync);
#endif
#ifdef SYS_sync_file_range
    allow(SYS_sync_file_range);
#endif
#ifdef SYS_stat
    allow(SYS_stat);
#endif
#ifdef SYS_lstat
    allow(SYS_lstat);
#endif
#ifdef SYS_fstat
    allow(SYS_fstat);
#endif
#ifdef SYS_newfstatat
    allow(SYS_newfstatat);
#endif
#ifdef SYS_statx
    allow(SYS_statx);
#endif
#ifdef SYS_access
    allow(SYS_access);
#endif
#ifdef SYS_faccessat
    allow(SYS_faccessat);
#endif
#ifdef SYS_faccessat2
    allow(SYS_faccessat2);
#endif
#ifdef SYS_getdents
    allow(SYS_getdents);
#endif
#ifdef SYS_getdents64
    allow(SYS_getdents64);
#endif
#ifdef SYS_readlink
    allow(SYS_readlink);
#endif
#ifdef SYS_readlinkat
    allow(SYS_readlinkat);
#endif
#ifdef SYS_getcwd
    allow(SYS_getcwd);
#endif
#ifdef SYS_chdir
    allow(SYS_chdir);
#endif
#ifdef SYS_fchdir
    allow(SYS_fchdir);
#endif
#ifdef SYS_mkdir
    allow(SYS_mkdir);
#endif
#ifdef SYS_mkdirat
    allow(SYS_mkdirat);
#endif
#ifdef SYS_rmdir
    allow(SYS_rmdir);
#endif
#ifdef SYS_unlink
    allow(SYS_unlink);
#endif
#ifdef SYS_unlinkat
    allow(SYS_unlinkat);
#endif
#ifdef SYS_rename
    allow(SYS_rename);
#endif
#ifdef SYS_renameat
    allow(SYS_renameat);
#endif
#ifdef SYS_renameat2
    allow(SYS_renameat2);
#endif
#ifdef SYS_link
    allow(SYS_link);
#endif
#ifdef SYS_linkat
    allow(SYS_linkat);
#endif
#ifdef SYS_symlink
    allow(SYS_symlink);
#endif
#ifdef SYS_symlinkat
    allow(SYS_symlinkat);
#endif
#ifdef SYS_mmap
    allow(SYS_mmap);
#endif
#ifdef SYS_mremap
    allow(SYS_mremap);
#endif
#ifdef SYS_munmap
    allow(SYS_munmap);
#endif
#ifdef SYS_mprotect
    allow(SYS_mprotect);
#endif
#ifdef SYS_msync
    allow(SYS_msync);
#endif
#ifdef SYS_madvise
    allow(SYS_madvise);
#endif
#ifdef SYS_mlock
    allow(SYS_mlock);
#endif
#ifdef SYS_munlock
    allow(SYS_munlock);
#endif
#ifdef SYS_mincore
    allow(SYS_mincore);
#endif
#ifdef SYS_brk
    allow(SYS_brk);
#endif
#ifdef SYS_rt_sigaction
    allow(SYS_rt_sigaction);
#endif
#ifdef SYS_rt_sigprocmask
    allow(SYS_rt_sigprocmask);
#endif
#ifdef SYS_rt_sigpending
    allow(SYS_rt_sigpending);
#endif
#ifdef SYS_rt_sigreturn
    allow(SYS_rt_sigreturn);
#endif
#ifdef SYS_sigaltstack
    allow(SYS_sigaltstack);
#endif
#ifdef SYS_rt_sigtimedwait
    allow(SYS_rt_sigtimedwait);
#endif
#ifdef SYS_rt_sigsuspend
    allow(SYS_rt_sigsuspend);
#endif
#ifdef SYS_nanosleep
    allow(SYS_nanosleep);
#endif
#ifdef SYS_clock_nanosleep
    allow(SYS_clock_nanosleep);
#endif
#ifdef SYS_clock_gettime
    allow(SYS_clock_gettime);
#endif
#ifdef SYS_clock_getres
    allow(SYS_clock_getres);
#endif
#ifdef SYS_gettimeofday
    allow(SYS_gettimeofday);
#endif
#ifdef SYS_time
    allow(SYS_time);
#endif
#ifdef SYS_times
    allow(SYS_times);
#endif
#ifdef SYS_getrusage
    allow(SYS_getrusage);
#endif
#ifdef SYS_uname
    allow(SYS_uname);
#endif
#ifdef SYS_sysinfo
    allow(SYS_sysinfo);
#endif
#ifdef SYS_getpid
    allow(SYS_getpid);
#endif
#ifdef SYS_getppid
    allow(SYS_getppid);
#endif
#ifdef SYS_gettid
    allow(SYS_gettid);
#endif
#ifdef SYS_getuid
    allow(SYS_getuid);
#endif
#ifdef SYS_getgid
    allow(SYS_getgid);
#endif
#ifdef SYS_geteuid
    allow(SYS_geteuid);
#endif
#ifdef SYS_getegid
    allow(SYS_getegid);
#endif
#ifdef SYS_getresuid
    allow(SYS_getresuid);
#endif
#ifdef SYS_getresgid
    allow(SYS_getresgid);
#endif
#ifdef SYS_getgroups
    allow(SYS_getgroups);
#endif
#ifdef SYS_getrlimit
    allow(SYS_getrlimit);
#endif
#ifdef SYS_setrlimit
    allow(SYS_setrlimit);
#endif
#ifdef SYS_futex
    allow(SYS_futex);
#endif
#ifdef SYS_futex_waitv
    allow(SYS_futex_waitv);
#endif
#ifdef SYS_set_tid_address
    allow(SYS_set_tid_address);
#endif
#ifdef SYS_set_robust_list
    allow(SYS_set_robust_list);
#endif
#ifdef SYS_rseq
    allow(SYS_rseq);
#endif
#ifdef SYS_restart_syscall
    allow(SYS_restart_syscall);
#endif
#ifdef SYS_sched_yield
    allow(SYS_sched_yield);
#endif
#ifdef SYS_sched_getparam
    allow(SYS_sched_getparam);
#endif
#ifdef SYS_sched_getscheduler
    allow(SYS_sched_getscheduler);
#endif
#ifdef SYS_sched_get_priority_max
    allow(SYS_sched_get_priority_max);
#endif
#ifdef SYS_sched_get_priority_min
    allow(SYS_sched_get_priority_min);
#endif
#ifdef SYS_sched_getaffinity
    allow(SYS_sched_getaffinity);
#endif
#ifdef SYS_poll
    allow(SYS_poll);
#endif
#ifdef SYS_ppoll
    allow(SYS_ppoll);
#endif
#ifdef SYS_select
    allow(SYS_select);
#endif
#ifdef SYS_pselect6
    allow(SYS_pselect6);
#endif
#ifdef SYS_epoll_create
    allow(SYS_epoll_create);
#endif
#ifdef SYS_epoll_create1
    allow(SYS_epoll_create1);
#endif
#ifdef SYS_epoll_ctl
    allow(SYS_epoll_ctl);
#endif
#ifdef SYS_epoll_wait
    allow(SYS_epoll_wait);
#endif
#ifdef SYS_epoll_pwait
    allow(SYS_epoll_pwait);
#endif
#ifdef SYS_epoll_pwait2
    allow(SYS_epoll_pwait2);
#endif
#ifdef SYS_eventfd
    allow(SYS_eventfd);
#endif
#ifdef SYS_eventfd2
    allow(SYS_eventfd2);
#endif
#ifdef SYS_pipe
    allow(SYS_pipe);
#endif
#ifdef SYS_pipe2
    allow(SYS_pipe2);
#endif
#ifdef SYS_dup
    allow(SYS_dup);
#endif
#ifdef SYS_dup2
    allow(SYS_dup2);
#endif
#ifdef SYS_dup3
    allow(SYS_dup3);
#endif
#ifdef SYS_getrandom
    allow(SYS_getrandom);
#endif
#ifdef SYS_execve
    allow(SYS_execve);
#endif
#ifdef SYS_exit
    allow(SYS_exit);
#endif
#ifdef SYS_exit_group
    allow(SYS_exit_group);
#endif
    statement(BPF_RET | BPF_K, denied);
    struct sock_fprog program{};
    program.len = static_cast<unsigned short>(filters.size());
    program.filter = filters.data();
    if (::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
        return failure(diagnostic, "install analysis seccomp filter");
    return true;
}
} // namespace

int linuxAnalysisLandlockAbi(std::string& diagnostic) {
#if !defined(__x86_64__) && !defined(__aarch64__)
    diagnostic = "Linux analysis seccomp supports only x86_64 and aarch64";
    return 0;
#endif
    const auto abi = ::syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < minimumLandlockAbi) {
        diagnostic = abi < 0 ? std::string("Landlock is unavailable: ") + std::strerror(errno)
                             : "Landlock ABI 3 or newer is required for truncate confinement";
        return 0;
    }
    return static_cast<int>(abi);
}

bool establishLinuxAnalysisSandbox(int argc, char** argv, int& targetIndex, std::string& diagnostic) {
    // Landlock does not revoke authority carried by already-open descriptors.
    // The parent capture layer maps stdin/stdout/stderr, and every other
    // inherited descriptor must be closed before policy grants are opened.
#ifdef SYS_close_range
    if (::syscall(SYS_close_range, 3U, std::numeric_limits<unsigned>::max(), 0) != 0)
        return failure(diagnostic, "close inherited analysis descriptors");
#else
    diagnostic = "Linux analysis requires close_range to close inherited descriptors";
    return false;
#endif
    if (!linuxAnalysisLandlockAbi(diagnostic)) return false;
    struct landlock_ruleset_attr configuration{};
    configuration.handled_access_fs = filesystemAccess;
    const Descriptor ruleset(
        static_cast<int>(::syscall(SYS_landlock_create_ruleset, &configuration, sizeof(configuration), 0)));
    if (ruleset.get() < 0) return failure(diagnostic, "create analysis Landlock ruleset");
    targetIndex = 0;
    for (int i = 1; i < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag == "--") {
            targetIndex = i + 1;
            break;
        }
        if (i + 1 >= argc) {
            diagnostic = "missing sandbox grant path";
            return false;
        }
        bool directory = false;
        uint64_t access = LANDLOCK_ACCESS_FS_READ_FILE;
        if (flag == "--read-directory") {
            directory = true;
            access |= LANDLOCK_ACCESS_FS_READ_DIR;
        } else if (flag == "--read-file") {
        } else if (flag == "--execute") {
            access |= LANDLOCK_ACCESS_FS_EXECUTE;
        } else if (flag == "--temporary") {
            directory = true;
            access = filesystemAccess & ~(LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_MAKE_CHAR |
                                          LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SOCK);
        } else if (flag == "--null-device") {
            access |= LANDLOCK_ACCESS_FS_WRITE_FILE;
        } else {
            diagnostic = "unknown sandbox grant";
            return false;
        }
        if (!grant(ruleset.get(), argv[i + 1], access, directory, diagnostic)) return false;
    }
    if (targetIndex <= 0 || targetIndex >= argc) {
        diagnostic = "missing sandbox target";
        return false;
    }
    // These are the launcher's own proc objects, never /proc as a directory or
    // a parent-resolved /proc/self path. Their inode grants survive exec.
    for (const char* path : {"/proc/self/maps", "/proc/self/stat", "/proc/self/status"}) {
        if (!grant(ruleset.get(), path, LANDLOCK_ACCESS_FS_READ_FILE, false, diagnostic)) return false;
    }
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return failure(diagnostic, "set no-new-privileges");
    if (::syscall(SYS_landlock_restrict_self, ruleset.get(), 0) != 0)
        return failure(diagnostic, "apply analysis Landlock ruleset");
    return installSeccomp(diagnostic);
}
} // namespace topo::platform::detail
