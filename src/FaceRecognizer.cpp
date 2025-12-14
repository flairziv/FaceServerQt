#include "FaceRecognizer.h"
#include <QDebug>
#include <QByteArray>
#include <dlib/image_processing.h>
#include <cmath>

FaceRecognizer::FaceRecognizer(QObject *parent) 
    : QObject(parent), m_modelsLoaded(false)
{
    m_faceDetector = dlib::get_frontal_face_detector();
}

FaceRecognizer::~FaceRecognizer()
{
}

bool FaceRecognizer::loadModels(const QString &shapePredictorPath, const QString &faceRecModelPath)
{
    try {
        qInfo() << "正在加载模型...";
        
        // 加载 68 点人脸关键点检测器
        dlib::deserialize(shapePredictorPath.toStdString()) >> m_shapePredictor;
        qInfo() << "✅ 关键点检测器加载成功";
        
        // 加载人脸识别网络
        dlib::deserialize(faceRecModelPath.toStdString()) >> m_faceRecNet;
        qInfo() << "✅ 人脸识别网络加载成功";
        
        m_modelsLoaded = true;
        return true;
    } catch (const std::exception &e) {
        qCritical() << "❌ 模型加载失败:" << e.what();
        m_modelsLoaded = false;
        return false;
    }
}

cv::Mat FaceRecognizer::base64ToMat(const QString &base64String)
{
    // 移除 data:image/jpeg;base64, 前缀
    QString base64Data = base64String;
    if (base64Data.contains(",")) {
        base64Data = base64Data.section(',', 1);
    }

    // 解码 base64
    QByteArray byteArray = QByteArray::fromBase64(base64Data.toUtf8());
    std::vector<uchar> buffer(byteArray.begin(), byteArray.end());

    // 解码为 OpenCV 图像
    cv::Mat image = cv::imdecode(buffer, cv::IMREAD_COLOR);
    
    if (image.empty()) {
        qWarning() << "图像解码失败";
    }
    
    return image;
}

QVector<float> FaceRecognizer::extractDescriptorFromBase64(const QString &base64Image)
{
    if (!m_modelsLoaded) {
        qWarning() << "模型未加载";
        return {};
    }

    // 解码图像
    cv::Mat cvImage = base64ToMat(base64Image);
    if (cvImage.empty()) {
        qWarning() << "无法解码图像";
        return {};
    }

    try {
        // 转换为 dlib 格式（BGR -> RGB）
        cv::Mat rgbImage;
        cv::cvtColor(cvImage, rgbImage, cv::COLOR_BGR2RGB);
        dlib::cv_image<dlib::rgb_pixel> dlibImage(rgbImage);

        // 检测人脸
        std::vector<dlib::rectangle> faces = m_faceDetector(dlibImage);
        
        if (faces.empty()) {
            qWarning() << "未检测到人脸";
            return {};
        }

        qInfo() << "检测到" << faces.size() << "个人脸，使用第一个";

        // 获取人脸关键点
        dlib::full_object_detection shape = m_shapePredictor(dlibImage, faces[0]);

        // 提取人脸区域（face chip）
        dlib::matrix<dlib::rgb_pixel> faceChip;
        dlib::extract_image_chip(dlibImage, dlib::get_face_chip_details(shape, 150, 0.25), faceChip);

        // 计算 128-d 特征向量
        dlib::matrix<float, 0, 1> faceDescriptor = m_faceRecNet(faceChip);

        // 转换为 QVector
        QVector<float> descriptor;
        descriptor.reserve(128);
        for (long i = 0; i < faceDescriptor.size(); ++i) {
            descriptor.append(faceDescriptor(i));
        }

        qInfo() << "✅ 成功提取 128-d 人脸特征";
        return descriptor;

    } catch (const std::exception &e) {
        qCritical() << "人脸识别失败:" << e.what();
        return {};
    }
}

double FaceRecognizer::computeDistance(const QVector<float> &desc1, const QVector<float> &desc2)
{
    if (desc1.size() != desc2.size() || desc1.isEmpty()) {
        return 999.0; // 返回一个很大的距离表示不匹配
    }

    double sum = 0.0;
    for (int i = 0; i < desc1.size(); ++i) {
        double diff = desc1[i] - desc2[i];
        sum += diff * diff;
    }

    return std::sqrt(sum);
}
