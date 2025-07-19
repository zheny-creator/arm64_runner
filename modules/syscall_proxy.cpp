#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>
#include <set>
#include <cstdio>
#include <unordered_map>
#include "syscall_proxy.h"
#include "elf_loader.h"
extern ElfFile g_elf;
extern int debug_enabled;

// --- СТАТИЧЕСКИЙ СЛОВАРЬ СООТВЕТСТВИЯ ARM64 -> x86-64 ---
static const std::unordered_map<long, long> arm64_to_x86_64_syscall = {
    {0, 206}, // io_setup
    {1, 207}, // io_destroy
    {2, 209}, // io_submit
    {3, 210}, // io_cancel
    {5, 188}, // setxattr
    {6, 189}, // lsetxattr
    {7, 190}, // fsetxattr
    {8, 191}, // getxattr
    {9, 192}, // lgetxattr
    {10, 193}, // fgetxattr
    {11, 194}, // listxattr
    {12, 195}, // llistxattr
    {13, 196}, // flistxattr
    {14, 197}, // removexattr
    {15, 198}, // lremovexattr
    {16, 199}, // fremovexattr
    {17, 79}, // getcwd
    {19, 290}, // eventfd2
    {20, 291}, // epoll_create1
    {21, 233}, // epoll_ctl
    {22, 281}, // epoll_pwait
    {23, 32}, // dup
    {24, 292}, // dup3
    {26, 294}, // inotify_init1
    {27, 254}, // inotify_add_watch
    {28, 255}, // inotify_rm_watch
    {29, 16}, // ioctl
    {30, 251}, // ioprio_set
    {31, 252}, // ioprio_get
    {32, 73}, // flock
    {33, 259}, // mknodat
    {34, 258}, // mkdirat
    {35, 263}, // unlinkat
    {36, 266}, // symlinkat
    {37, 265}, // linkat
    {39, 166}, // umount2
    {40, 165}, // mount
    {41, 155}, // pivot_root
    {47, 285}, // fallocate
    {48, 269}, // faccessat
    {49, 80}, // chdir
    {50, 81}, // fchdir
    {51, 161}, // chroot
    {52, 91}, // fchmod
    {53, 268}, // fchmodat
    {54, 260}, // fchownat
    {55, 93}, // fchown
    {56, 257}, // openat
    {57, 3}, // close
    {58, 153}, // vhangup
    {59, 293}, // pipe2
    {60, 179}, // quotactl
    {61, 217}, // getdents64
    {63, 0}, // read
    {64, 1}, // write
    {65, 19}, // readv
    {66, 20}, // writev
    {67, 17}, // pread64
    {68, 18}, // pwrite64
    {69, 295}, // preadv
    {70, 296}, // pwritev
    {74, 289}, // signalfd4
    {75, 278}, // vmsplice
    {76, 275}, // splice
    {77, 276}, // tee
    {78, 267}, // readlinkat
    {81, 162}, // sync
    {82, 74}, // fsync
    {83, 75}, // fdatasync
    {84, 277}, // sync_file_range
    {85, 283}, // timerfd_create
    {89, 163}, // acct
    {90, 125}, // capget
    {91, 126}, // capset
    {92, 135}, // personality
    {93, 60}, // exit
    {94, 231}, // exit_group
    {95, 247}, // waitid
    {96, 218}, // set_tid_address
    {97, 272}, // unshare
    {99, 273}, // set_robust_list
    {100, 274}, // get_robust_list
    {101, 35}, // nanosleep
    {102, 36}, // getitimer
    {103, 38}, // setitimer
    {104, 246}, // kexec_load
    {105, 175}, // init_module
    {106, 176}, // delete_module
    {107, 222}, // timer_create
    {109, 225}, // timer_getoverrun
    {111, 226}, // timer_delete
    {116, 103}, // syslog
    {117, 101}, // ptrace
    {118, 142}, // sched_setparam
    {119, 144}, // sched_setscheduler
    {120, 145}, // sched_getscheduler
    {121, 143}, // sched_getparam
    {122, 203}, // sched_setaffinity
    {123, 204}, // sched_getaffinity
    {124, 24}, // sched_yield
    {125, 146}, // sched_get_priority_max
    {126, 147}, // sched_get_priority_min
    {128, 219}, // restart_syscall
    {129, 62}, // kill
    {130, 200}, // tkill
    {131, 234}, // tgkill
    {132, 131}, // sigaltstack
    {133, 130}, // rt_sigsuspend
    {134, 13}, // rt_sigaction
    {135, 14}, // rt_sigprocmask
    {136, 127}, // rt_sigpending
    {138, 129}, // rt_sigqueueinfo
    {139, 15}, // rt_sigreturn
    {140, 141}, // setpriority
    {141, 140}, // getpriority
    {142, 169}, // reboot
    {143, 114}, // setregid
    {144, 106}, // setgid
    {145, 113}, // setreuid
    {146, 105}, // setuid
    {147, 117}, // setresuid
    {148, 118}, // getresuid
    {149, 119}, // setresgid
    {150, 120}, // getresgid
    {151, 122}, // setfsuid
    {152, 123}, // setfsgid
    {153, 100}, // times
    {154, 154}, // setpgid
    {155, 121}, // getpgid
    {156, 124}, // getsid
    {157, 112}, // setsid
    {158, 115}, // getgroups
    {159, 116}, // setgroups
    {160, 63}, // uname
    {161, 170}, // sethostname
    {162, 171}, // setdomainname
    {163, 97}, // getrlimit
    {164, 160}, // setrlimit
    {165, 98}, // getrusage
    {166, 95}, // umask
    {167, 157}, // prctl
    {168, 309}, // getcpu
    {169, 96}, // gettimeofday
    {170, 164}, // settimeofday
    {172, 39}, // getpid
    {173, 110}, // getppid
    {174, 102}, // getuid
    {175, 107}, // geteuid
    {176, 104}, // getgid
    {177, 108}, // getegid
    {178, 186}, // gettid
    {179, 99}, // sysinfo
    {180, 240}, // mq_open
    {181, 241}, // mq_unlink
    {184, 244}, // mq_notify
    {185, 245}, // mq_getsetattr
    {186, 186}, // msgget
    {187, 71}, // msgctl
    {188, 70}, // msgrcv
    {189, 69}, // msgsnd
    {190, 64}, // semget
    {191, 66}, // semctl
    {192, 220}, // semtimedop
    {193, 193}, // semop
    {194, 194}, // shmget
    {195, 31}, // shmctl
    {196, 30}, // shmat
    {197, 67}, // shmdt
    {198, 41}, // socket
    {199, 53}, // socketpair
    {200, 49}, // bind
    {201, 50}, // listen
    {202, 43}, // accept
    {203, 42}, // connect
    {204, 51}, // getsockname
    {205, 52}, // getpeername
    {206, 44}, // sendto
    {207, 45}, // recvfrom
    {208, 54}, // setsockopt
    {209, 55}, // getsockopt
    {210, 48}, // shutdown
    {211, 46}, // sendmsg
    {212, 47}, // recvmsg
    {213, 187}, // readahead
    {214, 12}, // brk
    {215, 11}, // munmap
    {216, 25}, // mremap
    {217, 27}, // mincore
    {218, 28}, // madvise
    {219, 237}, // mbind
    {220, 238}, // set_mempolicy
    {221, 239}, // get_mempolicy
    {222, 9}, // mmap
    {224, 165}, // swapon
    {225, 166}, // swapoff
    {226, 10}, // mprotect
    {227, 227}, // msync
    {228, 149}, // mlock
    {229, 150}, // munlock
    {230, 151}, // mlockall
    {231, 152}, // munlockall
    {232, 232}, // mincore
    {233, 233}, // madvise
    {234, 216}, // remap_file_pages
    {235, 237}, // mbind
    {236, 239}, // get_mempolicy
    {237, 238}, // set_mempolicy
    {238, 256}, // migrate_pages
    {239, 279}, // move_pages
    {240, 240}, // rt_tgsigqueueinfo
    {241, 298}, // perf_event_open
    {242, 288}, // accept4
    {243, 299}, // recvmmsg
    {260, 61}, // wait4
    {261, 302}, // prlimit64
    {262, 300}, // fanotify_init
    {263, 301}, // fanotify_mark
    {264, 303}, // name_to_handle_at
    {265, 304}, // open_by_handle_at
    {266, 305}, // clock_adjtime
    {267, 306}, // syncfs
    {268, 308}, // setns
    {269, 307}, // sendmmsg
    {270, 310}, // process_vm_readv
    {271, 311}, // process_vm_writev
    {272, 312}, // kcmp
    {273, 313}, // finit_module
    {274, 314}, // sched_setattr
    {275, 315}, // sched_getattr
    {276, 316}, // renameat2
    {277, 317}, // seccomp
    {278, 318}, // getrandom
    {279, 319}, // memfd_create
    {280, 320}, // kexec_file_load
    {281, 321}, // bpf
    {282, 322}, // execveat
    {283, 323}, // userfaultfd
    {284, 324}, // membarrier
    {285, 325}, // mlock2
    {286, 326}, // copy_file_range
    {287, 327}, // preadv2
    {288, 328}, // pwritev2
    {289, 329}, // pkey_mprotect
    {290, 330}, // pkey_alloc
    {291, 331}, // pkey_free
    {292, 332}, // statx
    {293, 333}, // io_pgetevents
    {294, 334}, // rseq
    {295, 335}, // uretprobe
    {424, 424}, // pidfd_send_signal
    {425, 425}, // io_uring_setup
    {426, 426}, // io_uring_enter
    {427, 427}, // io_uring_register
    {428, 428}, // open_tree
    {429, 429}, // move_mount
    {430, 430}, // fsopen
    {431, 431}, // fsconfig
    {432, 432}, // fsmount
    {433, 433}, // fspick
    {434, 434}, // pidfd_open
    {435, 435}, // clone3
    {436, 436}, // close_range
    {437, 437}, // openat2
    {438, 438}, // pidfd_getfd
    {439, 439}, // faccessat2
    {440, 440}, // process_madvise
    {441, 441}, // epoll_pwait2
    {442, 442}, // mount_setattr
    {443, 443}, // quotactl_fd
    {444, 444}, // landlock_create_ruleset
    {445, 445}, // landlock_add_rule
    {446, 446}, // landlock_restrict_self
    {447, 447}, // memfd_secret
    {448, 448}, // process_mrelease
    {449, 449}, // futex_waitv
    {450, 450}, // set_mempolicy_home_node
    {451, 451}, // cachestat
    {452, 452}, // fchmodat2
    {453, 453}, // map_shadow_stack
    {454, 454}, // futex_wake
    {455, 455}, // futex_wait
    {456, 456}, // futex_requeue
    {457, 457}, // statmount
    {458, 458}, // listmount
    {459, 459}, // lsm_get_self_attr
    {460, 460}, // lsm_set_self_attr
    {461, 461}, // lsm_list_modules
    {462, 462}, // mseal
    {463, 463}, // setxattrat
    {464, 464}, // getxattrat
    {465, 465}, // listxattrat
    {466, 466}, // removexattrat
    {467, 467}, // open_tree_attr
};

extern "C" long host_syscall_proxy(long number, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6) {
    auto it = arm64_to_x86_64_syscall.find(number);
    if (it == arm64_to_x86_64_syscall.end()) {
        fprintf(stderr, "[JIT][SYSCALL] Неизвестный ARM64 syscall: %ld\n", number);
        errno = ENOSYS;
        return -1;
    }
    long x86_64_num = it->second;
    if (debug_enabled) fprintf(stderr, "[JIT][SYSCALL] ARM64#%ld → x86-64#%ld args: %ld %ld %ld %ld %ld %ld\n", number, x86_64_num, arg1, arg2, arg3, arg4, arg5, arg6);
    // write: fd, buf, count
    if (x86_64_num == 1) {
        void* real_buf = (void*)arg2;
        if (g_elf.emu_mem && arg2 >= g_elf.base_addr && (arg2 + arg3) <= (g_elf.base_addr + g_elf.emu_mem_size)) {
            real_buf = (uint8_t*)g_elf.emu_mem + (arg2 - g_elf.base_addr);
        }
        if (debug_enabled) {
            fprintf(stderr, "[JIT][SYSCALL] write(fd=%ld, buf=0x%lx, count=%ld): ", arg1, arg2, arg3);
            if (real_buf && arg3 > 0) {
                const char* s = (const char*)real_buf;
                for (long i = 0; i < arg3; ++i) fputc(s[i], stderr);
            }
            fputc('\n', stderr);
        }
        return syscall(x86_64_num, arg1, real_buf, arg3, arg4, arg5, arg6);
    }
    return syscall(x86_64_num, arg1, arg2, arg3, arg4, arg5, arg6);
} 