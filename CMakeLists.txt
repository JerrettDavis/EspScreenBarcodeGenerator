cmake_minimum_required(VERSION 3.20)
project(EspScreenBarcodeGeneratorNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(ESPBARCODE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(ESPBARCODE_CORE_SOURCES
    lib/EspBarcodeCore/src/EspBarcodeCore.cpp
    lib/EspBarcodeCore/src/LinearEncoders.cpp
    lib/EspBarcodeCore/src/MatrixEncoders.cpp
    lib/EspBarcodeCore/src/AztecEncoder.cpp
    lib/EspBarcodeCore/src/QrEncoder.cpp
    lib/EspBarcodeCore/src/RandomPayload.cpp
)

add_library(EspBarcodeCore STATIC ${ESPBARCODE_CORE_SOURCES})
target_include_directories(EspBarcodeCore PUBLIC lib/EspBarcodeCore/src)
target_compile_definitions(EspBarcodeCore PUBLIC ESPBARCODE_NATIVE=1)

if(MSVC)
    target_compile_options(EspBarcodeCore PRIVATE /W4 /WX)
else()
    target_compile_options(EspBarcodeCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

function(espbarcode_enable_sanitizers target)
    if(ESPBARCODE_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

espbarcode_enable_sanitizers(EspBarcodeCore)

set(ESPLINKCORE_SOURCES
    lib/EspLinkCore/src/Crc32.cpp
    lib/EspLinkCore/src/Envelope.cpp
    lib/EspLinkCore/src/HopFrame.cpp
    lib/EspLinkCore/src/Cobs.cpp
    lib/EspLinkCore/src/FrameAssembler.cpp
)
add_library(EspLinkCore STATIC ${ESPLINKCORE_SOURCES})
target_include_directories(EspLinkCore PUBLIC lib/EspLinkCore/src)
target_link_libraries(EspLinkCore PUBLIC EspBarcodeCore)
if(MSVC)
    target_compile_options(EspLinkCore PRIVATE /W4 /WX)
else()
    target_compile_options(EspLinkCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
espbarcode_enable_sanitizers(EspLinkCore)

add_executable(native_core_tests tests/native_core_tests.cpp)
target_link_libraries(native_core_tests PRIVATE EspBarcodeCore)
espbarcode_enable_sanitizers(native_core_tests)

add_executable(esplink_types_tests tests/esplink_types_tests.cpp)
target_link_libraries(esplink_types_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_types_tests)

add_executable(esplink_selection_tests tests/esplink_selection_tests.cpp)
target_link_libraries(esplink_selection_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_selection_tests)

add_executable(esplink_golden_shape_tests tests/esplink_golden_shape_tests.cpp)
target_link_libraries(esplink_golden_shape_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_golden_shape_tests)

add_executable(esplink_codec_tests tests/esplink_codec_tests.cpp)
target_link_libraries(esplink_codec_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_codec_tests)

add_executable(barcode_native tools/native_cli.cpp)
target_link_libraries(barcode_native PRIVATE EspBarcodeCore)
espbarcode_enable_sanitizers(barcode_native)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME native_core_tests COMMAND native_core_tests)
    add_test(NAME esplink_types_tests COMMAND esplink_types_tests)
    add_test(NAME esplink_selection_tests COMMAND esplink_selection_tests)
    add_test(NAME esplink_golden_shape_tests COMMAND esplink_golden_shape_tests)
    add_test(NAME esplink_codec_tests COMMAND esplink_codec_tests)
endif()
