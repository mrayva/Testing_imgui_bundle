set(Threads_FOUND TRUE)
set(CMAKE_THREAD_LIBS_INIT "")
set(CMAKE_USE_PTHREADS_INIT 0)
set(CMAKE_HAVE_THREADS_LIBRARY 1)

if(NOT TARGET Threads::Threads)
    add_library(Threads::Threads INTERFACE IMPORTED)
endif()

message(STATUS "WASM: Using dummy FindThreads.cmake to satisfy dependencies")
