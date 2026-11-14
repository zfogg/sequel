#pragma once

#include "runtime.h"

#if defined(__MINGW64__) && defined(__aarch64__)
#define __USE_MINGW_SETJMP_NON_SEH
#endif

#if defined(__MINGW64__) && defined(_WIN64) && !defined(__aarch64__)
#define sequel_checkpoint(context) _setjmp((context)->escape, NULL)
#else
#define sequel_checkpoint(context) setjmp((context)->escape)
#endif

#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
