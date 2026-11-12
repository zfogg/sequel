#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "runtime.h"

#ifdef _MSC_VER
__declspec(thread) sequel_call *sequel_current_call;
#else
_Thread_local sequel_call *sequel_current_call;
#endif

void sequel_abort(sequel_status_t failure) {
  sequel_current_call->failure = failure;
  longjmp(sequel_current_call->escape, 1);
}

void sequel_work_tick(size_t units) {
  sequel_call *call = sequel_current_call;
  if (!call)
    return;
  if (units > SIZE_MAX - call->work || (call->work_limit && units > call->work_limit - call->work))
    sequel_abort(SEQUEL_ERR_LIMIT);
  call->work += units;
  if (call->cancelled && (units >= call->poll || !call->poll)) {
    call->poll = 256;
    if (call->cancelled(call->userdata))
      sequel_abort(SEQUEL_ERR_CANCELLED);
  } else if (call->cancelled)
    call->poll -= units;
}

void sequel_output_tick(size_t bytes) {
  sequel_call *call = sequel_current_call;
  if (call && bytes > call->output_limit - call->output_bytes)
    sequel_abort(SEQUEL_ERR_LIMIT);
  if (call)
    call->output_bytes += bytes;
}

void *sequel_alloc(size_t bytes) {
  sequel_call *call = sequel_current_call;
  if (bytes > SIZE_MAX - sizeof(sequel_allocation)) {
    if (call)
      sequel_abort(SEQUEL_ERR_LIMIT);
    return NULL;
  }
  size_t total = bytes + sizeof(sequel_allocation);
  if (call && total > call->memory_limit - call->live)
    sequel_abort(SEQUEL_ERR_LIMIT);
  sequel_allocation *allocation = malloc(total);
  if (!allocation) {
    if (call)
      sequel_abort(SEQUEL_ERR_NOMEM);
    return NULL;
  }
  memset(&allocation->h, 0, sizeof(allocation->h));
  allocation->h.bytes = total;
  allocation->h.owner = call;
  if (call) {
    allocation->h.next = call->allocations;
    if (call->allocations)
      call->allocations->h.previous = allocation;
    call->allocations = allocation;
    call->live += total;
    if (call->live > call->peak)
      call->peak = call->live;
  }
  return allocation + 1;
}

void sequel_dealloc(void *pointer) {
  if (!pointer)
    return;
  sequel_allocation *allocation = (sequel_allocation *)pointer - 1;
  sequel_call *call = sequel_current_call;
  if (call && allocation->h.owner == call) {
    if (allocation->h.previous)
      allocation->h.previous->h.next = allocation->h.next;
    else
      call->allocations = allocation->h.next;
    if (allocation->h.next)
      allocation->h.next->h.previous = allocation->h.previous;
    call->live -= allocation->h.bytes;
  }
  free(allocation);
}

void *sequel_calloc(size_t count, size_t size) {
  if (count && size > SIZE_MAX / count) {
    if (sequel_current_call)
      sequel_abort(SEQUEL_ERR_LIMIT);
    return NULL;
  }
  size_t bytes = count * size;
  void *pointer = sequel_alloc(bytes);
  if (pointer) {
    sequel_work_tick(bytes);
    memset(pointer, 0, bytes);
  }
  return pointer;
}

void *sequel_realloc(void *pointer, size_t bytes) {
  if (!pointer)
    return sequel_alloc(bytes);
  sequel_allocation *old = (sequel_allocation *)pointer - 1;
  void *next = sequel_alloc(bytes);
  if (!next)
    return NULL;
  size_t copy = old->h.bytes - sizeof(*old);
  if (copy > bytes)
    copy = bytes;
  sequel_work_tick(copy);
  memcpy(next, pointer, copy);
  sequel_dealloc(pointer);
  return next;
}

sequel_call *sequel_call_new(void) { return calloc(1, sizeof(sequel_call)); }

void sequel_call_delete(sequel_call *call) { free(call); }

void sequel_release_allocations(void *storage) {
  sequel_allocation *allocation = storage;
  while (allocation) {
    sequel_allocation *next = allocation->h.next;
    free(allocation);
    allocation = next;
  }
}
