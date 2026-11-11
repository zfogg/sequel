#pragma once

#include <stddef.h>

/** Maximum accepted source bytes unless a smaller limit is requested. */
#ifndef SEQUEL_INPUT_MAX
#define SEQUEL_INPUT_MAX ((size_t)1024 * 1024 * 1024)
#endif

/** Maximum generated text bytes unless a smaller limit is requested. */
#ifndef SEQUEL_TEXT_MAX
#define SEQUEL_TEXT_MAX ((size_t)1024 * 1024 * 1024)
#endif

/** Maximum summary bytes unless a smaller limit is requested. */
#ifndef SEQUEL_SUMMARY_MAX
#define SEQUEL_SUMMARY_MAX ((size_t)1024 * 1024 * 1024)
#endif
