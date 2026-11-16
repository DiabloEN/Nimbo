set(openh264_tools "${PROJECT_SOURCE_DIR}/.tools/openh264-2.6.0")
set(codec "${openh264_tools}/openh264-2.6.0/codec")
set(nasm "${openh264_tools}/nasm-2.16.03/nasm.exe")
if(NOT EXISTS "${codec}/api/wels/codec_api.h" OR NOT EXISTS "${nasm}")
    message(FATAL_ERROR "Run scripts/bootstrap-openh264.ps1 before building Windows media.")
endif()
# Only the encoder, pixel processing and shared primitives are linked into the client.
file(GLOB cpp "${codec}/common/src/*.cpp" "${codec}/encoder/core/src/*.cpp"
    "${codec}/encoder/plus/src/*.cpp" "${codec}/processing/src/*/*.cpp")
file(GLOB asm "${codec}/common/x86/*.asm" "${codec}/encoder/core/x86/*.asm" "${codec}/processing/src/x86/*.asm")
list(REMOVE_ITEM asm "${codec}/common/x86/asm_inc.asm")
set(objects)
foreach(source IN LISTS asm)
    file(RELATIVE_PATH relative "${codec}" "${source}")
    string(REPLACE "/" "_" name "${relative}")
    set(object "${CMAKE_CURRENT_BINARY_DIR}/${name}.obj")
    add_custom_command(OUTPUT "${object}" COMMAND "${nasm}" -f win64 -DWIN64 -DHAVE_AVX2
        "-I${codec}/common/x86/" -o "${object}" "${source}"
        DEPENDS "${source}" "${codec}/common/x86/asm_inc.asm" VERBATIM)
    list(APPEND objects "${object}")
endforeach()
add_library(nimbo_h264 STATIC ${cpp} ${objects})
set_target_properties(nimbo_h264 PROPERTIES AUTOMOC OFF)
target_compile_definitions(nimbo_h264 PRIVATE X86_ASM HAVE_AVX2 _CRT_SECURE_NO_WARNINGS)
target_include_directories(nimbo_h264 PUBLIC "${codec}/api" PRIVATE "${codec}/api/wels"
    "${codec}/common/inc" "${codec}/encoder/core/inc" "${codec}/encoder/plus/inc"
    "${codec}/processing/interface" "${codec}/processing/src/common" "${codec}/processing/src/downsample"
    "${codec}/processing/src/adaptivequantization" "${codec}/processing/src/scrolldetection" "${codec}/processing/src/vaacalc")
