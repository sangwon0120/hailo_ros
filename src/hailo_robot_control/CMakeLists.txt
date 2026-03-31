cmake_minimum_required(VERSION 3.8)
project(hailo_robot_control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Set C++ standard to match unitree_ros2 requirements
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(unitree_go REQUIRED)
find_package(unitree_api REQUIRED)
find_package(unitree_go2_vision_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)

# Find HailoRT
find_library(HAILORT_LIB hailort)
if (HAILORT_LIB)
    message(STATUS "Found HailoRT Library: ${HAILORT_LIB}")
else()
    message(WARNING "HailoRT Library not found! The hailo_detection_publisher might fail to link.")
    set(HAILORT_LIB "hailort") # default fallback
endif()
include_directories(include include/common)

set(DEPENDENCY_LIST
  rclcpp
  std_msgs
  unitree_go
  unitree_api
  unitree_go2_vision_msgs
)

add_compile_options(-Wno-unused-parameter -Wno-unused-variable)

# App executables
add_executable(practice src/practice.cpp src/common/ros2_sport_client.cpp)
ament_target_dependencies(practice ${DEPENDENCY_LIST})

add_executable(state_machine src/state_machine_test.cpp src/common/ros2_sport_client.cpp)
ament_target_dependencies(state_machine ${DEPENDENCY_LIST})

add_executable(go2_ai_controller src/go2_ai_controller.cpp src/common/ros2_sport_client.cpp)
ament_target_dependencies(go2_ai_controller ${DEPENDENCY_LIST})

add_executable(go2_vision_node src/go2_vision_node.cpp src/YOLOInferencer.cpp)
ament_target_dependencies(go2_vision_node ${DEPENDENCY_LIST})
target_link_libraries(go2_vision_node ${OpenCV_LIBRARIES})

add_executable(camera_test src/camera_test.cpp)
ament_target_dependencies(camera_test ${DEPENDENCY_LIST})
target_link_libraries(camera_test ${OpenCV_LIBRARIES} PkgConfig::FFMPEG)

add_executable(hailo_subscriber src/hailo_subscriber.cpp)
ament_target_dependencies(hailo_subscriber ${DEPENDENCY_LIST})

add_executable(hailo_detection_publisher src/hailo_detection.cpp)
ament_target_dependencies(hailo_detection_publisher ${DEPENDENCY_LIST})
target_link_libraries(hailo_detection_publisher ${OpenCV_LIBRARIES} ${HAILORT_LIB})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/go2_stream_viewer.cpp)
    add_executable(go2_stream_viewer src/go2_stream_viewer.cpp)
    ament_target_dependencies(go2_stream_viewer ${DEPENDENCY_LIST})
    target_link_libraries(go2_stream_viewer ${OpenCV_LIBRARIES})
    install(TARGETS go2_stream_viewer DESTINATION lib/${PROJECT_NAME})
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/feedback_based.cpp)
    add_executable(feedback_based src/feedback_based.cpp src/common/ros2_sport_client.cpp)
    ament_target_dependencies(feedback_based ${DEPENDENCY_LIST})
    install(TARGETS feedback_based DESTINATION lib/${PROJECT_NAME})
endif()

# Installation
install(TARGETS
  practice
  state_machine
  go2_ai_controller
  go2_vision_node
  camera_test
  hailo_subscriber
  hailo_detection_publisher
  DESTINATION lib/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/hailo_detection.py
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
