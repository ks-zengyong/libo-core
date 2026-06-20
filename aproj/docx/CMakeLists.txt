cmake_minimum_required(VERSION 3.16)
project(docx_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 统一输出目录：所有 dll/lib/exe 产物输出到 aproj/docx/output
set(DOCX_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DOCX_OUTPUT_DIR})  # exe (Windows) / so (Linux)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DOCX_OUTPUT_DIR})  # dll (Windows) / dylib (macOS)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${DOCX_OUTPUT_DIR})  # lib (Windows) / a (Linux)

# 多配置生成器 (Visual Studio/Xcode) 默认会追加 Debug/Release 子目录，
# 这里按配置覆盖为同一目录，使产物直接平铺到 output/ 下。
foreach(CFG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CFG} CFG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${DOCX_OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${DOCX_OUTPUT_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} ${DOCX_OUTPUT_DIR})
endforeach()

# Third-party sources
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Check that deps were downloaded
foreach(dep miniz.h miniz.c miniz_common.h miniz_tdef.h miniz_tinfl.h miniz_zip.h
            pugixml.hpp pugixml.cpp pugiconfig.hpp stb_truetype.h)
    if(NOT EXISTS "${THIRD_PARTY_DIR}/${dep}")
        message(FATAL_ERROR "Missing ${dep}. Run: powershell -ExecutionPolicy Bypass -File download_deps.ps1")
    endif()
endforeach()

# Check for HarfBuzz
if(NOT EXISTS "${THIRD_PARTY_DIR}/harfbuzz/harfbuzz.cc")
    message(FATAL_ERROR "Missing HarfBuzz. Copy from libo-core/workdir/UnpackedTarball/harfbuzz/src/")
endif()

# miniz (C library)
add_library(miniz STATIC
    ${THIRD_PARTY_DIR}/miniz.c
    ${THIRD_PARTY_DIR}/miniz_tdef.c
    ${THIRD_PARTY_DIR}/miniz_tinfl.c
    ${THIRD_PARTY_DIR}/miniz_zip.c
)
target_include_directories(miniz PUBLIC ${THIRD_PARTY_DIR})

# pugixml (C++ library)
add_library(pugixml STATIC ${THIRD_PARTY_DIR}/pugixml.cpp)
target_include_directories(pugixml PUBLIC ${THIRD_PARTY_DIR})
target_compile_definitions(pugixml PUBLIC PUGIXML_NO_EXCEPTIONS=1)

# HarfBuzz (font metrics library)
add_library(harfbuzz STATIC ${THIRD_PARTY_DIR}/harfbuzz/harfbuzz.cc)
target_include_directories(harfbuzz PUBLIC ${THIRD_PARTY_DIR}/harfbuzz)
target_compile_definitions(harfbuzz PRIVATE _CRT_SECURE_NO_WARNINGS=1)
if(MSVC)
    target_compile_options(harfbuzz PRIVATE /W0 /utf-8)
endif()

# Render common (shared rendering utilities used by both sw and aproj)
set(RENDER_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../render_common)
add_library(render_common STATIC
    ${RENDER_COMMON_DIR}/render_format.cxx
    ${RENDER_COMMON_DIR}/frame_tree_walker.cxx
    ${RENDER_COMMON_DIR}/node_walker.cxx
    ${RENDER_COMMON_DIR}/node_format.cxx
)
target_include_directories(render_common PUBLIC ${RENDER_COMMON_DIR})
target_compile_definitions(render_common PRIVATE _CRT_SECURE_NO_WARNINGS=1)
if(MSVC)
    target_compile_options(render_common PRIVATE /W3 /utf-8)
endif()

# Core library (LibreOffice-compatible docx pipeline)
add_library(docx_core STATIC
    src/core/bparr.cpp
    src/core/node.cpp
    src/core/ndarr.cpp
    src/core/format.cpp
    src/core/doc.cpp
    src/frame/frame.cpp
    src/frame/frmtree.cpp
    src/frame/layhelper.cpp   # 新增：SwLayHelper 和 SwActualSection 实现
    src/frame/sortedobjs.cpp  # 新增：SwSortedObjs 浮动对象管理
    src/frame/objectformatter.cpp  # 新增：SwObjectFormatter 浮动对象格式化
    src/filter/docx_parser.cpp
    src/font/font_engine.cpp
    src/layout/layact.cpp
    src/render/render_log.cpp
    src/render/render_output_device.cpp
    src/render/nodes_log.cpp
)
target_include_directories(docx_core PUBLIC src/render src/core src/frame src/filter src/font src/layout ${THIRD_PARTY_DIR}
    ${RENDER_COMMON_DIR})
target_compile_definitions(docx_core PRIVATE _CRT_SECURE_NO_WARNINGS=1)
target_link_libraries(docx_core PUBLIC harfbuzz render_common)

# End-to-end test (SwDoc pipeline)
add_executable(docx_e2e_test test/test_end_to_end.cpp)
target_include_directories(docx_e2e_test PRIVATE src/core src/frame src/filter src/font src/layout src/render ${THIRD_PARTY_DIR} ${RENDER_COMMON_DIR})
target_link_libraries(docx_e2e_test PRIVATE miniz pugixml docx_core)
target_compile_definitions(docx_e2e_test PRIVATE _CRT_SECURE_NO_WARNINGS=1)

# Comparison tool (standalone, no external deps)
add_executable(render_diff tools/render_diff.cpp)
target_include_directories(render_diff PRIVATE ${RENDER_COMMON_DIR})
target_compile_definitions(render_diff PRIVATE _CRT_SECURE_NO_WARNINGS=1)

# Nodes 专用独立对比工具 (与 render_diff 无耦合关系，仅使用 C++17 标准库)
add_executable(node_diff tools/node_diff.cpp)
target_compile_definitions(node_diff PRIVATE _CRT_SECURE_NO_WARNINGS=1)

# Frame 专用独立对比工具 (支持多种对齐算法，替代 render_diff 用于 frame/vcl 对比)
add_executable(frame_diff tools/frame_diff.cpp)
target_include_directories(frame_diff PRIVATE ${RENDER_COMMON_DIR})
target_compile_definitions(frame_diff PRIVATE _CRT_SECURE_NO_WARNINGS=1)

# Debug 编译产物加 _debug 后缀
set_target_properties(docx_e2e_test render_diff node_diff frame_diff PROPERTIES DEBUG_POSTFIX "_debug")

# MSVC specific
if(MSVC)
    target_compile_options(docx_e2e_test PRIVATE /W3 /utf-8)
    target_compile_options(render_diff PRIVATE /W3 /utf-8)
    target_compile_options(node_diff PRIVATE /W3 /utf-8)
    target_compile_options(frame_diff PRIVATE /W3 /utf-8)
endif()
