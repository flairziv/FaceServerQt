# FaceServerQt 项目部署与开发指南

## 目录

[TOC]

---

## 1. 概述

本文档提供 **VisionGuard-Face 人脸识别身份认证系统** 在 Linux 环境（Ubuntu 20.04/22.04/24.04）下的完整部署与开发指南，包括：

- 系统依赖安装
- dlib 源码编译与配置
- MySQL 数据库配置
- 项目构建与运行
- 前端开发环境配置
- API 接口测试

**技术栈：**

- **后端**：C++ 17、Qt 5.14+、dlib、OpenCV、cpp-httplib、jwt-cpp
- **前端**：Vue 3、Vite、TypeScript
- **数据库**：MySQL 8.0
- **开发环境**：Ubuntu 22.04 LTS（推荐）

---

## 2. 系统依赖安装

### 2.1 更新系统包

```bash
sudo apt update
sudo apt upgrade -y
```

### 2.2 安装基础开发工具

```bash
sudo apt install -y build-essential cmake git pkg-config wget curl unzip
```

### 2.3 安装 Qt 5 开发库

```bash
sudo apt install -y \
    qtbase5-dev \
    qtbase5-dev-tools \
    libqt5sql5 \
    libqt5sql5-mysql
```

#### 验证安装

```bash
qmake --version
```

### 2.4 安装 MySQL 客户端开发库

```bash
sudo apt install -y libmysqlclient-dev
```

### 2.5 安装 OpenCV 开发库

```bash
sudo apt install -y \
    libopencv-dev \
    libopencv-core-dev \
    libopencv-imgproc-dev \
    libopencv-imgcodecs-dev \
    libopencv-highgui-dev
```

## 3. 安装 dlib

### 3.1 从源码编译安装（推荐）

```bash
# 克隆 dlib 仓库
git clone https://github.com/davisking/dlib.git
cd dlib

# 创建构建目录
mkdir build && cd build

# CMake 配置（启用 AVX 指令优化）
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_AVX_INSTRUCTIONS=ON

# 编译（使用所有 CPU 核心）
cmake --build . --config Release -j$(nproc)
# 如果有卡顿或者报错可以尝试一个cpu核心编译
cmake --build . --config Release -j1

# 安装到系统（需要 sudo）
sudo cmake --install . 

# 更新动态链接库缓存
sudo ldconfig

# 返回项目根目录
cd ../../
```

如果你有 NVIDIA GPU 并安装了 CUDA，可以添加 `-DDLIB_USE_CUDA=ON` 参数启用 GPU 加速。

### 3.2 验证 dlib 安装

创建测试文件 `test_dlib.cpp`：

```c++
#include <dlib/dnn.h>
#include <iostream>

int main() {
    std::cout << "dlib version:  " << DLIB_VERSION << std::endl;
    return 0;
}
```

编译并运行：

```bash
g++ test_dlib.cpp -o test_dlib -std=c++17 -ldlib -lpthread
./test_dlib
# 输出：dlib version: 19.24
```

## 4. 准备第三方库

### 4.1 下载 cpp-httplib（单头文件）

```bash
# 创建第三方库目录
mkdir -p third_party
cd third_party

# 下载 httplib.h
wget -O httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h

cd ..
```

### 4.2 克隆 jwt-cpp（用于 JWT Token）

```bash
cd third_party

# 克隆 jwt-cpp 仓库
git clone https://github.com/Thalhammer/jwt-cpp.git

cd .. 
```

**说明：** jwt-cpp 是 header-only 库，无需编译，只需在 CMakeLists.txt 中添加 include 路径。

## 5. 下载 dlib 预训练模型

### 5.1 创建模型目录

```bash
mkdir -p models
cd models
```

### 5.2 下载模型文件

```bash
# 下载 68 点人脸关键点检测器
wget http://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2

# 下载 ResNet-34 人脸识别模型
wget http://dlib.net/files/dlib_face_recognition_resnet_model_v1.dat.bz2

# 解压
bunzip2 shape_predictor_68_face_landmarks.dat.bz2
bunzip2 dlib_face_recognition_resnet_model_v1.dat.bz2

# 返回项目根目录
cd ..
```

**验证：**

```bash
ls -lh models/
# 输出：
# shape_predictor_68_face_landmarks.dat (约 99 MB)
# dlib_face_recognition_resnet_model_v1.dat (约 22 MB)
```

## 6. MySQL 数据库配置

### 6.1 安装 MySQL Server

```bash
sudo apt install -y mysql-server
```

### 6.2 安全配置（可选）

```bash
sudo mysql_secure_installation
```

按提示完成以下配置：

- 设置 root 密码
- 移除匿名用户
- 禁止 root 远程登录
- 删除测试数据库

### 6.3 创建数据库与用户

登录 MySQL：

```bash
sudo mysql -u root -p
```

执行以下 SQL 语句：

```sql
-- 创建数据库
CREATE DATABASE face_recognition_db 
    CHARACTER SET utf8mb4 
    COLLATE utf8mb4_unicode_ci;

-- 创建用户
CREATE USER 'faceuser'@'localhost' IDENTIFIED BY 'FacePass2025';

-- 授予权限
GRANT ALL PRIVILEGES ON face_recognition_db.* TO 'faceuser'@'localhost';

-- 刷新权限
FLUSH PRIVILEGES;

-- 退出
EXIT;
```

### 6.4 创建数据表

```sql
USE face_recognition_db;

CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) UNIQUE NOT NULL,
    face_descriptor BLOB DEFAULT NULL COMMENT '128维人脸特征向量',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP NULL,
    INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

### 6.5 验证数据库连接（可选）

```bash
mysql -u faceuser -p face_recognition_db
# 输入密码：FacePass2025

# 在 MySQL 提示符下执行：
SHOW TABLES;
DESCRIBE users;
EXIT;
```

## 7. 项目结构

```
FaceServerQt/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── FaceRecognizer.cpp
│   ├── DatabaseManager. cpp
│   └── JwtHelper.cpp
├── include/
│   ├── FaceRecognizer.h
│   ├── DatabaseManager.h
│   └── JwtHelper.h
├── third_party/
│   ├── httplib.h
│   └── jwt-cpp/
│   └── dlib/  (可选如果没有安装在系统，就需要放置在项目中)
├── models/
│   ├── shape_predictor_68_face_landmarks.dat
│   └── dlib_face_recognition_resnet_model_v1.dat
└── build/  (构建生成)
```

## 8. CMakeLists.txt 配置

创建 `CMakeLists.txt`：

```cmake
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

```

## 9. 构建项目

### 9.1 编译项目

```bash
# 在项目根目录下
mkdir build && cd build

# 配置 CMake
cmake ..

# 使用所有 CPU 核心编译
cmake --build . -j$(nproc)
# 如果有卡顿或者报错可以尝试一个cpu核心编译
cmake --build . -j1
```

**预期输出：**

```
[ 25%] Building CXX object CMakeFiles/FaceServer.dir/src/main.cpp.o
[ 50%] Building CXX object CMakeFiles/FaceServer.dir/src/FaceRecognizer.cpp.o
[ 75%] Building CXX object CMakeFiles/FaceServer.dir/src/DatabaseManager.cpp.o
[100%] Linking CXX executable FaceServer
[100%] Built target FaceServer
```

### 9.2 验证构建

```bash
ls -lh FaceServer
# 输出：-rwxr-xr-x 1 user user 2.5M Dec 15 10:30 FaceServer

# 检查动态链接库依赖
ldd FaceServer
```

## 10. 运行后端服务

### 10.1 启动服务器

```bash
# 在 build 目录下
./FaceServer
```

**预期输出：**

```
========================================
  人脸识别服务器 - VisionGuard-Face
========================================
✅ 数据库连接成功
✅ 人脸识别模型加载成功
🚀 HTTP 服务器启动在 http://0.0.0.0:3000
```

## 11. API 接口测试

### 健康检查

```bash
curl http://localhost:3000/api/health
```

**预期响应：**

```json
{
  "status": "ok",
  "message": "服务运行正常",
  "timestamp": "2024-12-15T10:30:00"
}
```

## 12. 常见问题与解决方案

**MySQL报错**

```
========================================
  人脸识别服务器 - C++ Qt + dlib 版本
========================================
QSqlDatabase: QMYSQL driver not loaded
QSqlDatabase: available drivers: QSQLITE QODBC QODBC3 QPSQL QPSQL7
❌ 数据库连接失败: "Driver not loaded Driver not loaded"
数据库初始化失败，退出
```

**查看程序编译时使用什么版本的Qt**

**版本不匹配导致驱动无法加载！**

我这里是使用了**自己安装在 `/opt/Qt5.14.2/` 的 Qt 5.14.2**，但系统的 MySQL 驱动是为 **系统 Qt 5.15.3** 编译的（在 `/usr/lib/x86_64-linux-gnu/qt5/plugins/`）。

```bash
qmake --version
Using Qt version 5.14.2 in /opt/Qt5.14.2/5.14.2/gcc_64/lib
```

## 解决方案：为你的 Qt 5.14.2 编译 MySQL 插件

### 从源码编译 Qt MySQL 插件

```bash
# 1. 安装编译依赖
sudo apt install build-essential libmysqlclient-dev -y

# 2. 下载 Qt 5.14.2 源码（SQL drivers 部分）
mkdir -p ~/qt_build
cd ~/qt_build
wget https://download.qt. io/archive/qt/5.14/5.14.2/submodules/qtbase-everywhere-src-5.14.2.tar.xz
tar -xf qtbase-everywhere-src-5.14.2.tar.xz

# 3. 进入 SQL drivers 目录
cd qtbase-everywhere-src-5.14.2/src/plugins/sqldrivers

# 4. 使用你的 Qt 5.14.2 的 qmake 编译 MySQL 插件
/opt/Qt5.14.2/5.14.2/gcc_64/bin/qmake -- MYSQL_PREFIX=/usr

# 5. 编译
make

# 6. 找到编译好的插件
find . -name "libqsqlmysql.so"

# 7. 复制到你的 Qt 5.14.2 插件目录
sudo mkdir -p /opt/Qt5.14.2/5.14.2/gcc_64/plugins/sqldrivers
sudo cp plugins/sqldrivers/libqsqlmysql.so /opt/Qt5.14.2/5.14.2/gcc_64/plugins/sqldrivers/

# 8. 验证
ls -la /opt/Qt5.14.2/5.14.2/gcc_64/plugins/sqldrivers/
```

**预期输出：**

```bash
drwxr-xr-x  2 root root    4096 12月  7 06:24 .
drwxr-xr-x 32 root root    4096 11月 16 01:22 ..
-rwxr-xr-x  1 root root 1315792 11月 16 01:22 libqsqlite.so
-rwxr-xr-x  1 root root  111360 12月  7 06:24 libqsqlmysql.so
-rwxr-xr-x  1 root root  119336 11月 16 01:22 libqsqlodbc.so
-rwxr-xr-x  1 root root  115120 11月 16 01:22 libqsqlpsql.so
```

### 执行后测试

```bash
cd ~/QtBase/FaceServerQt/build
./FaceServer
```

