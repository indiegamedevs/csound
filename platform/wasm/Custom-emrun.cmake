set(USE_PORTAUDIO 0)
set(USE_PORTMIDI 0)
set(HAVE_SPRINTF_L 0)
set(USE_IPMIDI 0)
set(BUILD_TESTS 0)
set(BUILD_STATIC_LIBRARY 1)
set(BUILD_JAVA_INTERFACE OFF)
set(INSTALL_PYTHON_INTERFACE OFF)
set(BUILD_UTILITIES OFF)
set(USE_DOUBLE 0)

set(CMAKE_CXX_COMPILER ${EMSDK}/upstream/emscripten/em++)
set(CMAKE_C_COMPILER ${EMSDK}/upstream/emscripten/emcc)
set(CMAKE_SYSROOT ${EMSDK}/upstream/emscripten/cache/sysroot)

add_compile_definitions(_GNU_SOURCE=1)

if(${CMAKE_BUILD_TYPE} MATCHES "Release")
    set(CMAKE_C_FLAGS "-pthread -Wall -fPIC --emrun --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/commandline@/")
    set(CMAKE_CXX_FLAGS "-pthread -Wall -fPIC --emrun --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/commandline@/")
else()
    set(CMAKE_C_FLAGS "-pthread -Wall -g -fPIC  --emrun --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/commandline@/")
    set(CMAKE_CXX_FLAGS "-pthread -Wall -g -fPIC --emrun --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/commandline@/")
endif()

set(CMAKE_EXECUTABLE_SUFFIX ".html")
