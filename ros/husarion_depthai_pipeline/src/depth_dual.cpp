#include "husarion_depthai_pipeline/depth_dual.hpp"

#include "depthai-shared/properties/VideoEncoderProperties.hpp"
#include "depthai/device/Device.hpp"
#include "depthai/pipeline/Pipeline.hpp"
#include "depthai/pipeline/node/StereoDepth.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/img_pub.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_helpers.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_wrapper.hpp"
#include "depthai_ros_driver/param_handlers/stereo_param_handler.hpp"
#include "depthai_ros_driver/utils.hpp"
#include "rclcpp/node.hpp"

// VERIFIED on an OAK-D-LITE (2026-06-22). Requires stereo.i_subpixel=false (8-bit
// disparity for the encoder) — see depth_dual.hpp.

namespace husarion_depthai {
namespace dai_nodes {

namespace utils = depthai_ros_driver::utils;
using depthai_ros_driver::dai_nodes::SensorWrapper;

DepthDual::DepthDual(const std::string& daiNodeName,
                     std::shared_ptr<rclcpp::Node> node,
                     std::shared_ptr<dai::Pipeline> pipeline,
                     std::shared_ptr<dai::Device> device,
                     dai::CameraBoardSocket leftSocket,
                     dai::CameraBoardSocket rightSocket)
    : BaseNode(daiNodeName, node, pipeline) {
    setNames();
    ph = std::make_unique<depthai_ros_driver::param_handlers::StereoParamHandler>(node, daiNodeName);
    auto alignSocket = dai::CameraBoardSocket::CAM_A;
    if(device->getDeviceName() == "OAK-D-SR" || device->getDeviceName() == "OAK-D-SR-POE") {
        alignSocket = dai::CameraBoardSocket::CAM_C;
    }
    ph->updateSocketsFromParams(leftSocket, rightSocket, alignSocket);
    for(auto f : device->getConnectedCameraFeatures()) {
        if(f.socket == leftSocket) {
            leftSensInfo = f;
            leftSensInfo.name = getSocketName(leftSocket);
        } else if(f.socket == rightSocket) {
            rightSensInfo = f;
            rightSensInfo.name = getSocketName(rightSocket);
        }
    }
    left = std::make_unique<SensorWrapper>(getSocketName(leftSensInfo.socket), node, pipeline, device, leftSensInfo.socket, false);
    right = std::make_unique<SensorWrapper>(getSocketName(rightSensInfo.socket), node, pipeline, device, rightSensInfo.socket, false);
    stereoCamNode = pipeline->create<dai::node::StereoDepth>();
    ph->declareParams(stereoCamNode);
    // HARD CONSTRAINT (not just a preset convention): the disparity VIEW tap feeds the
    // on-chip VideoEncoder, which only accepts 8-bit (NV12 / YUV400p). Subpixel makes
    // the disparity output 16-bit and the encoder rejects it at runtime ("Arrived frame
    // type (14) is not NV12 or YUV400p"). StereoParamHandler only auto-disables subpixel
    // under stereo.i_low_bandwidth (which this plugin never sets — it drives its own
    // encoder), so its i_subpixel default of `true` would crash a preset that forgot
    // stereo.i_subpixel=false. Force it off here so the encoder tap is always 8-bit.
    // (The metric DEPTH raw stays 16-bit regardless — subpixel only refines its precision.)
    stereoCamNode->setSubpixel(false);
    setXinXout(pipeline);
    left->link(stereoCamNode->left);
    right->link(stereoCamNode->right);
}
DepthDual::~DepthDual() = default;

void DepthDual::setNames() {
    depthQName = getName() + "_depth";
    viewQName = getName() + "_view";
}

void DepthDual::setXinXout(std::shared_ptr<dai::Pipeline> pipeline) {
    if(!ph->getParam<bool>("i_publish_topic")) {
        return;
    }
    // Depth DATA (16UC1 metric): tap StereoDepth.depth, no encoder.
    utils::VideoEncoderConfig rawEnc;
    rawEnc.enabled = false;
    depthPub = setupOutput(
        pipeline, depthQName, [&](dai::Node::Input in) { stereoCamNode->depth.link(in); }, /*isSynced=*/false, rawEnc);

    // Depth VIEW (lossy grayscale H.264): tap StereoDepth.disparity → on-chip encoder.
    utils::VideoEncoderConfig encConfig;
    encConfig.enabled = true;
    encConfig.profile = static_cast<dai::VideoEncoderProperties::Profile>(ph->getParam<int>("i_low_bandwidth_profile"));
    encConfig.bitrate = ph->getParam<int>("i_low_bandwidth_bitrate");
    encConfig.frameFreq = ph->getParam<int>("i_low_bandwidth_frame_freq");
    encConfig.quality = ph->getParam<int>("i_low_bandwidth_quality");
    viewPub = setupOutput(
        pipeline, viewQName, [&](dai::Node::Input in) { stereoCamNode->disparity.link(in); }, /*isSynced=*/false, encConfig);
}

void DepthDual::setupQueues(std::shared_ptr<dai::Device> device) {
    left->setupQueues(device);
    right->setupQueues(device);
    if(!ph->getParam<bool>("i_publish_topic")) {
        return;
    }
    std::string tfPrefix = ph->getParam<bool>("i_align_depth") ? getOpticalTFPrefix(ph->getParam<std::string>("i_socket_name"))
                                                               : getOpticalTFPrefix(getSocketName(rightSensInfo.socket).c_str());

    utils::ImgPublisherConfig basePub;
    basePub.daiNodeName = getName();
    basePub.topicName = "~/" + getName();
    basePub.width = ph->getParam<int>("i_width");
    basePub.height = ph->getParam<int>("i_height");
    basePub.socket = static_cast<dai::CameraBoardSocket>(ph->getParam<int>("i_board_socket_id"));
    basePub.calibrationFile = ph->getParam<std::string>("i_calibration_file");
    basePub.leftSocket = leftSensInfo.socket;
    basePub.rightSocket = rightSensInfo.socket;
    basePub.lazyPub = ph->getParam<bool>("i_enable_lazy_publisher");
    basePub.maxQSize = ph->getParam<int>("i_max_q_size");
    basePub.rectified = true;

    utils::ImgConverterConfig baseConv;
    baseConv.tfPrefix = tfPrefix;
    baseConv.getBaseDeviceTimestamp = ph->getParam<bool>("i_get_base_device_timestamp");
    baseConv.updateROSBaseTimeOnRosMsg = ph->getParam<bool>("i_update_ros_base_time_on_ros_msg");
    baseConv.encoding = dai::RawImgFrame::Type::RAW8;
    baseConv.addExposureOffset = ph->getParam<bool>("i_add_exposure_offset");
    baseConv.expOffset = static_cast<dai::CameraExposureOffset>(ph->getParam<int>("i_exposure_offset"));
    baseConv.reverseSocketOrder = ph->getParam<bool>("i_reverse_stereo_socket_order");
    baseConv.isStereo = true;

    // Raw metric depth (16UC1) — outputDisparity=false so the converter yields depth.
    {
        utils::ImgConverterConfig conv = baseConv;
        conv.lowBandwidth = false;
        conv.outputDisparity = false;
        utils::ImgPublisherConfig pub = basePub;
        pub.publishCompressed = false;
        pub.topicSuffix = "/image_raw";
        depthPub->setup(device, conv, pub);
    }
    // Disparity grayscale H.264 view — outputDisparity=true + lowBandwidth (encoded).
    {
        utils::ImgConverterConfig conv = baseConv;
        conv.lowBandwidth = true;
        conv.outputDisparity = true;
        conv.ffmpegEncoder = "h264";
        utils::ImgPublisherConfig pub = basePub;
        pub.publishCompressed = true;
        pub.compressedTopicSuffix = "/image_raw/compressed";
        pub.infoSuffix = "/image_raw";
        viewPub->setup(device, conv, pub);
    }
}

void DepthDual::closeQueues() {
    left->closeQueues();
    right->closeQueues();
    if(ph->getParam<bool>("i_publish_topic")) {
        depthPub->closeQueue();
        viewPub->closeQueue();
    }
}

void DepthDual::link(dai::Node::Input in, int /*linkType*/) {
    stereoCamNode->depth.link(in);
}

std::vector<std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher>> DepthDual::getPublishers() {
    return {};
}

void DepthDual::updateParams(const std::vector<rclcpp::Parameter>& params) {
    ph->setRuntimeParams(params);
}

}  // namespace dai_nodes
}  // namespace husarion_depthai
