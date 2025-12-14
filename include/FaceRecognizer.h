#ifndef FACERECOGNIZER_H
#define FACERECOGNIZER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <opencv2/opencv.hpp>
#include <dlib/opencv.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
#include <dlib/dnn.h>

// dlib 人脸识别网络模板定义
template <template <int,template<typename>class,int,typename> class block, int N, 
          template<typename>class BN, typename SUBNET>
using residual = dlib::add_prev1<block<N,BN,1,dlib::tag1<SUBNET>>>;

template <template <int,template<typename>class,int,typename> class block, int N, 
          template<typename>class BN, typename SUBNET>
using residual_down = dlib::add_prev2<dlib::avg_pool<2,2,2,2,dlib::skip1<dlib::tag2<block<N,BN,2,dlib::tag1<SUBNET>>>>>>;

template <int N, template <typename> class BN, int stride, typename SUBNET>
using block = BN<dlib::con<N,3,3,1,1,dlib::relu<BN<dlib::con<N,3,3,stride,stride,SUBNET>>>>>;

template <int N, typename SUBNET> using ares = dlib::relu<residual<block,N,dlib::affine,SUBNET>>;
template <int N, typename SUBNET> using ares_down = dlib::relu<residual_down<block,N,dlib::affine,SUBNET>>;

template <typename SUBNET> using alevel0 = ares_down<256,SUBNET>;
template <typename SUBNET> using alevel1 = ares<256,ares<256,ares_down<256,SUBNET>>>;
template <typename SUBNET> using alevel2 = ares<128,ares<128,ares_down<128,SUBNET>>>;
template <typename SUBNET> using alevel3 = ares<64,ares<64,ares<64,ares_down<64,SUBNET>>>>;
template <typename SUBNET> using alevel4 = ares<32,ares<32,ares<32,SUBNET>>>;

using anet_type = dlib::loss_metric<dlib::fc_no_bias<128,dlib::avg_pool_everything<
                            alevel0<
                            alevel1<
                            alevel2<
                            alevel3<
                            alevel4<
                            dlib::max_pool<3,3,2,2,dlib::relu<dlib::affine<dlib::con<32,7,7,2,2,
                            dlib::input_rgb_image_sized<150>
                            >>>>>>>>>>>>;

class FaceRecognizer : public QObject
{
    Q_OBJECT

public:
    explicit FaceRecognizer(QObject *parent = nullptr);
    ~FaceRecognizer();

    // 加载 dlib 模型
    bool loadModels(const QString &shapePredictorPath, const QString &faceRecModelPath);
    
    // 从 base64 图像提取 128-d 人脸特征向量
    QVector<float> extractDescriptorFromBase64(const QString &base64Image);
    
    // 计算两个特征向量的欧氏距离
    static double computeDistance(const QVector<float> &desc1, const QVector<float> &desc2);

private:
    bool m_modelsLoaded;
    dlib::frontal_face_detector m_faceDetector;
    dlib::shape_predictor m_shapePredictor;
    anet_type m_faceRecNet;
    
    // 辅助：base64 转 cv::Mat
    cv::Mat base64ToMat(const QString &base64String);
};

#endif // FACERECOGNIZER_H
