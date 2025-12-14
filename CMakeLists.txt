cmake_minimum_required(VERSION 3.12)
project(FaceServerQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt 自动处理 MOC、UIC、RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Sql)
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# 优先查找系统 dlib
set(DLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/dlib")
set(DLIB_FOUND FALSE)

# 先尝试系统 dlib
find_package(dlib QUIET CONFIG)
if(dlib_FOUND)
    message(STATUS "找到系统 dlib (CONFIG 模式)")
    if(TARGET dlib::dlib)
        set(DLIB_LIBRARIES dlib::dlib)
    else()
        set(DLIB_LIBRARIES dlib)
    endif()
    set(DLIB_FOUND TRUE)
else()
    # 手动查找系统安装的 dlib
    find_path(DLIB_INCLUDE_DIR dlib/dlib_basic_cpp_build_tutorial. txt
        PATHS /usr/local/include /usr/include
        NO_DEFAULT_PATH)
    find_library(DLIB_LIBRARY NAMES dlib libdlib
        PATHS /usr/local/lib /usr/lib
        NO_DEFAULT_PATH)
    
    if(DLIB_INCLUDE_DIR AND DLIB_LIBRARY)
        message(STATUS "手动找到系统 dlib:")
        message(STATUS "  Include: ${DLIB_INCLUDE_DIR}")
        message(STATUS "  Library: ${DLIB_LIBRARY}")
        set(DLIB_LIBRARIES ${DLIB_LIBRARY})
        set(DLIB_FOUND TRUE)
    else()
        # 最后尝试本地源码
        if(EXISTS "${DLIB_DIR}/dlib/CMakeLists.txt")
            message(STATUS "使用本地 dlib 源码: ${DLIB_DIR}")
            add_subdirectory(${DLIB_DIR}/dlib dlib_build)
            set(DLIB_LIBRARIES dlib:: dlib)
            set(DLIB_FOUND TRUE)
        else()
            message(FATAL_ERROR "未找到 dlib！")
        endif()
    endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/third_party
                    ${CMAKE_SOURCE_DIR}/third_party/jwt-cpp/include
                    ${OpenCV_INCLUDE_DIRS}
                    ${OPENSSL_INCLUDE_DIR}
                    ${DLIB_INCLUDE_DIR})

file(GLOB SRCS src/*.cpp src/*.cxx)
file(GLOB HDRS include/*.h include/*.hpp)

add_executable(FaceServerQt ${SRCS} ${HDRS})

target_include_directories(FaceServerQt PRIVATE 
    ${OpenCV_INCLUDE_DIRS}
    ${DLIB_INCLUDE_DIR})

target_link_libraries(FaceServerQt PRIVATE 
    Qt5::Core 
    Qt5::Sql 
    ${OpenCV_LIBS}
    ${DLIB_LIBRARIES}
    OpenSSL::SSL 
    OpenSSL::Crypto 
    pthread)

file(GLOB MODEL_FILES "${CMAKE_SOURCE_DIR}/models/*.dat")
file(COPY ${MODEL_FILES} DESTINATION ${CMAKE_BINARY_DIR}/models)
