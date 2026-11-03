#pragma once
/* Per-call accounting. The allocation registry permits nonlocal cancellation of
 * deeply nested legacy transforms without retaining partially sanitized output. */
#include <setjmp.h>
#include <stdalign.h>
#if defined(__MINGW64__) && defined(_WIN64)
/* cgo owns the surrounding Windows stack; bypass foreign SEH unwinding. */
#define sequel_checkpoint(context) _setjmp((context)->escape, NULL)
#else
#define sequel_checkpoint(context) setjmp((context)->escape)
#endif

typedef union sequel_allocation sequel_allocation;
union sequel_allocation {
  long double alignment;
  void *pointer_alignment;
  struct {
    sequel_allocation *previous, *next;
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
  size_t live, peak, work, memory_limit, work_limit, poll, output_bytes, output_limit;
  sequel_cancel_fn cancelled;
  void *userdata;
  sequel_status_t failure;
} sequel_call;
#ifdef _MSC_VER
static __declspec(thread) sequel_call *sequel_current_call;
#else
static _Thread_local sequel_call *sequel_current_call;
#endif
static void sequel_abort(sequel_status_t failure) {
  sequel_current_call->failure = failure;
  longjmp(sequel_current_call->escape, 1);
}
static void sequel_work_tick(size_t units) {
  sequel_call *c = sequel_current_call;
  if (!c)
    return;
  if (units > SIZE_MAX - c->work || (c->work_limit && units > c->work_limit - c->work))
    sequel_abort(SEQUEL_ERR_LIMIT);
  c->work += units;
  if (c->cancelled && (units >= c->poll || !c->poll)) {
    c->poll = 256;
    if (c->cancelled(c->userdata))
      sequel_abort(SEQUEL_ERR_CANCELLED);
  } else if (c->cancelled)
    c->poll -= units;
}
static void sequel_output_tick(size_t bytes) {
  sequel_call *c = sequel_current_call;
  if (c && bytes > c->output_limit - c->output_bytes)
    sequel_abort(SEQUEL_ERR_LIMIT);
  if (c)
    c->output_bytes += bytes;
}
static void *sequel_alloc(size_t bytes) {
  sequel_call *c = sequel_current_call;
  if (bytes > SIZE_MAX - sizeof(sequel_allocation)) {
    if (c)
      sequel_abort(SEQUEL_ERR_LIMIT);
    return NULL;
  }
  size_t total = bytes + sizeof(sequel_allocation);
  if (c && total > c->memory_limit - c->live)
    sequel_abort(SEQUEL_ERR_LIMIT);
  sequel_allocation *a = malloc(total);
  if (!a) {
    if (c)
      sequel_abort(SEQUEL_ERR_NOMEM);
    return NULL;
  }
  memset(&a->h, 0, sizeof(a->h));
  a->h.bytes = total;
  a->h.owner = c;
  if (c) {
    a->h.next = c->allocations;
    if (c->allocations)
      c->allocations->h.previous = a;
    c->allocations = a;
    c->live += total;
    if (c->live > c->peak)
      c->peak = c->live;
  }
  return a + 1;
}
static void sequel_dealloc(void *pointer) {
  if (!pointer)
    return;
  sequel_allocation *a = (sequel_allocation *)pointer - 1;
  sequel_call *c = sequel_current_call;
  if (c && a->h.owner == c) {
    if (a->h.previous)
      a->h.previous->h.next = a->h.next;
    else
      c->allocations = a->h.next;
    if (a->h.next)
      a->h.next->h.previous = a->h.previous;
    c->live -= a->h.bytes;
  }
  free(a);
}
static void *sequel_calloc(size_t count, size_t size) {
  if (count && size > SIZE_MAX / count) {
    if (sequel_current_call)
      sequel_abort(SEQUEL_ERR_LIMIT);
    return NULL;
  }
  size_t n = count * size;
  void *p = sequel_alloc(n);
  if (p) {
    sequel_work_tick(n);
    memset(p, 0, n);
  }
  return p;
}
static void *sequel_realloc(void *pointer, size_t bytes) {
  if (!pointer)
    return sequel_alloc(bytes);
  sequel_allocation *old = (sequel_allocation *)pointer - 1;
  void *next = sequel_alloc(bytes);
  if (!next)
    return NULL;
  size_t n = old->h.bytes - sizeof(*old);
  if (n > bytes)
    n = bytes;
  sequel_work_tick(n);
  memcpy(next, pointer, n);
  sequel_dealloc(pointer);
  return next;
}
static sequel_call *sequel_call_new(void) { return calloc(1, sizeof(sequel_call)); }
static void sequel_call_delete(sequel_call *c) { free(c); }
static void sequel_release_allocations(void *storage) {
  sequel_allocation *a = storage;
  while (a) {
    sequel_allocation *next = a->h.next;
    free(a);
    a = next;
  }
}
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
/* Charge all scanner/transform while conditions, including long lexical tokens. */
#define while(condition) while ((sequel_work_tick(1), (condition)))
