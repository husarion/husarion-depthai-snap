#include "husarion_depthai_pipeline/dual_pipeline.hpp"

#include <algorithm>

#include "depthai/device/Device.hpp"
#include "depthai/pipeline/Pipeline.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_helpers.hpp"
#include "husarion_depthai_pipeline/rgb_dual.hpp"
#include "rclcpp/node.hpp"

namespace husarion_depthai {
namespace pipeline_gen {

namespace sensor_helpers = depthai_ros_driver::dai_nodes::sensor_helpers;

std::vector<std::unique_ptr<depthai_ros_driver::dai_nodes::BaseNode>> RGBDual::createPipeline(
    std::shared_ptr<rclcpp::Node> node,
    std::shared_ptr<dai::Device> device,
    std::shared_ptr<dai::Pipeline> pipeline,
    const std::string& /*nnType*/) {
    std::vector<std::unique_ptr<depthai_ros_driver::dai_nodes::BaseNode>> daiNodes;

    // Detect the RGB sensor (CAM_A) exactly like the stock SensorWrapper does, so
    // the correct allowed-resolution list is used (e.g. IMX378 → {12MP,4K,1080P}).
    const auto socket = dai::CameraBoardSocket::CAM_A;
    auto sensorName = device->getCameraSensorNames().at(socket);
    for(auto& c : sensorName) c = toupper(c);
    auto it = std::find_if(sensor_helpers::availableSensors.begin(),
                           sensor_helpers::availableSensors.end(),
                           [&sensorName](const sensor_helpers::ImageSensor& s) { return s.name == sensorName; });
    if(it == sensor_helpers::availableSensors.end()) {
        RCLCPP_ERROR(node->get_logger(), "RGBDual: sensor %s on CAM_A not supported!", sensorName.c_str());
        throw std::runtime_error("RGBDual: sensor not supported");
    }
    RCLCPP_INFO(node->get_logger(), "RGBDual pipeline: CAM_A sensor %s — raw + on-chip H.264 dual output", sensorName.c_str());

    auto rgb = std::make_unique<dai_nodes::RGBDual>(
        sensor_helpers::getNodeName(node, sensor_helpers::NodeNameEnum::RGB), node, pipeline, socket, *it, /*publish=*/true);
    daiNodes.push_back(std::move(rgb));
    return daiNodes;
}

}  // namespace pipeline_gen
}  // namespace husarion_depthai

#include <pluginlib/class_list_macros.hpp>

PLUGINLIB_EXPORT_CLASS(husarion_depthai::pipeline_gen::RGBDual, depthai_ros_driver::pipeline_gen::BasePipeline)
