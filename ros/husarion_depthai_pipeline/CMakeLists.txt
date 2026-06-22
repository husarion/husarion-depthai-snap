cmake_minimum_required(VERSION 3.10)
project(husarion_depthai_pipeline)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(depthai CONFIG REQUIRED)
find_package(depthai_ros_driver REQUIRED)
find_package(depthai_bridge REQUIRED)
find_package(ffmpeg_image_transport_msgs REQUIRED)
find_package(image_transport REQUIRED)
find_package(camera_info_manager REQUIRED)
find_package(sensor_msgs REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/rgb_dual.cpp
  src/dual_pipeline.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  pluginlib
  depthai_ros_driver
  depthai_bridge
  ffmpeg_image_transport_msgs
  image_transport
  camera_info_manager
  sensor_msgs
)
target_link_libraries(${PROJECT_NAME} depthai::core)

# Register the plugin with depthai_ros_driver's pluginlib class loader.
pluginlib_export_plugin_description_file(depthai_ros_driver plugins.xml)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

ament_export_libraries(${PROJECT_NAME})
ament_package()
