#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/gui_widgets.h>
#include <dlib/image_io.h>
#include <opencv2/opencv.hpp>
#include <dlib/opencv.h>

int main()
{
    try {
        // 打开摄像头
        cv::VideoCapture cap(0);
        if (!cap.isOpened()) {
            std::cerr << "无法打开视频" << std::endl;
            return 1;
        }

        // 创建人脸检测器
        dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();

        // 显示窗口
        dlib::image_window win;

        cv::Mat frame;
        while (cv::waitKey(1) != 27) {  // ESC 键退出
            // 读取摄像头帧
            cap >> frame;
            cv::resize(frame, frame, cv::Size(640, 480));
            if (frame.empty()) break;

            // 转换为 dlib 图像
            dlib::cv_image<dlib::rgb_pixel> dlib_img(frame);

            // 检测人脸
            std::vector<dlib::rectangle> faces = detector(dlib_img);

            // 在 OpenCV 图像上画框
            for (auto& face : faces) {
                cv::rectangle(frame, 
                    cv::Point(face.left(), face.top()), 
                    cv::Point(face.right(), face.bottom()), 
                    cv::Scalar(0, 255, 0), 2);
            }

            // 显示
            cv::imshow("Face Detection", frame);
        }

        return 0;
    }
    catch (std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
        return 1;
    }
}
