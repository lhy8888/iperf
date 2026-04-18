#include "win_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW os_version_info);

static LONG g_wsa_ref = 0;

static unsigned long long
filetime_to_u64(const FILETIME *ft)
{
    ULARGE_INTEGER value;
    value.LowPart = ft->dwLowDateTime;
    value.HighPart = ft->dwHighDateTime;
    return value.QuadPart;
}

static const char *
arch_name(WORD arch)
{
    switch (arch) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64:
        return "arm64";
    case PROCESSOR_ARCHITECTURE_ARM:
        return "arm";
    case PROCESSOR_ARCHITECTURE_INTEL:
        return "x86";
    default:
        return "unknown";
    }
}

int
win_net_acquire(void)
{
    if (InterlockedIncrement(&g_wsa_ref) == 1) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            InterlockedDecrement(&g_wsa_ref);
            return -1;
        }
    }
    return 0;
}

void
win_net_release(void)
{
    if (InterlockedDecrement(&g_wsa_ref) == 0) {
        WSACleanup();
    }
}

int
win_readentropy(void *out, size_t size)
{
    if (out == NULL || size == 0) {
        return 0;
    }
    if (BCryptGenRandom(NULL, (PUCHAR) out, (ULONG) size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

double
win_cpu_util(void)
{
    static FILETIME last_wall = { 0 };
    static FILETIME last_kernel = { 0 };
    static FILETIME last_user = { 0 };
    static int initialized = 0;

    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    FILETIME wall_time;
    unsigned long long wall_delta;
    unsigned long long proc_delta;

    if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time)) {
        return 0.0;
    }
    GetSystemTimeAsFileTime(&wall_time);

    if (!initialized) {
        last_wall = wall_time;
        last_kernel = kernel_time;
        last_user = user_time;
        initialized = 1;
        return 0.0;
    }

    wall_delta = filetime_to_u64(&wall_time) - filetime_to_u64(&last_wall);
    proc_delta = (filetime_to_u64(&kernel_time) - filetime_to_u64(&last_kernel)) +
                 (filetime_to_u64(&user_time) - filetime_to_u64(&last_user));

    last_wall = wall_time;
    last_kernel = kernel_time;
    last_user = user_time;

    if (wall_delta == 0) {
        return 0.0;
    }

    {
        double ratio = (double) proc_delta / (double) wall_delta;
        if (ratio < 0.0) {
            ratio = 0.0;
        }
        if (ratio > 1.0) {
            ratio = 1.0;
        }
        return ratio;
    }
}

void
win_get_system_info(char *buf, size_t len)
{
    RTL_OSVERSIONINFOW os_version;
    SYSTEM_INFO system_info;
    HMODULE ntdll;
    RtlGetVersionFn rtl_get_version;

    if (buf == NULL || len == 0) {
        return;
    }

    memset(buf, 0, len);
    memset(&os_version, 0, sizeof(os_version));
    os_version.dwOSVersionInfoSize = sizeof(os_version);

    ntdll = GetModuleHandleA("ntdll.dll");
    rtl_get_version = NULL;
    if (ntdll != NULL) {
        rtl_get_version = (RtlGetVersionFn) GetProcAddress(ntdll, "RtlGetVersion");
    }
    if (rtl_get_version != NULL) {
        (void) rtl_get_version(&os_version);
    }

    GetSystemInfo(&system_info);

    snprintf(buf, len, "Windows %lu.%lu Build %lu %s",
             (unsigned long) os_version.dwMajorVersion,
             (unsigned long) os_version.dwMinorVersion,
             (unsigned long) os_version.dwBuildNumber,
             arch_name(system_info.wProcessorArchitecture));
}

#else

int
win_net_acquire(void)
{
    return 0;
}

void
win_net_release(void)
{
}

int
win_readentropy(void *out, size_t size)
{
    (void) out;
    (void) size;
    errno = ENOSYS;
    return -1;
}

double
win_cpu_util(void)
{
    return 0.0;
}

void
win_get_system_info(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    snprintf(buf, len, "Unix");
}

#endif
