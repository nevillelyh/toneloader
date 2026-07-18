cmake_minimum_required(VERSION 3.24)
project(ToneLoader VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
find_library(LIBZIP_RUNTIME NAMES zip REQUIRED)

set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(USE_NATIVE_ARCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(neuralaudio
  GIT_REPOSITORY https://github.com/mikeoliphant/NeuralAudio.git
  GIT_TAG fec4ea2ca369f58343d005b5e13094556e4fd977
  GIT_SHALLOW FALSE
  GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(neuralaudio)

FetchContent_Declare(fftconvolver
  GIT_REPOSITORY https://github.com/HiFi-LoFi/FFTConvolver.git
  GIT_TAG f2cdeb04c42141d2caec19ca4f137398b2a76b85
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(fftconvolver)

FetchContent_Declare(libxputty
  GIT_REPOSITORY https://github.com/brummer10/libxputty.git
  GIT_TAG 4299c46dfde7ec66ae8309435914fa841a4ac15a)
FetchContent_GetProperties(libxputty)
if(NOT libxputty_POPULATED)
  FetchContent_Populate(libxputty)
endif()
add_custom_target(libxputty_build
  COMMAND ${CMAKE_MAKE_PROGRAM} -C ${libxputty_SOURCE_DIR}
          ECHO=echo B_ECHO=echo R_ECHO=echo
          XXDI=xxd\ -i
  BYPRODUCTS ${libxputty_SOURCE_DIR}/Build/libxputty.a
             ${libxputty_SOURCE_DIR}/xputty/resources/xresources.h
  WORKING_DIRECTORY ${libxputty_SOURCE_DIR})

add_library(fftconvolver_lib STATIC
  ${fftconvolver_SOURCE_DIR}/AudioFFT.cpp
  ${fftconvolver_SOURCE_DIR}/FFTConvolver.cpp
  ${fftconvolver_SOURCE_DIR}/Utilities.cpp)
target_include_directories(fftconvolver_lib PUBLIC ${fftconvolver_SOURCE_DIR})
set_target_properties(fftconvolver_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(toneloader SHARED
  src/plugin.cpp)
target_include_directories(toneloader PRIVATE
  src
  ${neuralaudio_SOURCE_DIR})
target_link_libraries(toneloader PRIVATE
  NeuralAudio
  fftconvolver_lib
  PkgConfig::LV2
  PkgConfig::SNDFILE)
target_compile_options(toneloader PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(toneloader PRIVATE
  -Wl,--version-script=${CMAKE_SOURCE_DIR}/src/toneloader.map)
set_target_properties(toneloader PROPERTIES
  PREFIX ""
  OUTPUT_NAME "toneloader"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

add_library(toneloader_ui SHARED src/ui.cpp src/library.cpp)
add_dependencies(toneloader_ui libxputty_build)
target_include_directories(toneloader_ui PRIVATE src
  ${libxputty_SOURCE_DIR}/Build
  ${libxputty_SOURCE_DIR}/xputty/resources
  ${libxputty_SOURCE_DIR}/xputty/header
  ${libxputty_SOURCE_DIR}/xputty/header/dialogs
  ${libxputty_SOURCE_DIR}/xputty/xdgmime
  ${libxputty_SOURCE_DIR}/xputty/header/widgets)
target_link_libraries(toneloader_ui PRIVATE
  ${libxputty_SOURCE_DIR}/Build/libxputty.a PkgConfig::LV2 PkgConfig::LIBZIP
  PkgConfig::CAIRO PkgConfig::X11 dl pthread)
target_link_options(toneloader_ui PRIVATE
  -Wl,--version-script=${CMAKE_SOURCE_DIR}/src/toneloader-ui.map)
set_target_properties(toneloader_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "toneloader_ui"
  BUILD_RPATH "$ORIGIN"
  INSTALL_RPATH "$ORIGIN")

set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/lv2/ToneLoader.lv2)
set(FACTORY_MODEL ${CMAKE_BINARY_DIR}/factory/Crate_Vintage_Club_20.nam)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/factory)
file(DOWNLOAD
  https://api.tone3000.com/storage/v1/object/public/models/4018638ebc51c7ac_a2.nam
  ${FACTORY_MODEL}
  EXPECTED_HASH SHA256=598ddaa30f89de9ffe020c33d9731083adea05bbf3be062751b3e3f54317bd5b
  TLS_VERIFY ON)

add_custom_command(TARGET toneloader POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:toneloader> ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/manifest.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/toneloader.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/toneloader_ui.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${FACTORY_MODEL} ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSE ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSES/Crate_Vintage_Club_20.CC0.txt ${BUNDLE_DIR}/)
add_custom_command(TARGET toneloader_ui POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:toneloader_ui> ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIBZIP_RUNTIME} ${BUNDLE_DIR}/libzip.so.4)

add_custom_target(bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:toneloader> ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:toneloader_ui> ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/manifest.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/toneloader.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lv2/toneloader_ui.ttl ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${FACTORY_MODEL} ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSE ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSES/Crate_Vintage_Club_20.CC0.txt ${BUNDLE_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIBZIP_RUNTIME} ${BUNDLE_DIR}/libzip.so.4
  DEPENDS toneloader toneloader_ui)

include(CTest)
if(BUILD_TESTING)
  add_executable(library_test tests/library_test.cpp src/library.cpp)
  target_include_directories(library_test PRIVATE src)
  target_link_libraries(library_test PRIVATE PkgConfig::LIBZIP)
  add_test(NAME library_test COMMAND library_test)
endif()

install(DIRECTORY ${BUNDLE_DIR} DESTINATION lib/lv2)
