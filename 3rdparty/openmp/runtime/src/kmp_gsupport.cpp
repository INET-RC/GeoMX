/*
 * kmp_gsupport.cpp
 */


//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#include "kmp.h"
#include "kmp_atomic.h"

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define MKLOC(loc, routine)                                                    \
  static ident_t(loc) = {0, KMP_IDENT_KMPC, 0, 0, ";unknown;unknown;0;0;;"};

#include "kmp_ftn_os.h"

void xexpand(KMP_API_NAME_GOMP_BARRIER)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_barrier");
  KA_TRACE(20, ("GOMP_barrier: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_TRACE
  ompt_frame_t *ompt_frame;
  if (ompt_enabled) {
    ompt_frame = __ompt_get_task_frame_internal(0);
    ompt_frame->reenter_runtime_frame = __builtin_frame_address(1);
  }
#endif
  __kmpc_barrier(&loc, gtid);
}

// Mutual exclusion

// The symbol that icc/ifort generates for unnamed for unnamed critical sections
// - .gomp_critical_user_ - is defined using .comm in any objects reference it.
// We can't reference it directly here in C code, as the symbol contains a ".".
//
// The RTL contains an assembly language definition of .gomp_critical_user_
// with another symbol __kmp_unnamed_critical_addr initialized with it's
// address.
extern kmp_critical_name *__kmp_unnamed_critical_addr;

void xexpand(KMP_API_NAME_GOMP_CRITICAL_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_critical_start");
  KA_TRACE(20, ("GOMP_critical_start: T#%d\n", gtid));
  __kmpc_critical(&loc, gtid, __kmp_unnamed_critical_addr);
}

void xexpand(KMP_API_NAME_GOMP_CRITICAL_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_critical_end");
  KA_TRACE(20, ("GOMP_critical_end: T#%d\n", gtid));
  __kmpc_end_critical(&loc, gtid, __kmp_unnamed_critical_addr);
}

void xexpand(KMP_API_NAME_GOMP_CRITICAL_NAME_START)(void **pptr) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_critical_name_start");
  KA_TRACE(20, ("GOMP_critical_name_start: T#%d\n", gtid));
  __kmpc_critical(&loc, gtid, (kmp_critical_name *)pptr);
}

void xexpand(KMP_API_NAME_GOMP_CRITICAL_NAME_END)(void **pptr) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_critical_name_end");
  KA_TRACE(20, ("GOMP_critical_name_end: T#%d\n", gtid));
  __kmpc_end_critical(&loc, gtid, (kmp_critical_name *)pptr);
}

// The Gnu codegen tries to use locked operations to perform atomic updates
// inline.  If it can't, then it calls GOMP_atomic_start() before performing
// the update and GOMP_atomic_end() afterward, regardless of the data type.
void xexpand(KMP_API_NAME_GOMP_ATOMIC_START)(void) {
  int gtid = __kmp_entry_gtid();
  KA_TRACE(20, ("GOMP_atomic_start: T#%d\n", gtid));

#if OMPT_SUPPORT
  __ompt_thread_assign_wait_id(0);
#endif

  __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
}

void xexpand(KMP_API_NAME_GOMP_ATOMIC_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_atomic_start: T#%d\n", gtid));
  __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
}

int xexpand(KMP_API_NAME_GOMP_SINGLE_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_single_start");
  KA_TRACE(20, ("GOMP_single_start: T#%d\n", gtid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  // 3rd parameter == FALSE prevents kmp_enter_single from pushing a
  // workshare when USE_CHECKS is defined.  We need to avoid the push,
  // as there is no corresponding GOMP_single_end() call.
  return __kmp_enter_single(gtid, &loc, FALSE);
}

void *xexpand(KMP_API_NAME_GOMP_SINGLE_COPY_START)(void) {
  void *retval;
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_single_copy_start");
  KA_TRACE(20, ("GOMP_single_copy_start: T#%d\n", gtid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  // If this is the first thread to enter, return NULL.  The generated code will
  // then call GOMP_single_copy_end() for this thread only, with the
  // copyprivate data pointer as an argument.
  if (__kmp_enter_single(gtid, &loc, FALSE))
    return NULL;

  // Wait for the first thread to set the copyprivate data pointer,
  // and for all other threads to reach this point.
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  // Retrieve the value of the copyprivate data point, and wait for all
  // threads to do likewise, then return.
  retval = __kmp_team_from_gtid(gtid)->t.t_copypriv_data;
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
  return retval;
}

void xexpand(KMP_API_NAME_GOMP_SINGLE_COPY_END)(void *data) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_single_copy_end: T#%d\n", gtid));

  // Set the copyprivate data pointer fo the team, then hit the barrier so that
  // the other threads will continue on and read it.  Hit another barrier before
  // continuing, so that the know that the copyprivate data pointer has been
  // propagated to all threads before trying to reuse the t_copypriv_data field.
  __kmp_team_from_gtid(gtid)->t.t_copypriv_data = data;
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
}

void xexpand(KMP_API_NAME_GOMP_ORDERED_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_ordered_start");
  KA_TRACE(20, ("GOMP_ordered_start: T#%d\n", gtid));
  __kmpc_ordered(&loc, gtid);
}

void xexpand(KMP_API_NAME_GOMP_ORDERED_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_ordered_end");
  KA_TRACE(20, ("GOMP_ordered_start: T#%d\n", gtid));
  __kmpc_end_ordered(&loc, gtid);
}

// Dispatch macro defs
//
// They come in two flavors: 64-bit unsigned, and either 32-bit signed
// (IA-32 architecture) or 64-bit signed (Intel(R) 64).

#if KMP_ARCH_X86 || KMP_ARCH_ARM || KMP_ARCH_MIPS
#define KMP_DISPATCH_INIT __kmp_aux_dispatch_init_4
#define KMP_DISPATCH_FINI_CHUNK __kmp_aux_dispatch_fini_chunk_4
#define KMP_DISPATCH_NEXT __kmpc_dispatch_next_4
#else
#define KMP_DISPATCH_INIT __kmp_aux_dispatch_init_8
#define KMP_DISPATCH_FINI_CHUNK __kmp_aux_dispatch_fini_chunk_8
#define KMP_DISPATCH_NEXT __kmpc_dispatch_next_8
#endif /* KMP_ARCH_X86 */

#define KMP_DISPATCH_INIT_ULL __kmp_aux_dispatch_init_8u
#define KMP_DISPATCH_FINI_CHUNK_ULL __kmp_aux_dispatch_fini_chunk_8u
#define KMP_DISPATCH_NEXT_ULL __kmpc_dispatch_next_8u

// The parallel contruct

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_microtask_wrapper(int *gtid, int *npr, void (*task)(void *),
                                 void *data) {
#if OMPT_SUPPORT
  kmp_info_t *thr;
  ompt_frame_t *ompt_frame;
  ompt_state_t enclosing_state;

  if (ompt_enabled) {
    // get pointer to thread data structure
    thr = __kmp_threads[*gtid];

    // save enclosing task state; set current state for task
    enclosing_state = thr->th.ompt_thread_info.state;
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;

    // set task frame
    ompt_frame = __ompt_get_task_frame_internal(0);
    ompt_frame->exit_runtime_frame = __builtin_frame_address(0);
  }
#endif

  task(data);

#if OMPT_SUPPORT
  if (ompt_enabled) {
    // clear task frame
    ompt_frame->exit_runtime_frame = NULL;

    // restore enclosing state
    thr->th.ompt_thread_info.state = enclosing_state;
  }
#endif
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_parallel_microtask_wrapper(int *gtid, int *npr,
                                          void (*task)(void *), void *data,
                                          unsigned num_threads, ident_t *loc,
                                          enum sched_type schedule, long start,
                                          long end, long incr,
                                          long chunk_size) {
  // Intialize the loop worksharing construct.
  KMP_DISPATCH_INIT(loc, *gtid, schedule, start, end, incr, chunk_size,
                    schedule != kmp_sch_static);

#if OMPT_SUPPORT
  kmp_info_t *thr;
  ompt_frame_t *ompt_frame;
  ompt_state_t enclosing_state;

  if (ompt_enabled) {
    thr = __kmp_threads[*gtid];
    // save enclosing task state; set current state for task
    enclosing_state = thr->th.ompt_thread_info.state;
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;

    // set task frame
    ompt_frame = __ompt_get_task_frame_internal(0);
    ompt_frame->exit_runtime_frame = __builtin_frame_address(0);
  }
#endif

  // Now invoke the microtask.
  task(data);

#if OMPT_SUPPORT
  if (ompt_enabled) {
    // clear task frame
    ompt_frame->exit_runtime_frame = NULL;

    // reset enclosing state
    thr->th.ompt_thread_info.state = enclosing_state;
  }
#endif
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_fork_call(ident_t *loc, int gtid, void (*unwrapped_task)(void *),
                         microtask_t wrapper, int argc, ...) {
  int rc;
  kmp_info_t *thr = __kmp_threads[gtid];
  kmp_team_t *team = thr->th.th_team;
  int tid = __kmp_tid_from_gtid(gtid);

  va_list ap;
  va_start(ap, argc);

  rc = __kmp_fork_call(loc, gtid, fork_context_gnu, argc,
#if OMPT_SUPPORT
                       VOLATILE_CAST(void *) unwrapped_task,
#endif
                       wrapper, __kmp_invoke_task_func,
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
                       &ap
#else
                       ap
#endif
                       );

  va_end(ap);

  if (rc) {
    __kmp_run_before_invoked_task(gtid, tid, thr, team);
  }

#if OMPT_SUPPORT
  if (ompt_enabled) {
#if OMPT_TRACE
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    ompt_task_info_t *task_info = __ompt_get_taskinfo(0);

    // implicit task callback
    if (ompt_callbacks.ompt_callback(ompt_event_implicit_task_begin)) {
      ompt_callbacks.ompt_callback(ompt_event_implicit_task_begin)(
          team_info->parallel_id, task_info->task_id);
    }
#endif
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;
  }
#endif
}

static void __kmp_GOMP_serialized_parallel(ident_t *loc, kmp_int32 gtid,
                                           void (*task)(void *)) {
#if OMPT_SUPPORT
  ompt_parallel_id_t ompt_parallel_id;
  if (ompt_enabled) {
    ompt_task_info_t *task_info = __ompt_get_taskinfo(0);

    ompt_parallel_id = __ompt_parallel_id_new(gtid);

    // parallel region callback
    if (ompt_callbacks.ompt_callback(ompt_event_parallel_begin)) {
      int team_size = 1;
      ompt_callbacks.ompt_callback(ompt_event_parallel_begin)(
          task_info->task_id, &task_info->frame, ompt_parallel_id, team_size,
          (void *)task, OMPT_INVOKER(fork_context_gnu));
    }
  }
#endif

  __kmp_serialized_parallel(loc, gtid);

#if OMPT_SUPPORT
  if (ompt_enabled) {
    kmp_info_t *thr = __kmp_threads[gtid];

    ompt_task_id_t my_ompt_task_id = __ompt_task_id_new(gtid);

    // set up lightweight task
    ompt_lw_taskteam_t *lwt =
        (ompt_lw_taskteam_t *)__kmp_allocate(sizeof(ompt_lw_taskteam_t));
    __ompt_lw_taskteam_init(lwt, thr, gtid, (void *)task, ompt_parallel_id);
    lwt->ompt_task_info.task_id = my_ompt_task_id;
    __ompt_lw_taskteam_link(lwt, thr);

#if OMPT_TRACE
    // implicit task callback
    if (ompt_callbacks.ompt_callback(ompt_event_implicit_task_begin)) {
      ompt_callbacks.ompt_callback(ompt_event_implicit_task_begin)(
          ompt_parallel_id, my_ompt_task_id);
    }
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;
#endif
  }
#endif
}

void xexpand(KMP_API_NAME_GOMP_PARALLEL_START)(void (*task)(void *), void *data,
                                               unsigned num_threads) {
  int gtid = __kmp_entry_gtid();

#if OMPT_SUPPORT
  ompt_frame_t *parent_frame, *frame;

  if (ompt_enabled) {
    parent_frame = __ompt_get_task_frame_internal(0);
    parent_frame->reenter_runtime_frame = __builtin_frame_address(1);
  }
#endif

  MKLOC(loc, "GOMP_parallel_start");
  KA_TRACE(20, ("GOMP_parallel_start: T#%d\n", gtid));

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_microtask_wrapper, 2, task,
                         data);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

#if OMPT_SUPPORT
  if (ompt_enabled) {
    frame = __ompt_get_task_frame_internal(0);
    frame->exit_runtime_frame = __builtin_frame_address(1);
  }
#endif
}

void xexpand(KMP_API_NAME_GOMP_PARALLEL_END)(void) {
  int gtid = __kmp_get_gtid();
  kmp_info_t *thr;

  thr = __kmp_threads[gtid];

  MKLOC(loc, "GOMP_parallel_end");
  KA_TRACE(20, ("GOMP_parallel_end: T#%d\n", gtid));

#if OMPT_SUPPORT
  ompt_parallel_id_t parallel_id;
  ompt_task_id_t serialized_task_id;
  ompt_frame_t *ompt_frame = NULL;

  if (ompt_enabled) {
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    parallel_id = team_info->parallel_id;

    ompt_task_info_t *task_info = __ompt_get_taskinfo(0);
    serialized_task_id = task_info->task_id;

    // unlink if necessary. no-op if there is not a lightweight task.
    ompt_lw_taskteam_t *lwt = __ompt_lw_taskteam_unlink(thr);
    // GOMP allocates/frees lwt since it can't be kept on the stack
    if (lwt) {
      __kmp_free(lwt);
    }
  }
#endif

  if (!thr->th.th_team->t.t_serialized) {
    __kmp_run_after_invoked_task(gtid, __kmp_tid_from_gtid(gtid), thr,
                                 thr->th.th_team);

#if OMPT_SUPPORT
    if (ompt_enabled) {
      // Implicit task is finished here, in the barrier we might schedule
      // deferred tasks,
      // these don't see the implicit task on the stack
      ompt_frame = __ompt_get_task_frame_internal(0);
      ompt_frame->exit_runtime_frame = NULL;
    }
#endif

    __kmp_join_call(&loc, gtid
#if OMPT_SUPPORT
                    ,
                    fork_context_gnu
#endif
                    );
  } else {
#if OMPT_SUPPORT && OMPT_TRACE
    if (ompt_enabled &&
        ompt_callbacks.ompt_callback(ompt_event_implicit_task_end)) {
      ompt_callbacks.ompt_callback(ompt_event_implicit_task_end)(
          parallel_id, serialized_task_id);
    }
#endif

    __kmpc_end_serialized_parallel(&loc, gtid);

#if OMPT_SUPPORT
    if (ompt_enabled) {
      // Record that we re-entered the runtime system in the frame that
      // created the parallel region.
      ompt_task_info_t *parent_task_info = __ompt_get_taskinfo(0);

      if (ompt_callbacks.ompt_callback(ompt_event_parallel_end)) {
        ompt_callbacks.ompt_callback(ompt_event_parallel_end)(
            parallel_id, parent_task_info->task_id,
            OMPT_INVOKER(fork_context_gnu));
      }

      parent_task_info->frame.reenter_runtime_frame = NULL;

      thr->th.ompt_thread_info.state =
          (((thr->th.th_team)->t.t_serialized) ? ompt_state_work_serial
                                               : ompt_state_work_parallel);
    }
#endif
  }
}

// Loop worksharing constructs

// The Gnu codegen passes in an exclusive upper bound for the overall range,
// but the libguide dispatch code expects an inclusive upper bound, hence the
// "end - incr" 5th argument to KMP_DISPATCH_INIT (and the " ub - str" 11th
// argument to __kmp_GOMP_fork_call).
//
// Conversely, KMP_DISPATCH_NEXT returns and inclusive upper bound in *p_ub,
// but the Gnu codegen expects an excluside upper bound, so the adjustment
// "*p_ub += stride" compenstates for the discrepancy.
//
// Correction: the gnu codegen always adjusts the upper bound by +-1, not the
// stride value.  We adjust the dispatch parameters accordingly (by +-1), but
// we still adjust p_ub by the actual stride value.
//
// The "runtime" versions do not take a chunk_sz parameter.
//
// The profile lib cannot support construct checking of unordered loops that
// are predetermined by the compiler to be statically scheduled, as the gcc
// codegen will not always emit calls to GOMP_loop_static_next() to get the
// next iteration.  Instead, it emits inline code to call omp_get_thread_num()
// num and calculate the iteration space using the result.  It doesn't do this
// with ordered static loop, so they can be checked.

#define LOOP_START(func, schedule)                                             \
  int func(long lb, long ub, long str, long chunk_sz, long *p_lb,              \
           long *p_ub) {                                                       \
    int status;                                                                \
    long stride;                                                               \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20,                                                               \
             (#func ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n", \
              gtid, lb, ub, str, chunk_sz));                                   \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,        \
                        (schedule) != kmp_sch_static);                         \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",   \
              gtid, *p_lb, *p_ub, status));                                    \
    return status;                                                             \
  }

#define LOOP_RUNTIME_START(func, schedule)                                     \
  int func(long lb, long ub, long str, long *p_lb, long *p_ub) {               \
    int status;                                                                \
    long stride;                                                               \
    long chunk_sz = 0;                                                         \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20,                                                               \
             (#func ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz %d\n",    \
              gtid, lb, ub, str, chunk_sz));                                   \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz, TRUE); \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",   \
              gtid, *p_lb, *p_ub, status));                                    \
    return status;                                                             \
  }

#define LOOP_NEXT(func, fini_code)                                             \
  int func(long *p_lb, long *p_ub) {                                           \
    int status;                                                                \
    long stride;                                                               \
    int gtid = __kmp_get_gtid();                                               \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20, (#func ": T#%d\n", gtid));                                    \
                                                                               \
    fini_code status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,    \
                                         (kmp_int *)p_ub, (kmp_int *)&stride); \
    if (status) {                                                              \
      *p_ub += (stride > 0) ? 1 : -1;                                          \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, stride 0x%lx, "    \
                    "returning %d\n",                                          \
              gtid, *p_lb, *p_ub, stride, status));                            \
    return status;                                                             \
  }

LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_STATIC_START), kmp_sch_static)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_STATIC_NEXT), {})
LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_DYNAMIC_START),
           kmp_sch_dynamic_chunked)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT), {})
LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_GUIDED_START), kmp_sch_guided_chunked)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT), {})
LOOP_RUNTIME_START(xexpand(KMP_API_NAME_GOMP_LOOP_RUNTIME_START),
                   kmp_sch_runtime)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT), {})

LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START), kmp_ord_static)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START),
           kmp_ord_dynamic_chunked)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_START(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START),
           kmp_ord_guided_chunked)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_RUNTIME_START(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START),
                   kmp_ord_runtime)
LOOP_NEXT(xexpand(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })

void xexpand(KMP_API_NAME_GOMP_LOOP_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_loop_end: T#%d\n", gtid))

  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  KA_TRACE(20, ("GOMP_loop_end exit: T#%d\n", gtid))
}

void xexpand(KMP_API_NAME_GOMP_LOOP_END_NOWAIT)(void) {
  KA_TRACE(20, ("GOMP_loop_end_nowait: T#%d\n", __kmp_get_gtid()))
}

// Unsigned long long loop worksharing constructs
//
// These are new with gcc 4.4

#define LOOP_START_ULL(func, schedule)                                         \
  int func(int up, unsigned long long lb, unsigned long long ub,               \
           unsigned long long str, unsigned long long chunk_sz,                \
           unsigned long long *p_lb, unsigned long long *p_ub) {               \
    int status;                                                                \
    long long str2 = up ? ((long long)str) : -((long long)str);                \
    long long stride;                                                          \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (#func                                                                 \
         ": T#%d, up %d, lb 0x%llx, ub 0x%llx, str 0x%llx, chunk_sz 0x%llx\n", \
         gtid, up, lb, ub, str, chunk_sz));                                    \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str2 > 0) ? (ub - 1) : (ub + 1), str2, chunk_sz,  \
                            (schedule) != kmp_sch_static);                     \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str2);                                      \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n", \
              gtid, *p_lb, *p_ub, status));                                    \
    return status;                                                             \
  }

#define LOOP_RUNTIME_START_ULL(func, schedule)                                 \
  int func(int up, unsigned long long lb, unsigned long long ub,               \
           unsigned long long str, unsigned long long *p_lb,                   \
           unsigned long long *p_ub) {                                         \
    int status;                                                                \
    long long str2 = up ? ((long long)str) : -((long long)str);                \
    unsigned long long stride;                                                 \
    unsigned long long chunk_sz = 0;                                           \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (#func                                                                 \
         ": T#%d, up %d, lb 0x%llx, ub 0x%llx, str 0x%llx, chunk_sz 0x%llx\n", \
         gtid, up, lb, ub, str, chunk_sz));                                    \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str2 > 0) ? (ub - 1) : (ub + 1), str2, chunk_sz,  \
                            TRUE);                                             \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT((long long)stride == str2);                           \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n", \
              gtid, *p_lb, *p_ub, status));                                    \
    return status;                                                             \
  }

#define LOOP_NEXT_ULL(func, fini_code)                                         \
  int func(unsigned long long *p_lb, unsigned long long *p_ub) {               \
    int status;                                                                \
    long long stride;                                                          \
    int gtid = __kmp_get_gtid();                                               \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20, (#func ": T#%d\n", gtid));                                    \
                                                                               \
    fini_code status =                                                         \
        KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,            \
                              (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);       \
    if (status) {                                                              \
      *p_ub += (stride > 0) ? 1 : -1;                                          \
    }                                                                          \
                                                                               \
    KA_TRACE(20,                                                               \
             (#func " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, stride 0x%llx, " \
                    "returning %d\n",                                          \
              gtid, *p_lb, *p_ub, stride, status));                            \
    return status;                                                             \
  }

LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START), kmp_sch_static)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT), {})
LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START),
               kmp_sch_dynamic_chunked)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT), {})
LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START),
               kmp_sch_guided_chunked)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT), {})
LOOP_RUNTIME_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START),
                       kmp_sch_runtime)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT), {})

LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START),
               kmp_ord_static)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START),
               kmp_ord_dynamic_chunked)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_START_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START),
               kmp_ord_guided_chunked)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_RUNTIME_START_ULL(
    xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START), kmp_ord_runtime)
LOOP_NEXT_ULL(xexpand(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })

// Combined parallel / loop worksharing constructs
//
// There are no ull versions (yet).

#define PARALLEL_LOOP_START(func, schedule, ompt_pre, ompt_post)               \
  void func(void (*task)(void *), void *data, unsigned num_threads, long lb,   \
            long ub, long str, long chunk_sz) {                                \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20,                                                               \
             (#func ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n", \
              gtid, lb, ub, str, chunk_sz));                                   \
                                                                               \
    ompt_pre();                                                                \
                                                                               \
    if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {                       \
      if (num_threads != 0) {                                                  \
        __kmp_push_num_threads(&loc, gtid, num_threads);                       \
      }                                                                        \
      __kmp_GOMP_fork_call(&loc, gtid, task,                                   \
                           (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, \
                           9, task, data, num_threads, &loc, (schedule), lb,   \
                           (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz);    \
    } else {                                                                   \
      __kmp_GOMP_serialized_parallel(&loc, gtid, task);                        \
    }                                                                          \
                                                                               \
    KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                              \
                      (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,          \
                      (schedule) != kmp_sch_static);                           \
                                                                               \
    ompt_post();                                                               \
                                                                               \
    KA_TRACE(20, (#func " exit: T#%d\n", gtid));                               \
  }

#if OMPT_SUPPORT

#define OMPT_LOOP_PRE()                                                        \
  ompt_frame_t *parent_frame;                                                  \
  if (ompt_enabled) {                                                          \
    parent_frame = __ompt_get_task_frame_internal(0);                          \
    parent_frame->reenter_runtime_frame = __builtin_frame_address(1);          \
  }

#define OMPT_LOOP_POST()                                                       \
  if (ompt_enabled) {                                                          \
    parent_frame->reenter_runtime_frame = NULL;                                \
  }

#else

#define OMPT_LOOP_PRE()

#define OMPT_LOOP_POST()

#endif

PARALLEL_LOOP_START(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START),
                    kmp_sch_static, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START),
                    kmp_sch_dynamic_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START),
                    kmp_sch_guided_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START),
                    kmp_sch_runtime, OMPT_LOOP_PRE, OMPT_LOOP_POST)

// Tasking constructs

void xexpand(KMP_API_NAME_GOMP_TASK)(void (*func)(void *), void *data,
                                     void (*copy_func)(void *, void *),
                                     long arg_size, long arg_align,
                                     bool if_cond, unsigned gomp_flags
#if OMP_40_ENABLED
                                     ,
                                     void **depend
#endif
                                     ) {
  MKLOC(loc, "GOMP_task");
  int gtid = __kmp_entry_gtid();
  kmp_int32 flags = 0;
  kmp_tasking_flags_t *input_flags = (kmp_tasking_flags_t *)&flags;

  KA_TRACE(20, ("GOMP_task: T#%d\n", gtid));

  // The low-order bit is the "untied" flag
  if (!(gomp_flags & 1)) {
    input_flags->tiedness = 1;
  }
  // The second low-order bit is the "final" flag
  if (gomp_flags & 2) {
    input_flags->final = 1;
  }
  input_flags->native = 1;
  // __kmp_task_alloc() sets up all other flags

  if (!if_cond) {
    arg_size = 0;
  }

  kmp_task_t *task = __kmp_task_alloc(
      &loc, gtid, input_flags, sizeof(kmp_task_t),
      arg_size ? arg_size + arg_align - 1 : 0, (kmp_routine_entry_t)func);

  if (arg_size > 0) {
    if (arg_align > 0) {
      task->shareds = (void *)((((size_t)task->shareds) + arg_align - 1) /
                               arg_align * arg_align);
    }
    // else error??

    if (copy_func) {
      (*copy_func)(task->shareds, data);
    } else {
      KMP_MEMCPY(task->shareds, data, arg_size);
    }
  }

  if (if_cond) {
#if OMP_40_ENABLED
    if (gomp_flags & 8) {
      KMP_ASSERT(depend);
      const size_t ndeps = (kmp_intptr_t)depend[0];
      const size_t nout = (kmp_intptr_t)depend[1];
      kmp_depend_info_t dep_list[ndeps];

      for (size_t i = 0U; i < ndeps; i++) {
        dep_list[i].base_addr = (kmp_intptr_t)depend[2U + i];
        dep_list[i].len = 0U;
        dep_list[i].flags.in = 1;
        dep_list[i].flags.out = (i < nout);
      }
      __kmpc_omp_task_with_deps(&loc, gtid, task, ndeps, dep_list, 0, NULL);
    } else
#endif
      __kmpc_omp_task(&loc, gtid, task);
  } else {
#if OMPT_SUPPORT
    ompt_thread_info_t oldInfo;
    kmp_info_t *thread;
    kmp_taskdata_t *taskdata;
    if (ompt_enabled) {
      // Store the threads states and restore them after the task
      thread = __kmp_threads[gtid];
      taskdata = KMP_TASK_TO_TASKDATA(task);
      oldInfo = thread->th.ompt_thread_info;
      thread->th.ompt_thread_info.wait_id = 0;
      thread->th.ompt_thread_info.state = ompt_state_work_parallel;
      taskdata->ompt_task_info.frame.exit_runtime_frame =
          __builtin_frame_address(0);
    }
#endif

    __kmpc_omp_task_begin_if0(&loc, gtid, task);
    func(data);
    __kmpc_omp_task_complete_if0(&loc, gtid, task);

#if OMPT_SUPPORT
    if (ompt_enabled) {
      thread->th.ompt_thread_info = oldInfo;
      taskdata->ompt_task_info.frame.exit_runtime_frame = NULL;
    }
#endif
  }

  KA_TRACE(20, ("GOMP_task exit: T#%d\n", gtid));
}

void xexpand(KMP_API_NAME_GOMP_TASKWAIT)(void) {
  MKLOC(loc, "GOMP_taskwait");
  int gtid = __kmp_entry_gtid();

  KA_TRACE(20, ("GOMP_taskwait: T#%d\n", gtid));

  __kmpc_omp_taskwait(&loc, gtid);

  KA_TRACE(20, ("GOMP_taskwait exit: T#%d\n", gtid));
}

// Sections worksharing constructs
//
// For the sections construct, we initialize a dynamically scheduled loop
// worksharing construct with lb 1 and stride 1, and use the iteration #'s
// that its returns as sections ids.
//
// There are no special entry points for ordered sections, so we always use
// the dynamically scheduled workshare, even if the sections aren't ordered.

unsigned xexpand(KMP_API_NAME_GOMP_SECTIONS_START)(unsigned count) {
  int status;
  kmp_int lb, ub, stride;
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_sections_start");
  KA_TRACE(20, ("GOMP_sections_start: T#%d\n", gtid));

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, &lb, &ub, &stride);
  if (status) {
    KMP_DEBUG_ASSERT(stride == 1);
    KMP_DEBUG_ASSERT(lb > 0);
    KMP_ASSERT(lb == ub);
  } else {
    lb = 0;
  }

  KA_TRACE(20, ("GOMP_sections_start exit: T#%d returning %u\n", gtid,
                (unsigned)lb));
  return (unsigned)lb;
}

unsigned xexpand(KMP_API_NAME_GOMP_SECTIONS_NEXT)(void) {
  int status;
  kmp_int lb, ub, stride;
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_sections_next");
  KA_TRACE(20, ("GOMP_sections_next: T#%d\n", gtid));

  status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, &lb, &ub, &stride);
  if (status) {
    KMP_DEBUG_ASSERT(stride == 1);
    KMP_DEBUG_ASSERT(lb > 0);
    KMP_ASSERT(lb == ub);
  } else {
    lb = 0;
  }

  KA_TRACE(
      20, ("GOMP_sections_next exit: T#%d returning %u\n", gtid, (unsigned)lb));
  return (unsigned)lb;
}

void xexpand(KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START)(void (*task)(void *),
                                                        void *data,
                                                        unsigned num_threads,
                                                        unsigned count) {
  int gtid = __kmp_entry_gtid();

#if OMPT_SUPPORT
  ompt_frame_t *parent_frame;

  if (ompt_enabled) {
    parent_frame = __ompt_get_task_frame_internal(0);
    parent_frame->reenter_runtime_frame = __builtin_frame_address(1);
  }
#endif

  MKLOC(loc, "GOMP_parallel_sections_start");
  KA_TRACE(20, ("GOMP_parallel_sections_start: T#%d\n", gtid));

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, 9,
                         task, data, num_threads, &loc, kmp_nm_dynamic_chunked,
                         (kmp_int)1, (kmp_int)count, (kmp_int)1, (kmp_int)1);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

#if OMPT_SUPPORT
  if (ompt_enabled) {
    parent_frame->reenter_runtime_frame = NULL;
  }
#endif

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  KA_TRACE(20, ("GOMP_parallel_sections_start exit: T#%d\n", gtid));
}

void xexpand(KMP_API_NAME_GOMP_SECTIONS_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_sections_end: T#%d\n", gtid))

  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  KA_TRACE(20, ("GOMP_sections_end exit: T#%d\n", gtid))
}

void xexpand(KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT)(void) {
  KA_TRACE(20, ("GOMP_sections_end_nowait: T#%d\n", __kmp_get_gtid()))
}

// libgomp has an empty function for GOMP_taskyield as of 2013-10-10
void xexpand(KMP_API_NAME_GOMP_TASKYIELD)(void) {
  KA_TRACE(20, ("GOMP_taskyield: T#%d\n", __kmp_get_gtid()))
  return;
}

#if OMP_40_ENABLED // these are new GOMP_4.0 entry points

void xexpand(KMP_API_NAME_GOMP_PARALLEL)(void (*task)(void *), void *data,
                                         unsigned num_threads,
                                         unsigned int flags) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_parallel");
  KA_TRACE(20, ("GOMP_parallel: T#%d\n", gtid));

#if OMPT_SUPPORT
  ompt_task_info_t *parent_task_info, *task_info;
  if (ompt_enabled) {
    parent_task_info = __ompt_get_taskinfo(0);
    parent_task_info->frame.reenter_runtime_frame = __builtin_frame_address(1);
  }
#endif
  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    if (flags != 0) {
      __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_microtask_wrapper, 2, task,
                         data);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }
#if OMPT_SUPPORT
  if (ompt_enabled) {
    task_info = __ompt_get_taskinfo(0);
    task_info->frame.exit_runtime_frame = __builtin_frame_address(0);
  }
#endif
  task(data);
  xexpand(KMP_API_NAME_GOMP_PARALLEL_END)();
#if OMPT_SUPPORT
  if (ompt_enabled) {
    task_info->frame.exit_runtime_frame = NULL;
    parent_task_info->frame.reenter_runtime_frame = NULL;
  }
#endif
}

void xexpand(KMP_API_NAME_GOMP_PARALLEL_SECTIONS)(void (*task)(void *),
                                                  void *data,
                                                  unsigned num_threads,
                                                  unsigned count,
                                                  unsigned flags) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_parallel_sections");
  KA_TRACE(20, ("GOMP_parallel_sections: T#%d\n", gtid));

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    if (flags != 0) {
      __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, 9,
                         task, data, num_threads, &loc, kmp_nm_dynamic_chunked,
                         (kmp_int)1, (kmp_int)count, (kmp_int)1, (kmp_int)1);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  task(data);
  xexpand(KMP_API_NAME_GOMP_PARALLEL_END)();
  KA_TRACE(20, ("GOMP_parallel_sections exit: T#%d\n", gtid));
}

#define PARALLEL_LOOP(func, schedule, ompt_pre, ompt_post)                     \
  void func(void (*task)(void *), void *data, unsigned num_threads, long lb,   \
            long ub, long str, long chunk_sz, unsigned flags) {                \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, #func);                                                         \
    KA_TRACE(20,                                                               \
             (#func ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n", \
              gtid, lb, ub, str, chunk_sz));                                   \
                                                                               \
    ompt_pre();                                                                \
    if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {                       \
      if (num_threads != 0) {                                                  \
        __kmp_push_num_threads(&loc, gtid, num_threads);                       \
      }                                                                        \
      if (flags != 0) {                                                        \
        __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);              \
      }                                                                        \
      __kmp_GOMP_fork_call(&loc, gtid, task,                                   \
                           (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, \
                           9, task, data, num_threads, &loc, (schedule), lb,   \
                           (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz);    \
    } else {                                                                   \
      __kmp_GOMP_serialized_parallel(&loc, gtid, task);                        \
    }                                                                          \
                                                                               \
    KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                              \
                      (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,          \
                      (schedule) != kmp_sch_static);                           \
    task(data);                                                                \
    xexpand(KMP_API_NAME_GOMP_PARALLEL_END)();                                 \
    ompt_post();                                                               \
                                                                               \
    KA_TRACE(20, (#func " exit: T#%d\n", gtid));                               \
  }

PARALLEL_LOOP(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC), kmp_sch_static,
              OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC),
              kmp_sch_dynamic_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED),
              kmp_sch_guided_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(xexpand(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME), kmp_sch_runtime,
              OMPT_LOOP_PRE, OMPT_LOOP_POST)

void xexpand(KMP_API_NAME_GOMP_TASKGROUP_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_taskgroup_start");
  KA_TRACE(20, ("GOMP_taskgroup_start: T#%d\n", gtid));

  __kmpc_taskgroup(&loc, gtid);

  return;
}

void xexpand(KMP_API_NAME_GOMP_TASKGROUP_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_taskgroup_end");
  KA_TRACE(20, ("GOMP_taskgroup_end: T#%d\n", gtid));

  __kmpc_end_taskgroup(&loc, gtid);

  return;
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    kmp_int32
    __kmp_gomp_to_omp_cancellation_kind(int gomp_kind) {
  kmp_int32 cncl_kind = 0;
  switch (gomp_kind) {
  case 1:
    cncl_kind = cancel_parallel;
    break;
  case 2:
    cncl_kind = cancel_loop;
    break;
  case 4:
    cncl_kind = cancel_sections;
    break;
  case 8:
    cncl_kind = cancel_taskgroup;
    break;
  }
  return cncl_kind;
}

bool xexpand(KMP_API_NAME_GOMP_CANCELLATION_POINT)(int which) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_cancellation_point");
  KA_TRACE(20, ("GOMP_cancellation_point: T#%d\n", gtid));

  kmp_int32 cncl_kind = __kmp_gomp_to_omp_cancellation_kind(which);

  return __kmpc_cancellationpoint(&loc, gtid, cncl_kind);
}

bool xexpand(KMP_API_NAME_GOMP_BARRIER_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  KMP_FATAL(NoGompCancellation);
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_barrier_cancel");
  KA_TRACE(20, ("GOMP_barrier_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

bool xexpand(KMP_API_NAME_GOMP_CANCEL)(int which, bool do_cancel) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  } else {
    return FALSE;
  }

  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_cancel");
  KA_TRACE(20, ("GOMP_cancel: T#%d\n", gtid));

  kmp_int32 cncl_kind = __kmp_gomp_to_omp_cancellation_kind(which);

  if (do_cancel == FALSE) {
    return xexpand(KMP_API_NAME_GOMP_CANCELLATION_POINT)(which);
  } else {
    return __kmpc_cancel(&loc, gtid, cncl_kind);
  }
}

bool xexpand(KMP_API_NAME_GOMP_SECTIONS_END_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_sections_end_cancel");
  KA_TRACE(20, ("GOMP_sections_end_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

bool xexpand(KMP_API_NAME_GOMP_LOOP_END_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_loop_end_cancel");
  KA_TRACE(20, ("GOMP_loop_end_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

// All target functions are empty as of 2014-05-29
void xexpand(KMP_API_NAME_GOMP_TARGET)(int device, void (*fn)(void *),
                                       const void *openmp_target, size_t mapnum,
                                       void **hostaddrs, size_t *sizes,
                                       unsigned char *kinds) {
  return;
}

void xexpand(KMP_API_NAME_GOMP_TARGET_DATA)(int device,
                                            const void *openmp_target,
                                            size_t mapnum, void **hostaddrs,
                                            size_t *sizes,
                                            unsigned char *kinds) {
  return;
}

void xexpand(KMP_API_NAME_GOMP_TARGET_END_DATA)(void) { return; }

void xexpand(KMP_API_NAME_GOMP_TARGET_UPDATE)(int device,
                                              const void *openmp_target,
                                              size_t mapnum, void **hostaddrs,
                                              size_t *sizes,
                                              unsigned char *kinds) {
  return;
}

void xexpand(KMP_API_NAME_GOMP_TEAMS)(unsigned int num_teams,
                                      unsigned int thread_limit) {
  return;
}
#endif // OMP_40_ENABLED

/* The following sections of code create aliases for the GOMP_* functions, then
   create versioned symbols using the assembler directive .symver. This is only
   pertinent for ELF .so library xaliasify and xversionify are defined in
   kmp_ftn_os.h  */

#ifdef KMP_USE_VERSION_SYMBOLS

// GOMP_1.0 aliases
xaliasify(KMP_API_NAME_GOMP_ATOMIC_END, 10);
xaliasify(KMP_API_NAME_GOMP_ATOMIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_BARRIER, 10);
xaliasify(KMP_API_NAME_GOMP_CRITICAL_END, 10);
xaliasify(KMP_API_NAME_GOMP_CRITICAL_NAME_END, 10);
xaliasify(KMP_API_NAME_GOMP_CRITICAL_NAME_START, 10);
xaliasify(KMP_API_NAME_GOMP_CRITICAL_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_DYNAMIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_END, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_END_NOWAIT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_GUIDED_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_RUNTIME_START, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_STATIC_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_LOOP_STATIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_ORDERED_END, 10);
xaliasify(KMP_API_NAME_GOMP_ORDERED_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_END, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START, 10);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_START, 10);
xaliasify(KMP_API_NAME_GOMP_SECTIONS_END, 10);
xaliasify(KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT, 10);
xaliasify(KMP_API_NAME_GOMP_SECTIONS_NEXT, 10);
xaliasify(KMP_API_NAME_GOMP_SECTIONS_START, 10);
xaliasify(KMP_API_NAME_GOMP_SINGLE_COPY_END, 10);
xaliasify(KMP_API_NAME_GOMP_SINGLE_COPY_START, 10);
xaliasify(KMP_API_NAME_GOMP_SINGLE_START, 10);

// GOMP_2.0 aliases
xaliasify(KMP_API_NAME_GOMP_TASK, 20);
xaliasify(KMP_API_NAME_GOMP_TASKWAIT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT, 20);
xaliasify(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START, 20);

// GOMP_3.0 aliases
xaliasify(KMP_API_NAME_GOMP_TASKYIELD, 30);

// GOMP_4.0 aliases
// The GOMP_parallel* entry points below aren't OpenMP 4.0 related.
#if OMP_40_ENABLED
xaliasify(KMP_API_NAME_GOMP_PARALLEL, 40);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_SECTIONS, 40);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC, 40);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED, 40);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME, 40);
xaliasify(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC, 40);
xaliasify(KMP_API_NAME_GOMP_TASKGROUP_START, 40);
xaliasify(KMP_API_NAME_GOMP_TASKGROUP_END, 40);
xaliasify(KMP_API_NAME_GOMP_BARRIER_CANCEL, 40);
xaliasify(KMP_API_NAME_GOMP_CANCEL, 40);
xaliasify(KMP_API_NAME_GOMP_CANCELLATION_POINT, 40);
xaliasify(KMP_API_NAME_GOMP_LOOP_END_CANCEL, 40);
xaliasify(KMP_API_NAME_GOMP_SECTIONS_END_CANCEL, 40);
xaliasify(KMP_API_NAME_GOMP_TARGET, 40);
xaliasify(KMP_API_NAME_GOMP_TARGET_DATA, 40);
xaliasify(KMP_API_NAME_GOMP_TARGET_END_DATA, 40);
xaliasify(KMP_API_NAME_GOMP_TARGET_UPDATE, 40);
xaliasify(KMP_API_NAME_GOMP_TEAMS, 40);
#endif

// GOMP_1.0 versioned symbols
xversionify(KMP_API_NAME_GOMP_ATOMIC_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_ATOMIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_BARRIER, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_CRITICAL_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_CRITICAL_NAME_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_CRITICAL_NAME_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_CRITICAL_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_DYNAMIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_END_NOWAIT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_GUIDED_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_RUNTIME_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_STATIC_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_LOOP_STATIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_ORDERED_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_ORDERED_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SECTIONS_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SECTIONS_NEXT, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SECTIONS_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SINGLE_COPY_END, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SINGLE_COPY_START, 10, "GOMP_1.0");
xversionify(KMP_API_NAME_GOMP_SINGLE_START, 10, "GOMP_1.0");

// GOMP_2.0 versioned symbols
xversionify(KMP_API_NAME_GOMP_TASK, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_TASKWAIT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT, 20, "GOMP_2.0");
xversionify(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START, 20, "GOMP_2.0");

// GOMP_3.0 versioned symbols
xversionify(KMP_API_NAME_GOMP_TASKYIELD, 30, "GOMP_3.0");

// GOMP_4.0 versioned symbols
#if OMP_40_ENABLED
xversionify(KMP_API_NAME_GOMP_PARALLEL, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_SECTIONS, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TASKGROUP_START, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TASKGROUP_END, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_BARRIER_CANCEL, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_CANCEL, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_CANCELLATION_POINT, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_LOOP_END_CANCEL, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_SECTIONS_END_CANCEL, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TARGET, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TARGET_DATA, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TARGET_END_DATA, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TARGET_UPDATE, 40, "GOMP_4.0");
xversionify(KMP_API_NAME_GOMP_TEAMS, 40, "GOMP_4.0");
#endif

#endif // KMP_USE_VERSION_SYMBOLS

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
