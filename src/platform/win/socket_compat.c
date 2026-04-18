#define IPERF_SOCKET_COMPAT_IMPL
#include "socket_compat.h"

#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

static int
map_wsa_error(int wsa_error)
{
    switch (wsa_error) {
    case WSAEWOULDBLOCK:
        return EWOULDBLOCK;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAESHUTDOWN:
        return EPIPE;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETRESET:
        return ENETRESET;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:
        return EOPNOTSUPP;
    default:
        return EIO;
    }
}

int
iperf_sock_last_error(void)
{
    return map_wsa_error(WSAGetLastError());
}

ssize_t
iperf_sock_read(int fd, void *buf, size_t n)
{
    int r = recv((SOCKET) fd, (char *) buf, (int) n, 0);
    if (r == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return (ssize_t) r;
}

ssize_t
iperf_sock_write(int fd, const void *buf, size_t n)
{
    int r = send((SOCKET) fd, (const char *) buf, (int) n, 0);
    if (r == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return (ssize_t) r;
}

int
iperf_sock_close(int fd)
{
    if (closesocket((SOCKET) fd) == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return 0;
}

int
iperf_sock_set_nonblocking(int fd, int enable)
{
    u_long mode = enable ? 1 : 0;

    if (ioctlsocket((SOCKET) fd, FIONBIO, &mode) == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return 0;
}

#else

#include <fcntl.h>
#include <unistd.h>

int
iperf_sock_last_error(void)
{
    return errno;
}

ssize_t
iperf_sock_read(int fd, void *buf, size_t n)
{
    return read(fd, buf, n);
}

ssize_t
iperf_sock_write(int fd, const void *buf, size_t n)
{
    return write(fd, buf, n);
}

int
iperf_sock_close(int fd)
{
    return close(fd);
}

int
iperf_sock_set_nonblocking(int fd, int enable)
{
    int flags;
    int newflags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (enable) {
        newflags = flags | O_NONBLOCK;
    } else {
        newflags = flags & ~O_NONBLOCK;
    }
    if (newflags != flags) {
        if (fcntl(fd, F_SETFL, newflags) < 0) {
            return -1;
        }
    }
    return 0;
}

int
iperf_sock_setsockopt(int fd, int level, int optname, const void *optval, int optlen)
{
    if (setsockopt((SOCKET) fd, level, optname, (const char *) optval, optlen) == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return 0;
}

int
iperf_sock_getsockopt(int fd, int level, int optname, void *optval, int *optlen)
{
    if (getsockopt((SOCKET) fd, level, optname, (char *) optval, optlen) == SOCKET_ERROR) {
        errno = iperf_sock_last_error();
        return -1;
    }
    return 0;
}

#endif
