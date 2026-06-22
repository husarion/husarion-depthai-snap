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

// VERIFIED on an OAK-D-LITE (2026-06-22). Mirrors the stock Stereo node's
// StereoDepth setup; depth dual output off one StereoDepth node, simultaneously:
//   * depth 16UC1 sensor_msgs/Image  on  <topic>/image_raw            (autonomy — METRIC)
//   * disparity H.264 FFMPEGPacket    on  <topic>/image_raw/compressed (telepresence — VIEW)
//
// LOAD-BEARING: H.264 is lossy 8-bit; depth is 16-bit metric data. You NEVER H.264
// the metric depth (it corrupts distances). The encoded stream is the 8-bit
// DISPARITY as grayscale H.264 — a viewable visualization only. Depth DATA always
// travels as the raw 16UC1 topic. Same dual-publish shape as RGBDual.
//
// CONSTRAINT: subpixel must be OFF. Subpixel makes the disparity output 16-bit,
// which the OAK VideoEncoder rejects ("Arrived frame type (14) is not NV12 or
// YUV400p (8-bit Gray)"). The metric depth raw stays 16-bit regardless. The ctor
// now FORCES stereoCamNode->setSubpixel(false) (the param default is true and is
// only auto-disabled under stereo.i_low_bandwidth, which this plugin never sets),
// so a preset that forgets stereo.i_subpixel=false can no longer crash the encoder;
// keep it in the preset for clarity. A future subpixel-depth + 8-bit-preview combo
// needs an ImageManip 16->8-bit on the encoder tap.
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
