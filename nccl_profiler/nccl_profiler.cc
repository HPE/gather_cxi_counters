/*
 * nccl_profiler.cc
 *
 * RCCL/NCCL profiler plugin that accumulates per-channel proxy-step
 * statistics directly into shared-memory counters (ShmNcclCounters).
 *
 * Subscribes to ProxyOp + ProxyStep to preserve per-step (point-to-point)
 * resolution.  ProxyOp provides the channel_id + direction metadata;
 * ProxyStep provides the actual transfer timing and byte counts.
 *
 * Hot-path cost per proxy step:
 *   - 1 × mono_ns() on start     ~5 ns   (vDSO, no syscall)
 *   - 1 × mono_ns() on stop      ~5 ns
 *   - 3 × fetch_add              ~60 ns  (count, bytes, duration)
 *   - 1 × CAS for max            ~20 ns  (rare contention)
 *   - pool alloc + free           ~6 ns   (bitmask)
 *   Total per step:              ~96 ns
 *   Per allreduce (~76 steps):   ~7.3 µs  = 0.33% of 2200 µs
 *
 * NO ring buffer, NO memcpy, NO IPC messages, NO system calls on hot path.
 * All handle memory is pre-allocated at communicator init.
 *
 * Build output: librccl-profiler-gather.so
 * Activation:   export NCCL_PROFILER_PLUGIN=gather
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "nccl/err.h"
#include "nccl/profiler.h"
#include "nccl_counters.h"

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static inline int64_t mono_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// Handle types and pools
// ---------------------------------------------------------------------------

static constexpr int OP_POOL_SIZE   = 16;   // 4 ch × 2 dir = 8 live ops max
static constexpr int STEP_POOL_SIZE = 32;   // steps are sequential per op

enum EventTag : uint8_t { TAG_PROXYOP = 0, TAG_PROXYSTEP = 1 };


struct ProxyOpHandle {
    EventTag tag;           // TAG_PROXYOP
    uint8_t  channel_id;
    uint8_t  direction;     // 0=recv, 1=send
    uint8_t  _pad;
    void*    ctx_ptr;       // ProfilerContext*
};

struct ProxyStepHandle {
    EventTag       tag;     // TAG_PROXYSTEP
    uint8_t        _pad[3];
    ProxyOpHandle* parent_op;
    int64_t        start_mono;
    uint64_t       trans_size;
};

// O(1) bitmap pool — __builtin_ctz picks first free slot in one instruction.
template<typename T, int N>
struct Pool {
    static_assert(N <= 32, "Pool capacity must be <= 32 for uint32_t bitmask");
    T        slots[N];
    uint32_t free;
    Pool() : free(N == 32 ? 0xFFFFFFFFu : (1u << N) - 1) {}
    T* alloc() {
        if (!free) return nullptr;
        int i = __builtin_ctz(free);
        free &= ~(1u << i);
        return &slots[i];
    }
    void release(T* p) {
        int idx = static_cast<int>(p - slots);
        if (idx >= 0 && idx < N) free |= (1u << idx);
    }
};

struct ProfilerContext {
    ShmNcclCounters* counters = nullptr;
    int              shm_fd   = -1;
    int              rank     = 0;
    pid_t            pid      = 0;

    Pool<ProxyOpHandle,   OP_POOL_SIZE>   op_pool;
    Pool<ProxyStepHandle, STEP_POOL_SIZE> step_pool;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static std::atomic<int> g_initialized{0};
static pid_t            g_pid = 0;
static pthread_mutex_t  g_init_lock = PTHREAD_MUTEX_INITIALIZER;


// ---------------------------------------------------------------------------
// Plugin callbacks
// ---------------------------------------------------------------------------

static ncclResult_t gatherInit(void** context, uint64_t /*commId*/,
                               int* eActivationMask,
                               const char* /*commName*/, int /*nNodes*/,
                               int /*nranks*/, int rank,
                               ncclDebugLogger_t /*logfn*/) {
    pthread_mutex_lock(&g_init_lock);
    if (g_initialized.fetch_add(1) == 0) {
        g_pid = getpid();
    }
    pthread_mutex_unlock(&g_init_lock);

    // Subscribe to BOTH ProxyOp (for channel_id + direction metadata)
    // AND ProxyStep (for per-step timing and byte counts — p2p resolution).
    *eActivationMask = ncclProfileProxyOp | ncclProfileProxyStep;

    const char* shm_name = getenv("GATHER_CXI_SHM_PATH");
    if (!shm_name) {
        fprintf(stderr, "[NCCL-SHM-PROFILER] GATHER_CXI_SHM_PATH not set; "
                        "events will be discarded.\n");
        *context = nullptr;
        return ncclSuccess;
    }

    int fd = shm_open(shm_name, O_RDWR, 0);
    if (fd < 0) {
        fprintf(stderr, "[NCCL-SHM-PROFILER] shm_open(%s) failed: %s\n",
                shm_name, strerror(errno));
        *context = nullptr;
        return ncclSuccess;
    }

    void* ptr = mmap(nullptr, sizeof(ShmNcclCounters),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "[NCCL-SHM-PROFILER] mmap failed: %s\n",
                strerror(errno));
        close(fd);
        *context = nullptr;
        return ncclSuccess;
    }

    auto* ctx      = new ProfilerContext();
    ctx->counters  = (ShmNcclCounters*)ptr;
    ctx->shm_fd    = fd;
    ctx->rank      = rank;
    ctx->pid       = g_pid;

    *context = ctx;
    return ncclSuccess;
}

static ncclResult_t gatherFinalize(void* context) {
    if (!context) return ncclSuccess;
    auto* ctx = (ProfilerContext*)context;
    if (ctx->counters) munmap(ctx->counters, sizeof(ShmNcclCounters));
    if (ctx->shm_fd >= 0) close(ctx->shm_fd);
    g_initialized.fetch_sub(1);
    delete ctx;
    return ncclSuccess;
}


static ncclResult_t startEvent_v2(void* context, void** eHandle,
                                   ncclProfilerEventDescr_t* eDescr) {
    *eHandle = nullptr;
    if (!context) return ncclSuccess;
    auto* ctx = (ProfilerContext*)context;

    if (eDescr->type == ncclProfileProxyOp) {
        if (eDescr->proxyOp.pid != ctx->pid) return ncclSuccess;
        auto* h = ctx->op_pool.alloc();
        if (!h) return ncclSuccess;
        h->tag        = TAG_PROXYOP;
        h->channel_id = eDescr->proxyOp.channelId;
        h->direction  = eDescr->proxyOp.isSend ? 1 : 0;
        h->ctx_ptr    = ctx;
        *eHandle = h;

    } else if (eDescr->type == ncclProfileProxyStep) {
        if (!eDescr->parentObj) return ncclSuccess;
        auto* parent = (ProxyOpHandle*)eDescr->parentObj;
        if (parent->tag != TAG_PROXYOP) return ncclSuccess;
        auto* h = ctx->step_pool.alloc();
        if (!h) return ncclSuccess;
        h->tag        = TAG_PROXYSTEP;
        h->parent_op  = parent;
        h->start_mono = mono_ns();
        h->trans_size = 0;
        *eHandle = h;
    }

    return ncclSuccess;
}

static ncclResult_t stopEvent_v2(void* eHandle) {
    if (!eHandle) return ncclSuccess;
    EventTag tag = *(EventTag*)eHandle;

    if (tag == TAG_PROXYOP) {
        auto* h   = (ProxyOpHandle*)eHandle;
        auto* ctx = (ProfilerContext*)h->ctx_ptr;
        if (ctx) ctx->op_pool.release(h);

    } else if (tag == TAG_PROXYSTEP) {
        auto* h   = (ProxyStepHandle*)eHandle;
        auto* op  = h->parent_op;
        auto* ctx = op ? (ProfilerContext*)op->ctx_ptr : nullptr;

        if (ctx && ctx->counters && op->channel_id < MAX_NCCL_CHANNELS) {
            uint64_t dur_ns = (uint64_t)(mono_ns() - h->start_mono);
            auto& ch = ctx->counters->channels[op->channel_id];

            if (op->direction == 1) {   // SEND
                ch.send_count.fetch_add(1,            std::memory_order_relaxed);
                ch.send_bytes.fetch_add(h->trans_size, std::memory_order_relaxed);
                ch.send_duration_ns.fetch_add(dur_ns,  std::memory_order_relaxed);
                atomic_max_u64(ch.send_max_dur_ns, dur_ns);
            } else {                     // RECV
                ch.recv_count.fetch_add(1,            std::memory_order_relaxed);
                ch.recv_bytes.fetch_add(h->trans_size, std::memory_order_relaxed);
                ch.recv_duration_ns.fetch_add(dur_ns,  std::memory_order_relaxed);
                atomic_max_u64(ch.recv_max_dur_ns, dur_ns);
            }
        }
        if (ctx) ctx->step_pool.release(h);
    }

    return ncclSuccess;
}

static ncclResult_t recordState_v2(void* eHandle,
                                    ncclProfilerEventState_t /*eState*/,
                                    ncclProfilerEventStateArgs_t* args) {
    if (!eHandle) return ncclSuccess;
    if (*(EventTag*)eHandle == TAG_PROXYSTEP && args) {
        ((ProxyStepHandle*)eHandle)->trans_size +=
            (uint64_t)args->proxyStep.transSize;
    }
    return ncclSuccess;
}

// ---------------------------------------------------------------------------
// v4-compatible init
// ---------------------------------------------------------------------------
static ncclResult_t gatherInit_v4(void** context, int* eActivationMask,
                                   const char* commName, uint64_t /*commHash*/,
                                   int nNodes, int nranks, int rank,
                                   ncclDebugLogger_t logfn) {
    return gatherInit(context, /*commId=*/0, eActivationMask,
                      commName, nNodes, nranks, rank, logfn);
}

// ---------------------------------------------------------------------------
// Exported symbols
// ---------------------------------------------------------------------------

__attribute__((visibility("default")))
ncclProfiler_v4_t ncclProfiler_v4 = {
    /* name               */ "gather",
    /* init               */ gatherInit_v4,
    /* startEvent         */ startEvent_v2,
    /* stopEvent          */ stopEvent_v2,
    /* recordEventState   */ recordState_v2,
    /* finalize           */ gatherFinalize,
};

__attribute__((visibility("default")))
ncclProfiler_t ncclProfiler_v5 = {
    /* name               */ "gather",
    /* init               */ gatherInit,
    /* startEvent         */ startEvent_v2,
    /* stopEvent          */ stopEvent_v2,
    /* recordEventState   */ recordState_v2,
    /* finalize           */ gatherFinalize,
};
