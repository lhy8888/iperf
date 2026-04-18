#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t iperf_sock_read(int fd, void *buf, size_t n);
ssize_t iperf_sock_write(int fd, const void *buf, size_t n);
int iperf_sock_close(int fd);
int iperf_sock_set_nonblocking(int fd, int enable);
int iperf_sock_last_error(void);

#ifdef _WIN32
int iperf_sock_setsockopt(int fd, int level, int optname, const void *optval, int optlen);
int iperf_sock_getsockopt(int fd, int level, int optname, void *optval, int *optlen);

#ifndef IPERF_SOCKET_COMPAT_IMPL
#define setsockopt(fd, level, optname, optval, optlen) \
    iperf_sock_setsockopt((fd), (level), (optname), (optval), (optlen))
#define getsockopt(fd, level, optname, optval, optlen) \
    iperf_sock_getsockopt((fd), (level), (optname), (optval), (optlen))
#endif
#endif

#ifdef __cplusplus
}
#endif
