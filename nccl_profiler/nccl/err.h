/*************************************************************************
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
 * See LICENSE.txt for license information
 * Copied from profiler/latency-measurer/nccl/err.h
 ************************************************************************/

#ifndef NCCL_ERR_H_
#define NCCL_ERR_H_

typedef enum { ncclSuccess             =  0,
               ncclUnhandledCudaError  =  1,
               ncclSystemError         =  2,
               ncclInternalError       =  3,
               ncclInvalidArgument     =  4,
               ncclInvalidUsage        =  5,
               ncclRemoteError         =  6 } ncclResult_t;

#endif
