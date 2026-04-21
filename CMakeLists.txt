cmake_minimum_required(VERSION 3.20)
project(gather_cxi_counters)

set(CMAKE_CXX_STANDARD 20)
# clone git@github.com:zeromq/libzmq.git and build it.
include(FetchContent)

FetchContent_Declare(
        libzmq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG        master # or a specific tag/commit
)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Build libzmq-static with -fPIC so it can be linked into the profiler .so too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_MakeAvailable(libzmq)

add_executable(gather_cxi_counters
    main.cpp
    counter_collection.cpp
    counter_aggregation.cpp
    zmq_transfer.cpp
    utils.cpp
    metric_registry.cpp
    metric_source_nic.cpp
    metric_source_perf.cpp
    metric_source_nccl.cpp
)
target_link_libraries(gather_cxi_counters PRIVATE libzmq-static rt)
target_include_directories(gather_cxi_counters PRIVATE include .)

# Build the RCCL/NCCL profiler plugin shared library.
add_subdirectory(nccl_profiler)
