cmake_minimum_required(VERSION 3.16)
project(docx_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Third-party sources
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Check that deps were downloaded
foreach(dep miniz.h miniz.c miniz_common.h miniz_tdef.h miniz_tinfl.h miniz_zip.h
            pugixml.hpp pugixml.cpp pugiconfig.hpp stb_image_write.h stb_truetype.h stb_image.h)
    if(NOT EXISTS "${THIRD_PARTY_DIR}/${dep}")
        message(FATAL_ERROR "Missing ${dep}. Run: powershell -ExecutionPolicy Bypass -File download_deps.ps1")
    endif()
endforeach()

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

# Core library (new LibreOffice-compatible types)
add_library(docx_core STATIC
    src/core/bparr.cpp
    src/core/node.cpp
    src/core/ndarr.cpp
    src/core/format.cpp
    src/core/doc.cpp
    src/frame/frame.cpp
    src/frame/frmtree.cpp
    src/filter/docx_parser.cpp
    src/layout/layact.cpp
    src/render/render_log.cpp
    src/render/render_output_device.cpp
)
target_include_directories(docx_core PUBLIC src/core src/frame src/filter src/layout src/render ${THIRD_PARTY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sw/source/core/inc)  # 共享 instruction_builder.h / render_instruction.h
target_compile_definitions(docx_core PRIVATE
    _CRT_SECURE_NO_WARNINGS=1
)

# Main executable
add_executable(docx_reader
    src/main.cpp
    src/xml_util.cpp
    src/document.cpp
    src/docx_reader.cpp
    src/font_engine.cpp
    src/frame.cpp
    src/layout.cpp
    src/renderer.cpp
    src/pdf_writer.cpp
)
target_include_directories(docx_reader PRIVATE src src/core ${THIRD_PARTY_DIR})
target_link_libraries(docx_reader PRIVATE miniz pugixml docx_core)
target_compile_definitions(docx_reader PRIVATE
    _CRT_SECURE_NO_WARNINGS=1
    _USE_MATH_DEFINES=1
)

# Test executable
add_executable(docx_test
    test/test_main.cpp
    src/xml_util.cpp
    src/document.cpp
    src/docx_reader.cpp
    src/font_engine.cpp
    src/frame.cpp
    src/layout.cpp
    src/renderer.cpp
)
target_include_directories(docx_test PRIVATE src src/core ${THIRD_PARTY_DIR})
target_link_libraries(docx_test PRIVATE miniz pugixml docx_core)
target_compile_definitions(docx_test PRIVATE
    _CRT_SECURE_NO_WARNINGS=1
    _USE_MATH_DEFINES=1
)

# End-to-end test (SwDoc pipeline)
add_executable(docx_e2e_test test/test_end_to_end.cpp)
target_include_directories(docx_e2e_test PRIVATE src/core src/frame src/filter src/layout src/render ${THIRD_PARTY_DIR})
target_link_libraries(docx_e2e_test PRIVATE miniz pugixml docx_core)
target_compile_definitions(docx_e2e_test PRIVATE _CRT_SECURE_NO_WARNINGS=1)

# Comparison tool (standalone, no external deps)
add_executable(render_diff tools/render_diff.cpp)
target_compile_definitions(render_diff PRIVATE _CRT_SECURE_NO_WARNINGS=1)
if(MSVC)
    target_compile_options(render_diff PRIVATE /W3 /utf-8)
endif()

# MSVC specific
if(MSVC)
    target_compile_options(docx_reader PRIVATE /W3 /utf-8)
    target_compile_options(docx_test PRIVATE /W3 /utf-8)
    target_compile_options(docx_e2e_test PRIVATE /W3 /utf-8)
endif()
