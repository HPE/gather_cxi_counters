cmake_minimum_required(VERSION 3.16)
project(nccl-shm-profiler CXX)

# ---------------------------------------------------------------------------
# Build the RCCL/NCCL profiler plugin as a shared library.
#
# The library is loaded at runtime by RCCL via:
#   export NCCL_PROFILER_PLUGIN=gather
#   export LD_LIBRARY_PATH=<path-to-this-build-dir>:$LD_LIBRARY_PATH
#
# Transport: POSIX shared memory (shm_open/mmap) — no message transport dependency.
# The parent process (gather_cxi_counters) creates a /nccl_gather_<pid> shm
# segment and passes its name via GATHER_CXI_SHM_PATH before fork/exec.
# ---------------------------------------------------------------------------

add_library(rccl-profiler-gather SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/nccl_profiler.cc
)

set_target_properties(rccl-profiler-gather PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME  "rccl-profiler-gather"
    PREFIX       "lib"
)

target_compile_options(rccl-profiler-gather PRIVATE
    -fPIC
    -O2
    -Wall
    -Wno-unused-function
)

target_include_directories(rccl-profiler-gather PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # nccl_counters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/nccl     # nccl/profiler.h etc.
)

# shm_open is in librt on older glibc; a no-op on modern glibc (already in libc).
target_link_libraries(rccl-profiler-gather PRIVATE rt)

# Create the canonical symlink RCCL looks for:  librccl-profiler.so → librccl-profiler-gather.so
add_custom_command(TARGET rccl-profiler-gather POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        $<TARGET_FILE_NAME:rccl-profiler-gather>
        $<TARGET_FILE_DIR:rccl-profiler-gather>/librccl-profiler.so
    COMMENT "Creating librccl-profiler.so symlink"
)
