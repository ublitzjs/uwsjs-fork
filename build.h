#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>

static volatile char CaughtSIGINT = 0;
void SIGINTHandler(int dummy) { CaughtSIGINT = 1; }

/* List of platform features */
#if defined(_WIN32)
#define OS "win32"
#define IS_WINDOWS
#define STATIC_LIB(path, name) " ./" path "/" name ".lib "
#define C_COMPILER "clang -fms-runtime-lib=static"
#define CXX_COMPILER "clang++ -fms-runtime-lib=static"
#define WIN32_LEAN_AND_MEAN
// suppress Windows "secure" deprecations
// #define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
int get_cpu_count(void) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
}
static DWORD WINAPI _thread_fn(LPVOID _arg);
#define _THREAD_BODY_START  /* defined per-use below */

#define START_FOREACH_NODEJS(i)                                           \
  HANDLE _handles[versionsQuantity];                                      \
  struct { unsigned int idx; } _args[versionsQuantity];                   \
  DWORD WINAPI _thread_fn(LPVOID _arg) {                                  \
    unsigned int i = ((typeof(_args[0])*)_arg)->idx;                      \

#define END_FOREACH_NODEJS                                                \
    return 0;                                                             \
  }                                                                       \
  for (unsigned int _i = 0; _i < versionsQuantity; _i++) {               \
    _args[_i].idx = _i;                                                   \
    _handles[_i] = CreateThread(NULL, 0, _thread_fn, &_args[_i], 0, NULL);\
  }                                                                       \
  WaitForMultipleObjects(versionsQuantity, _handles, TRUE, INFINITE);    \
  for (unsigned int _i = 0; _i < versionsQuantity; _i++) CloseHandle(_handles[_i]);

#else // POSIX systems
#define STATIC_LIB(path, name) " " path "/lib" name ".a "
#define START_FOREACH_NODEJS(i) \
  pid_t pids[versionsQuantity]; \
  for (unsigned int i = 0; i < versionsQuantity; i++) { \
    pids[i] = fork(); \
    if (pids[i] != 0) continue; 

#define END_FOREACH_NODEJS \
    exit(0); \
  } \
  for (unsigned int i = 0; i < versionsQuantity; i++) waitpid(pids[i], 0, 0);

#include <unistd.h>
#include <sys/wait.h>
int get_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

#if defined(__linux)
#define OS "linux"
#define IS_LINUX
#define C_COMPILER "clang"
#define CXX_COMPILER "clang++"

#elif defined(__APPLE__)
#define OS "darwin"
#define IS_MACOS
#if defined(CROSS_COMPILE_MACOS)
#define C_COMPILER "clang -target x86_64-apple-macos12"
#define CXX_COMPILER "clang++ -target x86_64-apple-macos12"
#else
#define C_COMPILER "clang -target arm64-apple-macos12"
#define CXX_COMPILER "clang++ -target arm64-apple-macos12"
#endif
#endif

#endif

/* ASAN vs. optimized build flags (used via C string literal concatenation). */
#ifdef WITH_ASAN
#define OPT_FLAGS " -fsanitize=address -fno-omit-frame-pointer -g -O1"
#define LINUX_LINK_EXTRAS " -fsanitize=address"
#define MACOS_LINK_EXTRAS " -fsanitize=address"
#define PER_TARGET_ARTIFACTS_FOLDER "artifacts-asan"
#else
#define OPT_FLAGS " -flto -O3"
#define LINUX_LINK_EXTRAS " -static-libstdc++ -static-libgcc"
#define MACOS_LINK_EXTRAS ""
#define PER_TARGET_ARTIFACTS_FOLDER "artifacts"
#endif

const char *ARM = "arm";
const char *ARM64 = "arm64";
const char *X64 = "x64";

#if defined(CROSS_COMPILE_MACOS)
#define ARCH X64
#elif defined(__arm__)
#define ARCH ARM
#elif defined(__aarch64__)
#define ARCH ARM64
#else
#define ARCH X64
#endif

/* System, but with string replace */
int run(const char *cmd, ...) {
    char buf[2048];
    va_list args;
    va_start(args, cmd);
    vsprintf(buf, cmd, args);
    va_end(args);
    printf("--> %s\n\n", buf);
    if(CaughtSIGINT){ printf("\nCaught SIGINT!!! Exiting now\n"); exit(0); };
    return system(buf);
}

/* List of Node.js versions */
struct node_version {
    const char *name;
    const char *abi;
} versions[] = {
    {"v22.0.0", "127"},
    {"v24.0.0", "137"},
    {"v26.0.0", "147"}
};
const int versionsQuantity = sizeof(versions) / sizeof(struct node_version);
int threads_quantity;
