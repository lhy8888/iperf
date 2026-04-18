#pragma once

#include <stddef.h>

int win_net_acquire(void);
void win_net_release(void);

int win_readentropy(void *out, size_t size);
double win_cpu_util(void);
void win_get_system_info(char *buf, size_t len);
