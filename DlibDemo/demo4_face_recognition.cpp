#include <dlib/dnn.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
#include <dlib/image_io.h>
#include <iostream>

// 定义 ResNet 网络结构
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
                            alevel0<alevel1<alevel2<alevel3<alevel4<
                            dlib::max_pool<3,3,2,2,dlib::relu<dlib::affine<dlib::con<32,7,7,2,2,
                            dlib::input_rgb_image_sized<150>
                            >>>>>>>>>>>>;

int main(int argc, char** argv) 
{
    try {
        // 加载模型
        dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();
        dlib::shape_predictor sp;
        dlib::deserialize("/home/flairziv/QtBase/FaceServerQt/models/shape_predictor_68_face_landmarks.dat") >> sp;
        
        anet_type net;
        dlib::deserialize("/home/flairziv/QtBase/FaceServerQt/models/dlib_face_recognition_resnet_model_v1.dat") >> net;

        // 加载两张图片
        dlib::array2d<dlib::rgb_pixel> img1, img2;
        dlib::load_image(img1, "/home/flairziv/QtBase/FaceServerQt/Demo/face.png");
        dlib::load_image(img2, "/home/flairziv/QtBase/FaceServerQt/Demo/face1.png");

        // 提取人脸特征
        auto extract_descriptor = [&](dlib::array2d<dlib::rgb_pixel>& img) {
            auto faces = detector(img);
            if (faces.empty()) {
                throw std::runtime_error("未检测到人脸");
            }

            auto shape = sp(img, faces[0]);
            dlib::matrix<dlib::rgb_pixel> face_chip;
            dlib::extract_image_chip(img, dlib::get_face_chip_details(shape, 150, 0.25), face_chip);

            return net(face_chip);
        };

        auto desc1 = extract_descriptor(img1);
        auto desc2 = extract_descriptor(img2);

        // 计算欧氏距离
        double distance = dlib::length(desc1 - desc2);

        std::cout << "特征向量距离: " << distance << std::endl;

        if (distance < 0.6) {
            std::cout << "✅ 是同一个人" << std::endl;
        } else {
            std::cout << "❌ 不是同一个人" << std::endl;
        }

        return 0;
    }
    catch (std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
        return 1;
    }
}
