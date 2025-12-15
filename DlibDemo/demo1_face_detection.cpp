#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/gui_widgets.h>
#include <dlib/image_io.h>
#include <iostream>

int main(int argc, char** argv) 
{
    try {
        // 加载图像
        dlib::array2d<dlib::rgb_pixel> img;
        dlib::load_image(img, "/home/flairziv/QtBase/FaceServerQt/Demo/face.png");

        // 创建人脸检测器
        dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();

        // 检测人脸
        auto from = std::chrono::system_clock::now();
        std::vector<dlib::rectangle> faces = detector(img);
        auto next = std::chrono::system_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(next - from);
        std::cout << "延迟 " << duration.count() << " 毫秒" << std::endl;

        std::cout << "检测到 " << faces.size() << " 个人脸" << std::endl;

        // 在每个人脸周围画矩形框
        dlib::image_window win;
        win.set_image(img);
        win.add_overlay(faces, dlib::rgb_pixel(255, 0, 0));

        std::cout << "按任意键退出..." << std::endl;
        win.wait_until_closed();

        return 0;
    }
    catch (std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
        return 1;
    }
}
