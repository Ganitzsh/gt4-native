# eeref: the original game code as a test reference (tools/eeref/build.cmd builds it; docs/DIFFERENTIAL_TESTING.md).
# Configured twice: first for eerecomp, the translator; then, with GT4_EEREF_GEN pointing at its output, for
# eeref.exe, the translated units with the replay tool. Needs clang (the generated code uses vector extensions).
cmake_minimum_required(VERSION 3.24)
project(gt4_eeref CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RECOMP ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ps2-web-recomp)

add_executable(eerecomp
    ${RECOMP}/recomp/main.cpp
    ${RECOMP}/recomp/r5900.cpp
    ${RECOMP}/recomp/elf.cpp
    ${RECOMP}/recomp/analysis.cpp
    ${RECOMP}/recomp/codegen.cpp
    ${RECOMP}/recomp/snr2.cpp)

if(GT4_EEREF_GEN)
    file(TO_CMAKE_PATH "${GT4_EEREF_GEN}" GT4_EEREF_GEN)
    file(GLOB GEN_SOURCES ${GT4_EEREF_GEN}/*.cpp)
    # VU0 microprograms run on the native game's interpreter; shim/ maps its guest addresses into eeref's memory
    add_executable(eeref eeref.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../runtime/native/vu0.cpp ${GEN_SOURCES})
    target_include_directories(eeref PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim ${RECOMP}/runtime/include ${GT4_EEREF_GEN})
    # the units are large switch-and-goto functions: optimise without the slowest passes
    target_compile_options(eeref PRIVATE /O1 -Wno-everything)
endif()
