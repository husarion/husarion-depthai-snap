#include "husarion_depthai_pipeline/rgb_dual.hpp"

#include "depthai-shared/properties/VideoEncoderProperties.hpp"
#include "depthai/device/Device.hpp"
#include "depthai/pipeline/Pipeline.hpp"
#include "depthai/pipeline/node/ColorCamera.hpp"
#include "depthai/pipeline/node/ImageManip.hpp"
#include "depthai/pipeline/node/XLinkIn.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/img_pub.hpp"
#include "depthai_ros_driver/dai_nodes/sensors/sensor_helpers.hpp"
#include "depthai_ros_driver/param_handlers/sensor_param_handler.hpp"
#include "depthai_ros_driver/utils.hpp"
#include "rclcpp/node.hpp"

namespace husarion_depthai {
namespace dai_nodes {

using depthai_ros_driver::dai_nodes::link_types::RGBLinkType;
namespace utils = depthai_ros_driver::utils;

RGBDual::RGBDual(const std::string& daiNodeName,
                 std::shared_ptr<rclcpp::Node> node,
                 std::shared_ptr<dai::Pipeline> pipeline,
                 dai::CameraBoardSocket socket = dai::CameraBoardSocket::CAM_A,
                 depthai_ros_driver::dai_nodes::sensor_helpers::ImageSensor sensor = {"IMX378", "1080P", {"12MP", "4K", "1080P"}, dai::CameraSensorType::COLOR},
                 bool publish = true)
    : BaseNode(daiNodeName, node, pipeline) {
    RCLCPP_DEBUG(getLogger(), "Creating node %s", daiNodeName.c_str());
    setNames();
    colorCamNode = pipeline->create<dai::node::ColorCamera>();
    ph = std::make_unique<depthai_ros_driver::param_handlers::SensorParamHandler>(node, daiNodeName, socket);
    // Configures the ColorCamera (socket, resolution, fps, ISP scale, video size,
    // color order, …) from the rgb.i_* params AND declares the i_low_bandwidth_*
    // knobs we read back for the on-chip encoder. Same param surface as stock RGB.
    ph->declareParams(colorCamNode, sensor, publish);
    setXinXout(pipeline);
    RCLCPP_DEBUG(getLogger(), "Node %s created", daiNodeName.c_str());
}
RGBDual::~RGBDual() = default;

int RGBDual::declInt(const std::string& name, int def) {
    auto full = getName() + "." + name;
    auto n = getROSNode();
    if(!n->has_parameter(full)) {
        n->declare_parameter<int>(full, def);
    }
    return static_cast<int>(n->get_parameter(full).as_int());
}

void RGBDual::setNames() {
    rawQName = getName() + "_raw";
    encQName = getName() + "_enc";
    controlQName = getName() + "_control";
}

void RGBDual::setXinXout(std::shared_ptr<dai::Pipeline> pipeline) {
    const int nativeW = ph->getParam<int>("i_width");
    const int nativeH = ph->getParam<int>("i_height");
    // 0 → use the camera's native video size. Otherwise the tap is ImageManip-resized.
    rawW = declInt("i_raw_width", 0) ? declInt("i_raw_width", 0) : nativeW;
    rawH = declInt("i_raw_height", 0) ? declInt("i_raw_height", 0) : nativeH;
    encW = declInt("i_enc_width", 0) ? declInt("i_enc_width", 0) : nativeW;
    encH = declInt("i_enc_height", 0) ? declInt("i_enc_height", 0) : nativeH;

    // Build a link function for a tap at (w,h): direct off ColorCamera.video when
    // it matches the native video size, else through an NV12 ImageManip resize.
    // depthai fans one output to many inputs, so raw + encoder run concurrently.
    auto makeTap = [&](int w, int h, std::shared_ptr<dai::node::ImageManip>& manip) -> std::function<void(dai::Node::Input)> {
        if(w == nativeW && h == nativeH) {
            return [this](dai::Node::Input in) { colorCamNode->video.link(in); };
        }
        manip = pipeline->create<dai::node::ImageManip>();
        manip->initialConfig.setResize(w, h);
        manip->initialConfig.setFrameType(dai::ImgFrame::Type::NV12);
        manip->setMaxOutputFrameSize(w * h * 3 / 2 + 1024);  // NV12 = 1.5 bytes/px
        colorCamNode->video.link(manip->inputImage);
        return [manip](dai::Node::Input in) { manip->out.link(in); };
    };

    if(ph->getParam<bool>("i_publish_topic")) {
        // Raw stream (autonomy): no encoder.
        utils::VideoEncoderConfig rawEnc;
        rawEnc.enabled = false;
        rawPub = setupOutput(pipeline, rawQName, makeTap(rawW, rawH, rawManip), /*isSynced=*/false, rawEnc);

        // Encoded stream (telepresence): the OAK hardware VideoEncoder. Profile /
        // bitrate / keyframe cadence come from the same i_low_bandwidth_* knobs the
        // stock streaming-h264 presets already use.
        utils::VideoEncoderConfig encConfig;
        encConfig.enabled = true;
        encConfig.profile = static_cast<dai::VideoEncoderProperties::Profile>(ph->getParam<int>("i_low_bandwidth_profile"));
        encConfig.bitrate = ph->getParam<int>("i_low_bandwidth_bitrate");
        encConfig.frameFreq = ph->getParam<int>("i_low_bandwidth_frame_freq");
        encConfig.quality = ph->getParam<int>("i_low_bandwidth_quality");
        encPub = setupOutput(pipeline, encQName, makeTap(encW, encH, encManip), /*isSynced=*/false, encConfig);
    }

    xinControl = pipeline->create<dai::node::XLinkIn>();
    xinControl->setStreamName(controlQName);
    xinControl->out.link(colorCamNode->inputControl);
}

void RGBDual::setupQueues(std::shared_ptr<dai::Device> device) {
    if(!ph->getParam<bool>("i_publish_topic")) {
        controlQ = device->getInputQueue(controlQName);
        return;
    }
    auto tfPrefix = getOpticalTFPrefix(getSocketName(static_cast<dai::CameraBoardSocket>(ph->getParam<int>("i_board_socket_id"))));
    auto colorEncoding = ph->getParam<std::string>("i_color_order") == "BGR" ? dai::RawImgFrame::Type::BGR888i : dai::RawImgFrame::Type::RGB888i;

    utils::ImgPublisherConfig basePub;
    basePub.daiNodeName = getName();
    basePub.topicName = "~/" + getName();
    basePub.lazyPub = ph->getParam<bool>("i_enable_lazy_publisher");
    basePub.socket = static_cast<dai::CameraBoardSocket>(ph->getParam<int>("i_board_socket_id"));
    basePub.calibrationFile = ph->getParam<std::string>("i_calibration_file");
    basePub.rectified = false;
    basePub.maxQSize = ph->getParam<int>("i_max_q_size");
    basePub.flipImage = ph->getParam<bool>("i_flip_published_image");

    utils::ImgConverterConfig baseConv;
    baseConv.tfPrefix = tfPrefix;
    baseConv.getBaseDeviceTimestamp = ph->getParam<bool>("i_get_base_device_timestamp");
    baseConv.updateROSBaseTimeOnRosMsg = ph->getParam<bool>("i_update_ros_base_time_on_ros_msg");
    baseConv.encoding = colorEncoding;
    baseConv.addExposureOffset = ph->getParam<bool>("i_add_exposure_offset");
    baseConv.expOffset = static_cast<dai::CameraExposureOffset>(ph->getParam<int>("i_exposure_offset"));
    baseConv.reverseSocketOrder = ph->getParam<bool>("i_reverse_stereo_socket_order");

    // Raw: plain image_transport camera publisher → <topic>/image_raw + <topic>/camera_info.
    // (Suppress its republisher plugins via oak.rgb.image_raw.enable_pub_plugins:['image_transport/raw']
    //  in the preset, so the on-chip H.264 is the sole publisher on .../compressed.)
    {
        utils::ImgConverterConfig conv = baseConv;
        conv.lowBandwidth = false;
        utils::ImgPublisherConfig pub = basePub;
        pub.publishCompressed = false;  // raw sensor_msgs/Image
        pub.topicSuffix = "/image_raw";
        pub.width = rawW;
        pub.height = rawH;
        rawPub->setup(device, conv, pub);
    }
    // Encoded: on-chip H.264 → <topic>/image_raw/compressed (FFMPEGPacket). The
    // FFMPEGPacket codec token is set to "h264" (the bitstream IS on-chip h264, not
    // libx264); the cockpit keys the "sensor" provenance badge off the /compressed
    // suffix, and accepts h264/libx264/h264_* alike. camera_info parked under
    // image_raw/ to avoid clobbering the raw stream's canonical <topic>/camera_info.
    {
        utils::ImgConverterConfig conv = baseConv;
        conv.lowBandwidth = true;
        conv.ffmpegEncoder = "h264";
        utils::ImgPublisherConfig pub = basePub;
        pub.publishCompressed = true;  // FFMPEGPacket (H.264)
        pub.compressedTopicSuffix = "/image_raw/compressed";
        pub.infoSuffix = "/image_raw";
        pub.width = encW;
        pub.height = encH;
        encPub->setup(device, conv, pub);
    }
    controlQ = device->getInputQueue(controlQName);
}

void RGBDual::closeQueues() {
    if(ph->getParam<bool>("i_publish_topic")) {
        rawPub->closeQueue();
        encPub->closeQueue();
    }
    controlQ->close();
}

void RGBDual::link(dai::Node::Input in, int linkType) {
    if(linkType == static_cast<int>(RGBLinkType::video)) {
        colorCamNode->video.link(in);
    } else if(linkType == static_cast<int>(RGBLinkType::isp)) {
        colorCamNode->isp.link(in);
    } else if(linkType == static_cast<int>(RGBLinkType::preview)) {
        colorCamNode->preview.link(in);
    } else {
        throw std::runtime_error("Link type not supported");
    }
}

std::vector<std::shared_ptr<depthai_ros_driver::dai_nodes::sensor_helpers::ImagePublisher>> RGBDual::getPublishers() {
    // Only relevant for the driver's optional Sync node (i_enable_sync). Dual output
    // is inherently unsynced (raw + encoded are independent taps), so expose none.
    return {};
}

void RGBDual::updateParams(const std::vector<rclcpp::Parameter>& params) {
    auto ctrl = ph->setRuntimeParams(params);
    controlQ->send(ctrl);
}

}  // namespace dai_nodes
}  // namespace husarion_depthai
