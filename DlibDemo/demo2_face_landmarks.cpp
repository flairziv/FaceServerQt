#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing/render_face_detections.h>
#include <dlib/image_processing.h>
#include <dlib/gui_widgets.h>
#include <dlib/image_io.h>

int main(int argc, char** argv) 
{
    try {
        // 加载模型
        dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();
        dlib::shape_predictor sp;
        dlib::deserialize("/home/flairziv/QtBase/FaceServerQt/models/shape_predictor_68_face_landmarks.dat") >> sp;

        // 加载图像
        dlib::array2d<dlib::rgb_pixel> img;
        dlib::load_image(img, "/home/flairziv/QtBase/FaceServerQt/Demo/face.png");

        // 检测人脸
        auto from = std::chrono::system_clock::now();
        std::vector<dlib::rectangle> faces = detector(img);
        auto next = std::chrono::system_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(next - from);
        std::cout << "延迟 " << duration.count() << " 毫秒" << std::endl;

        // 显示窗口
        dlib::image_window win;
        win.set_image(img);

        // 对每个人脸检测关键点
        std::vector<dlib::full_object_detection> shapes;
        for (auto& face : faces) {
            dlib::full_object_detection shape = sp(img, face);
            shapes.push_back(shape);

            std::cout << "人脸关键点数量: " << shape.num_parts() << std::endl;

            // 打印前 5 个点的坐标
            for (int i = 0; i < 5; ++i) {
                dlib::point p = shape.part(i);
                std::cout << "点 " << i << ": (" << p.x() << ", " << p.y() << ")" << std::endl;
            }
        }

        // 在图像上绘制关键点
        win.add_overlay(dlib::render_face_detections(shapes));
        std::cout << "按任意键退出..." << std::endl;
        win.wait_until_closed();

        return 0;
    }
    catch (std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
        return 1;
    }
}
