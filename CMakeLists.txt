cmake_minimum_required(VERSION 3.8)
project(bag_manager)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)

add_executable(bag_manager_node src/bag_manager_node.cpp)
ament_target_dependencies(bag_manager_node
  rclcpp
  std_msgs
  rosbag2_cpp
  rosbag2_storage
)

install(TARGETS
  bag_manager_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()