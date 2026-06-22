#pragma once

#include "depthai_ros_driver/dai_nodes/base_node.hpp"

namespace dai {
class Pipeline;
class Device;
class DataInputQueue;
enum class CameraBoardSocket;
namespace node {
class ColorCamera;
class XLinkIn;
}  // namespace node
}  // namespace dai

namespace rclcpp {
class Node;
class Parameter;
}  // namespace rclcpp

namespace depthai_ros_driver {
namespace param_handlers {
class SensorParamHandler;
}
namespace dai_nodes {
namespace sensor_helpers {
struct ImageSensor;
class ImagePublisher;
}  // namespace sensor_helpers
}  // namespace dai_nodes
}  // namespace depthai_ros_driver

namespace husarion_depthai {
namespace dai_nodes {

// A single-sensor RGB node that publishes BOTH representations of one camera
// stream off a single ColorCamera.video (NV12) output, simultaneously:
//   * raw  sensor_msgs/Image          on  <topic>/image_raw            (autonomy)
//   * on-chip H.264 FFMPEGPacket      on  <topic>/image_raw/compressed (telepresence)
//
// The encoded path runs entirely on the OAK's hardware VideoEncoder — no host
// encode, no /dev/dri, no new snap interface. The raw path is a host XLinkOut tap.
// One dai output fanned out to two consumers (XLinkOut + VideoEncoder).
//
// This is the "dual-publish" BaseNode the SPEC calls for, written generic over
// the publish mechanics so the depth stream (Phase 3: depth 16UC1 raw + disparity
// grayscale H.264 view) can reuse setupDualOutput() against a StereoDepth source.
class RGBDual : public depthai_ros_driver::dai_nodes::BaseNode {
   public:
    explicit RGBDual(const std::string& daiNodeName,
                     std::shared_ptr<rclcpp::Node> node,
                     std::shared_ptr<dai::Pipeline> pipeline,
                     dai::CameraBoardSocket socket,
                     depthai_ros_driver::dai_nodes::sensor_helpers::ImageSensor sensor,
                     bool publish);
    ~RGBDual();
    void updateParams(const std::vector<rclcpp::Parameter>& params) override;
    void setupQueues(std::shared_ptr<dai::Device> device) override;
    void link(dai::Node::Input in, int linkType = 0) override;
    void setNames() override;
    void setXinXout(std::shared_ptr<dai::Pipeline> pipeline) override;
    void closeQueues() override;
    std::vector<std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher>> getPublishers() override;

   private:
    std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher> rawPub, encPub;
    std::shared_ptr<dai::node::ColorCamera> colorCamNode;
    std::unique_ptr<depthai_ros_driver::param_handlers::SensorParamHandler> ph;
    std::shared_ptr<dai::DataInputQueue> controlQ;
    std::shared_ptr<dai::node::XLinkIn> xinControl;
    std::string rawQName, encQName, controlQName;
};

}  // namespace dai_nodes
}  // namespace husarion_depthai
