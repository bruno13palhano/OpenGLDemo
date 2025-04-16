cmake_minimum_required(VERSION 3.22.1)

project("OpenGLDemo")

# Specify the include directory for native_app_glue
set(NATIVE_APP_GLUE_INCLUDE "${ANDROID_NDK}/sources/android/native_app_glue")
set(NATIVE_APP_GLUE_SOURCE "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
# NDK include path for EGL
set(NDK_SYSROOT_INCLUDE "${ANDROID_NDK}/sysroot/usr/include")

add_library(
        native-lib
        SHARED
        native-lib.cpp
        ${NATIVE_APP_GLUE_SOURCE}
)

# Include directories
target_include_directories(
        native-lib
        PRIVATE
        ${NATIVE_APP_GLUE_INCLUDE}
        ${NDK_SYSROOT_INCLUDE}
)

set(CMAKE_CXX_STANDARD 11) # For std::vector, cmath
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(log-lib log)
find_library(GLESv2-lib GLESv2)
find_library(android-lib android)
find_library(egl-lib EGL)

target_link_libraries(
        native-lib
        ${log-lib}
        ${GLESv2-lib}
        ${android-lib}
        ${egl-lib}
)