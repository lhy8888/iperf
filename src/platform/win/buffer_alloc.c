#include "buffer_alloc.h"

#include <errno.h>

#ifdef _WIN32
#include <stdlib.h>

int
iperf_buffer_alloc(size_t size, struct iperf_buffer_handle *h)
{
    if (h == NULL) {
        return -1;
    }
    h->ptr = malloc(size);
    h->size = 0;
    if (h->ptr == NULL) {
        errno = ENOMEM;
        return -1;
    }
    h->size = size;
    return 0;
}

void
iperf_buffer_free(struct iperf_buffer_handle *h)
{
    if (h == NULL || h->ptr == NULL) {
        return;
    }
    free(h->ptr);
    h->ptr = NULL;
    h->size = 0;
}

#else

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int
iperf_buffer_alloc(size_t size, struct iperf_buffer_handle *h)
{
    char template[] = "/tmp/iperf3.XXXXXX";
    int fd;

    if (h == NULL) {
        return -1;
    }

    h->ptr = NULL;
    h->size = 0;

    fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }
    (void) unlink(template);

    if (ftruncate(fd, (off_t) size) < 0) {
        (void) close(fd);
        return -1;
    }

    h->ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    (void) close(fd);
    if (h->ptr == MAP_FAILED) {
        h->ptr = NULL;
        return -1;
    }
    h->size = size;
    return 0;
}

void
iperf_buffer_free(struct iperf_buffer_handle *h)
{
    if (h == NULL || h->ptr == NULL) {
        return;
    }
    (void) munmap(h->ptr, h->size);
    h->ptr = NULL;
    h->size = 0;
}

#endif
