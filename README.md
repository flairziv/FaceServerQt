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
    libqt5sql5-mysql libopencv-dev libmysqlclient-dev libsodium-dev
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

启动前需要设置以下环境变量:

| 变量 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `JWT_SECRET` | ✅ | 无 | JWT 签名密钥,至少 32 字节 |
| `DB_USER` | ✅ | 无 | MySQL 用户名 |
| `DB_PASS` | ✅ | 无 | MySQL 密码 |
| `DB_HOST` | ❌ | `127.0.0.1` | MySQL 主机 |
| `DB_PORT` | ❌ | `3306` | MySQL 端口 |
| `DB_NAME` | ❌ | `face_recognition_db` | 数据库名 |
| `ALLOWED_ORIGINS` | ❌ | `*` | CORS 白名单,逗号分隔的 origin,如 `https://app.example.com,https://x.example.com` |

```bash
export JWT_SECRET="$(openssl rand -base64 48)"
export DB_USER=faceuser
export DB_PASS=FacePass2025
./FaceServerQt
```

服务将在 `http://localhost:3000` 启动。**重启时如果重新生成 `JWT_SECRET`,所有已签发的 token 会失效**。

> 不想污染 shell?在 `build/` 下放一个 `.env` 用 `run.sh` 包装启动,详见[部署指南](FaceServerQt%20项目部署与开发指南.md) §10.1。

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

### 访问需要认证的 API（需要 token）

```bash
// 获取用户信息
const getUserInfo = async () => {
  const token = localStorage.getItem('token'); // 从存储中获取token
  
  const response = await fetch('http://localhost:3000/api/user/info', {
    method: 'GET',
    headers: {
      'Authorization': `Bearer ${token}` // ✅ 必须添加 Bearer token
    }
  });
  
  const data = await response.json();
  console.log(data);
};

// 修改密码
const changePassword = async () => {
  const token = localStorage.getItem('token');
  
  const response = await fetch('http://localhost:3000/api/user/password', {
    method: 'PUT',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${token}` // ✅ 必须添加
    },
    body: JSON.stringify({
      oldPassword: 'old123',
      newPassword: 'new456'
    })
  });
  
  const data = await response.json();
  console.log(data);
};

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

## 📄 许可证 / 使用条款

本项目仅供个人学习、研究与教学用途。

- ❌ **禁止商业使用**:未经作者书面授权,不得将本项目(或其衍生代码)用于任何直接或间接的商业用途,包括但不限于售卖、企业内部生产环境部署、SaaS 服务、付费课程素材等。
- ⚠️ **免责声明**:本项目按"现状"提供,不附带任何明示或暗示的担保(包括但不限于适销性、特定用途适用性、不侵权等)。作者**不对任何因使用、误用或无法使用本项目而导致的直接、间接、附带、特殊、惩罚性或后果性损失承担责任**(包括但不限于数据丢失、业务中断、安全事件、识别错误造成的损失等)。
- ✅ 在遵守上述限制的前提下,允许复制、修改、用于个人项目以及在非商业场景下分发,但必须保留本说明。

如需商业授权,请通过 issue 或邮件联系作者协商。

## 🤝 贡献

欢迎提交 Issue 和 Pull Request!
