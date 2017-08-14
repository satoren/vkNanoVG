# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# build native_app_glue as a static lib
set(APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
include_directories(${APP_GLUE_DIR})
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(WRAPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vulkan_wrapper)
include_directories(${WRAPPER_DIR})


set(APP_NANOVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../nanovg/)

add_library(nanovg STATIC ${APP_NANOVG_DIR}/src/nanovg.c)
include_directories(${APP_NANOVG_DIR}/src ${APP_NANOVG_DIR}/example)


set(APP_VKNANOVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/)
include_directories(${APP_VKNANOVG_DIR})


# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNVG_NO_GPU_TIMER")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror -DVK_USE_PLATFORM_ANDROID_KHR -DNVG_NO_GPU_TIMER")

add_library( # Sets the name of the library.
             vulkan_example

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             src/main/cpp/vknanovgexample.cpp
             src/main/cpp/appmain.cpp
             ${APP_NANOVG_DIR}/example/demo.c
             ${APP_NANOVG_DIR}/example/perf.c
            ${WRAPPER_DIR}/vulkan_wrapper.cpp )


target_link_libraries(vulkan_example
                       android
                       native_app_glue
                       log
                       nanovg)


add_library( # Sets the name of the library.
             gles2_example

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             src/main/cpp/nanovg_gles2_example.cpp
             src/main/cpp/appmain.cpp
             ${APP_NANOVG_DIR}/example/demo.c
             ${APP_NANOVG_DIR}/example/perf.c)


target_link_libraries(gles2_example
                       android
                       EGL
                       GLESv2
                       native_app_glue
                       log
                       nanovg)