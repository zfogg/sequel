#pragma once

#include <setjmp.h>
#include <sequel.h>

#if defined(__MINGW64__) && defined(_WIN64) && !defined(__aarch64__)
#define sequel_checkpoint(context) _setjmp((context)->escape, NULL)
#else
#define sequel_checkpoint(context) setjmp((context)->escape)
#endif

typedef union sequel_allocation sequel_allocation;

union sequel_allocation {
  long double alignment;
  void *pointer_alignment;

  struct {
    sequel_allocation *previous;
    sequel_allocation *next;
    size_t bytes;
    void *owner;
  } h;
};

typedef struct {
  jmp_buf escape;
  const char *source;
  bool ambiguous_literal;
  size_t ambiguous_offset;
  sequel_allocation *allocations;
  size_t live;
  size_t peak;
  size_t work;
  size_t memory_limit;
  size_t work_limit;
  size_t poll;
  size_t output_bytes;
  size_t output_limit;
  sequel_cancel_fn cancelled;
  void *userdata;
  sequel_status_t failure;
} sequel_call;

#ifdef _MSC_VER
extern __declspec(thread) sequel_call *sequel_current_call;
#else
extern _Thread_local sequel_call *sequel_current_call;
#endif
void sequel_abort(sequel_status_t failure);
void sequel_work_tick(size_t units);
void sequel_output_tick(size_t bytes);
void *sequel_alloc(size_t bytes);
void sequel_dealloc(void *pointer);
void *sequel_calloc(size_t count, size_t size);
void *sequel_realloc(void *pointer, size_t bytes);
sequel_call *sequel_call_new(void);
void sequel_call_delete(sequel_call *call);
void sequel_release_allocations(void *storage);
