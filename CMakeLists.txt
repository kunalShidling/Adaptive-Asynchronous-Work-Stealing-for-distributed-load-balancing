cmake_minimum_required(VERSION 3.14)
project(scheduler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build by default for performance measurement
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# Source files
set(SOURCES
    cpp/src/Task.cpp
    cpp/src/TaskQueue.cpp
    cpp/src/CostEstimator.cpp
    cpp/src/RuntimeMonitor.cpp
    cpp/src/StealingPolicy.cpp
    cpp/src/WorkloadGenerator.cpp
    cpp/src/Metrics.cpp
    cpp/src/Scheduler.cpp
    cpp/src/StaticScheduler.cpp
    cpp/src/DynamicScheduler.cpp
    cpp/src/WorkStealingScheduler.cpp
    cpp/src/AdaptiveScheduler.cpp
    cpp/main.cpp
)

add_executable(scheduler ${SOURCES})

target_include_directories(scheduler PRIVATE
    cpp/include
    cpp/vendor
)

# Link threads
find_package(Threads REQUIRED)
target_link_libraries(scheduler PRIVATE Threads::Threads)

# On MinGW, also link stdc++fs for filesystem
if(MINGW)
    target_link_libraries(scheduler PRIVATE stdc++fs)
endif()

# Output to build/
set_target_properties(scheduler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)

# ── Tests ──────────────────────────────────────────────────────────────────────
enable_testing()

# Simple correctness smoke test
add_test(NAME smoke_static
    COMMAND scheduler
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(smoke_static PROPERTIES
    PASS_REGULAR_EXPRESSION "experiment_completed"
    TIMEOUT 60
    ENVIRONMENT "SCHEDULER_INPUT={\"algorithm\":\"static\",\"num_workers\":2,\"num_tasks\":10,\"workload_type\":\"uniform\",\"min_complexity\":100,\"max_complexity\":500,\"random_seed\":42,\"experiment_id\":1}"
)
