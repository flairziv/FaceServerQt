# FaceServerQt - 人脸识别身份认证系统

基于 C++ Qt + dlib 的高性能人脸识别后端服务。

## ✨ 特性

- 🎯 **高精度识别**：基于 dlib ResNet-34 模型，128维人脸特征向量
- 🚀 **高性能**：C++ 实现，支持 AVX 指令集优化
- 🔐 **JWT 认证**：安全的 Token 身份验证机制
- 💾 **MySQL 存储**：可靠的数据持久化方案
- 📡 **RESTful API**：标准 HTTP 接口，易于集成

## 🛠️ 技术栈

- **语言**: C++ 17
- **框架**: Qt 5.14+
- **人脸识别**: dlib + OpenCV
- **HTTP 服务**: cpp-httplib
- **认证**: jwt-cpp
- **数据库**: MySQL 8.0

## 📦 快速开始

### 环境要求

- Ubuntu 20.04/22.04/24.04
- CMake 3.12+
- Qt 5.14+
- MySQL 8.0+

### 安装依赖

```bash
sudo apt install -y build-essential cmake qtbase5-dev \
    libqt5sql5-mysql libopencv-dev libmysqlclient-dev
```

### 编译 dlib

```bash
git clone https://github.com/davisking/dlib.git
cd dlib && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_AVX_INSTRUCTIONS=ON
cmake --build . -j$(nproc)
sudo cmake --install .
sudo ldconfig
```

### 下载模型文件

```bash
mkdir -p models && cd models
wget http://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2
wget http://dlib.net/files/dlib_face_recognition_resnet_model_v1.dat.bz2
bunzip2 *.bz2
```

### 配置数据库

```sql
CREATE DATABASE face_recognition_db CHARACTER SET utf8mb4;
CREATE USER 'faceuser'@'localhost' IDENTIFIED BY 'FacePass2025';
GRANT ALL PRIVILEGES ON face_recognition_db.* TO 'faceuser'@'localhost';
FLUSH PRIVILEGES;

USE face_recognition_db;
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) UNIQUE NOT NULL,
    face_descriptor BLOB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP NULL
);
```

### 构建项目

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### 运行服务

```bash
./FaceServerQt
```

服务将在 `http://localhost:3000` 启动

## 📡 API 接口

### 健康检查

```bash
GET /api/health
```

### 用户注册

```bash
POST /api/register
Content-Type: application/json

{
  "username": "user123",
  "face_image": "base64_encoded_image"
}
```

### 人脸识别登录

```bash
POST /api/login
Content-Type: application/json

{
  "face_image": "base64_encoded_image"
}
```

## 📁 项目结构

```
FaceServerQt/
├── src/                    # 源代码
│   ├── main.cpp           # 主程序入口
│   ├── FaceRecognizer.cpp # 人脸识别核心
│   └── DatabaseManager.cpp# 数据库管理
├── include/               # 头文件
├── models/                # dlib 模型文件
├── third_party/           # 第三方库
│   ├── httplib.h
│   └── jwt-cpp/
└── CMakeLists.txt
```

## 📖 详细文档

查看 [FaceServerQt 项目部署与开发指南.md](FaceServerQt%20项目部署与开发指南.md) 获取完整部署和开发说明。

## 🐛 常见问题

**Q: MySQL 驱动加载失败？**  
A: 需要为您的 Qt 版本编译对应的 MySQL 插件,参考详细文档第 12 节。

**Q: 模型文件在哪下载？**  
A: 从 [dlib.net](http://dlib.net/files/) 下载预训练模型。

## 📄 许可证

MIT License

## 🤝 贡献

欢迎提交 Issue 和 Pull Request!
