# HieroOCR con CMake: macOS (aplicación .app), iOS (.app / .ipa) y, si se quiere,
# Linux. SDL2, SDL2_image y SDL2_ttf se compilan desde las fuentes que deja
# apple/preparar.sh (estáticas, dentro del programa).
#
#   sh apple/preparar.sh
#   sh apple/compilar-macos.sh     # -> apple/HieroOCR-macos.zip (HieroOCR.app, universal)
#   sh apple/compilar-ios.sh       # -> apple/HieroOCR-ios.ipa (sin firmar)
#
# En Linux y Windows sigue valiendo el Makefile y windows/compilar.sh.
cmake_minimum_required(VERSION 3.20)
project(HieroOCR VERSION 1.1.0 LANGUAGES C)
if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/apple/deps)
if(NOT EXISTS ${DEPS}/SDL/CMakeLists.txt)
    message(FATAL_ERROR "Faltan SDL2, SDL2_image y SDL2_ttf: ejecuta antes sh apple/preparar.sh")
endif()

# --- SDL2, SDL2_image y SDL2_ttf, estáticas ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(${DEPS}/SDL SDL EXCLUDE_FROM_ALL)

set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
foreach(fmt AVIF JXL TIF WEBP)
    set(SDL2IMAGE_${fmt} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(${DEPS}/SDL_image SDL_image EXCLUDE_FROM_ALL)

set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
add_subdirectory(${DEPS}/SDL_ttf SDL_ttf EXCLUDE_FROM_ALL)

# --- el programa ---
set(SRC src/main.c src/app.c src/image.c src/segment.c src/classify.c src/mdc.c src/dict.c
        src/train.c src/dataset.c src/gui.c)
if(APPLE)
    list(APPEND SRC src/apple.m)
    set_source_files_properties(src/apple.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(hieroocr MACOSX_BUNDLE ${SRC})
    set_target_properties(hieroocr PROPERTIES
        OUTPUT_NAME HieroOCR
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/apple/Info-ios.plist.in
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
    target_link_libraries(hieroocr PRIVATE SDL2::SDL2main)
    target_link_libraries(hieroocr PRIVATE "-framework UIKit" "-framework PhotosUI"
                          "-framework UniformTypeIdentifiers" "-framework ImageIO" "-framework CoreGraphics")
elseif(APPLE)
    add_executable(hieroocr MACOSX_BUNDLE ${SRC})
    set_target_properties(hieroocr PROPERTIES
        OUTPUT_NAME HieroOCR
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/apple/Info-macos.plist.in)
    target_link_libraries(hieroocr PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers"
                          "-framework ImageIO" "-framework CoreGraphics")
else()
    add_executable(hieroocr ${SRC})
    target_link_libraries(hieroocr PRIVATE m)
endif()
target_compile_definitions(hieroocr PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_compile_options(hieroocr PRIVATE -Wall -Wextra)
target_link_libraries(hieroocr PRIVATE SDL2_ttf::SDL2_ttf-static SDL2_image::SDL2_image-static SDL2::SDL2-static)
