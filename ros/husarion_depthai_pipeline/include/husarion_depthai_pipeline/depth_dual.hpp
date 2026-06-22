#pragma once

#include <memory>
#include <string>
#include <vector>

#include "depthai-shared/common/CameraBoardSocket.hpp"
#include "depthai-shared/common/CameraFeatures.hpp"
#include "depthai_ros_driver/dai_nodes/base_node.hpp"

namespace dai {
class Pipeline;
class Device;
enum class CameraBoardSocket;
namespace node {
class StereoDepth;
}
}  // namespace dai

namespace rclcpp {
class Node;
class Parameter;
}  // namespace rclcpp

namespace depthai_ros_driver {
namespace param_handlers {
class StereoParamHandler;
}
namespace dai_nodes {
class SensorWrapper;
namespace sensor_helpers {
class ImagePublisher;
}
}  // namespace dai_nodes
}  // namespace depthai_ros_driver

namespace husarion_depthai {
namespace dai_nodes {

// !!! DRAFT — UNVERIFIED. No stereo OAK is connected to this robot (the unit is an
// OAK-1, RGB-only), so this depth path has NOT been run on hardware. It compiles
// against depthai_ros_driver 2.12.2 and mirrors the stock Stereo node's proven
// StereoDepth setup; validate end-to-end when an OAK-D-class camera is attached.
//
// Depth dual output off one StereoDepth node, simultaneously:
//   * depth 16UC1 sensor_msgs/Image  on  <topic>/image_raw            (autonomy — METRIC)
//   * disparity H.264 FFMPEGPacket    on  <topic>/image_raw/compressed (telepresence — VIEW)
//
// LOAD-BEARING: H.264 is lossy 8-bit; depth is 16-bit metric data. You NEVER H.264
// the metric depth (it corrupts distances). The encoded stream is the 8-bit
// DISPARITY as grayscale H.264 — a viewable visualization only. Depth DATA always
// travels as the raw 16UC1 topic. Same dual-publish shape as RGBDual.
class DepthDual : public depthai_ros_driver::dai_nodes::BaseNode {
   public:
    explicit DepthDual(const std::string& daiNodeName,
                       std::shared_ptr<rclcpp::Node> node,
                       std::shared_ptr<dai::Pipeline> pipeline,
                       std::shared_ptr<dai::Device> device,
                       dai::CameraBoardSocket leftSocket = dai::CameraBoardSocket::CAM_B,
                       dai::CameraBoardSocket rightSocket = dai::CameraBoardSocket::CAM_C);
    ~DepthDual();
    void updateParams(const std::vector<rclcpp::Parameter>& params) override;
    void setupQueues(std::shared_ptr<dai::Device> device) override;
    void link(dai::Node::Input in, int linkType = 0) override;
    void setNames() override;
    void setXinXout(std::shared_ptr<dai::Pipeline> pipeline) override;
    void closeQueues() override;
    std::vector<std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher>> getPublishers() override;

   private:
    std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher> depthPub, viewPub;
    std::shared_ptr<dai::node::StereoDepth> stereoCamNode;
    std::unique_ptr<depthai_ros_driver::dai_nodes::SensorWrapper> left, right;
    std::unique_ptr<depthai_ros_driver::param_handlers::StereoParamHandler> ph;
    std::string depthQName, viewQName;
    dai::CameraFeatures leftSensInfo, rightSensInfo;
};

}  // namespace dai_nodes
}  // namespace husarion_depthai
