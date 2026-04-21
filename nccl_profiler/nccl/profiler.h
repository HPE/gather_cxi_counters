/*************************************************************************
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
 * See LICENSE.txt for license information
 * Adapted from profiler/latency-measurer/nccl/profiler_v5.h and profiler.h.
 * Only the v5 API is included — this plugin exports ncclProfiler_v5 and
 * has no need for backward-compat v1–v4 definitions.
 ************************************************************************/

#ifndef PROFILER_H_
#define PROFILER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>  // pid_t
#include "common.h"

// ---------------------------------------------------------------------------
// Event type bitmask flags (passed via eActivationMask in init())
// ---------------------------------------------------------------------------
enum {
  ncclProfileGroup          = (1 << 0),
  ncclProfileColl           = (1 << 1),
  ncclProfileP2p            = (1 << 2),
  ncclProfileProxyOp        = (1 << 3),
  ncclProfileProxyStep      = (1 << 4),
  ncclProfileProxyCtrl      = (1 << 5),
  ncclProfileKernelCh       = (1 << 6),
  ncclProfileNetPlugin      = (1 << 7),
  ncclProfileGroupApi       = (1 << 8),
  ncclProfileCollApi        = (1 << 9),
  ncclProfileP2pApi         = (1 << 10),
  ncclProfileKernelLaunch   = (1 << 11),
};

// ---------------------------------------------------------------------------
// Event state transitions reported via recordEventState()
// ---------------------------------------------------------------------------
typedef enum {
  ncclProfilerProxyOpSendPosted        = 0,
  ncclProfilerProxyOpSendRemFifoWait   = 1,
  ncclProfilerProxyOpSendTransmitted   = 2,
  ncclProfilerProxyOpSendDone          = 3,
  ncclProfilerProxyOpRecvPosted        = 4,
  ncclProfilerProxyOpRecvReceived      = 5,
  ncclProfilerProxyOpRecvTransmitted   = 6,
  ncclProfilerProxyOpRecvDone          = 7,
  ncclProfilerProxyOpInProgress_v4     = 19,

  ncclProfilerProxyStepSendGPUWait     = 8,
  ncclProfilerProxyStepSendPeerWait_v4 = 20,
  ncclProfilerProxyStepSendWait        = 9,
  ncclProfilerProxyStepRecvWait        = 10,
  ncclProfilerProxyStepRecvFlushWait   = 11,
  ncclProfilerProxyStepRecvGPUWait     = 12,

  ncclProfilerProxyCtrlIdle            = 13,
  ncclProfilerProxyCtrlActive          = 14,
  ncclProfilerProxyCtrlSleep           = 15,
  ncclProfilerProxyCtrlWakeup          = 16,
  ncclProfilerProxyCtrlAppend          = 17,
  ncclProfilerProxyCtrlAppendEnd       = 18,

  ncclProfilerNetPluginUpdate          = 21,
  ncclProfilerKernelChStop             = 22,
  ncclProfilerEndGroupApiStart         = 23,
  ncclProfilerBeginGroupApiEnd         = 24
} ncclProfilerEventState_t;

// ---------------------------------------------------------------------------
// v5 event descriptor — passed to startEvent()
// ---------------------------------------------------------------------------
typedef struct {
  uint64_t type;
  void*    parentObj;
  int      rank;
  union {
    struct { int graphCaptured; int groupDepth; } groupApi;
    struct {
      const char* func; size_t count; const char* datatype;
      int root; void* stream; bool graphCaptured;
    } collApi;
    struct {
      const char* func; size_t count; const char* datatype;
      void* stream; bool graphCaptured;
    } p2pApi;
    struct { void* stream; } kernelLaunch;
    struct {
      uint64_t seqNumber; const char* func;
      void const* sendBuff; void* recvBuff;
      size_t count; int root; const char* datatype;
      uint8_t nChannels; uint8_t nWarps; const char* algo; const char* proto;
      void* parentGroup;
    } coll;
    struct {
      const char* func; void* buff; const char* datatype;
      size_t count; int peer; uint8_t nChannels; void* parentGroup;
    } p2p;
    struct {
      pid_t   pid;
      uint8_t channelId;
      int     peer;
      int     nSteps;
      int     chunkSize;
      int     isSend;
    } proxyOp;
    struct { int step; } proxyStep;
    struct { uint8_t channelId; uint64_t pTimer; } kernelCh;
    struct { int64_t id; void* data; } netPlugin;
  };
} ncclProfilerEventDescr_t;

// ---------------------------------------------------------------------------
// v5 state-change args — passed to recordEventState()
// ---------------------------------------------------------------------------
typedef union {
  struct { size_t transSize; } proxyStep;
  struct { int appendedProxyOps; } proxyCtrl;
  struct { void* data; } netPlugin;
  struct { uint64_t pTimer; } kernelCh;
} ncclProfilerEventStateArgs_t;

// ---------------------------------------------------------------------------
// v4 profiler vtable — init has commHash & eActivationMask in different order
// ---------------------------------------------------------------------------
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** context, int* eActivationMask,
                       const char* commName, uint64_t commHash,
                       int nNodes, int nranks, int rank,
                       ncclDebugLogger_t logfn);
  ncclResult_t (*startEvent)(void* context, void** eHandle,
                             ncclProfilerEventDescr_t* eDescr);
  ncclResult_t (*stopEvent)(void* eHandle);
  ncclResult_t (*recordEventState)(void* eHandle,
                                   ncclProfilerEventState_t eState,
                                   ncclProfilerEventStateArgs_t* eStateArgs);
  ncclResult_t (*finalize)(void* context);
} ncclProfiler_v4_t;

// ---------------------------------------------------------------------------
// v5 profiler vtable — commId replaces commHash, eActivationMask moves earlier
// ---------------------------------------------------------------------------
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** context, uint64_t commId, int* eActivationMask,
                       const char* commName, int nNodes, int nranks, int rank,
                       ncclDebugLogger_t logfn);
  ncclResult_t (*startEvent)(void* context, void** eHandle,
                             ncclProfilerEventDescr_t* eDescr);
  ncclResult_t (*stopEvent)(void* eHandle);
  ncclResult_t (*recordEventState)(void* eHandle,
                                   ncclProfilerEventState_t eState,
                                   ncclProfilerEventStateArgs_t* eStateArgs);
  ncclResult_t (*finalize)(void* context);
} ncclProfiler_t;

#endif // PROFILER_H_
