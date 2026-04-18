#pragma once

#include <stddef.h>

struct iperf_buffer_handle {
    void *ptr;
    size_t size;
};

int iperf_buffer_alloc(size_t size, struct iperf_buffer_handle *h);
void iperf_buffer_free(struct iperf_buffer_handle *h);
