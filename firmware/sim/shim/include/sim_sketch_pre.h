// force-included into the firmware TU only
#pragma once
#include <sys/stat.h>
#include "sim_posix.h"
#define stat(p, b) gv_stat(p, b)
#include <sys/time.h>
#ifdef __cplusplus
extern "C" void __sim_oom(void);
extern "C" int settimeofday(const struct timeval *tv, const void *tz);
#endif
