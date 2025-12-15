cmake_minimum_required(VERSION 3.12)
project(DlibDemos)

set(CMAKE_CXX_STANDARD 14)

find_package(dlib REQUIRED)
find_package(OpenCV REQUIRED)

# # Demo 1
# add_executable(face_detection demo1_face_detection.cpp)
# target_link_libraries(face_detection dlib::dlib)

# # Demo 2
# add_executable(face_landmarks demo2_face_landmarks.cpp)
# target_link_libraries(face_landmarks dlib::dlib)

# # Demo 3
# add_executable(webcam_face demo3_webcam_face.cpp)
# target_link_libraries(webcam_face dlib::dlib ${OpenCV_LIBS})

# Demo 4
add_executable(face_recognition demo4_face_recognition.cpp)
target_link_libraries(face_recognition dlib::dlib)

# ...  其他 demo
