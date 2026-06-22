#pragma once

#include <memory>
#include <string>
#include <vector>

#include "depthai_ros_driver/pipeline/base_pipeline.hpp"

namespace husarion_depthai {
namespace pipeline_gen {

// Custom depthai_ros_driver pipeline plugins: publish raw + on-chip-H.264 for
// each enabled stream off one OAK. Selected via `camera.i_pipeline_type`.

// RGB stream only (raw image_raw + on-chip H.264 image_raw/compressed).
// The connected unit is an OAK-1 — this is the verified path.
class RGBDual : public depthai_ros_driver::pipeline_gen::BasePipeline {
   public:
    std::vector<std::unique_ptr<depthai_ros_driver::dai_nodes::BaseNode>> createPipeline(std::shared_ptr<rclcpp::Node> node,
                                                                                         std::shared_ptr<dai::Device> device,
                                                                                         std::shared_ptr<dai::Pipeline> pipeline,
                                                                                         const std::string& nnType) override;
};

// RGB + depth dual output (RGBDual node + a depth dual node: depth 16UC1 raw +
// disparity grayscale-H.264 view). Requires an OAK-D-class stereo camera; VERIFIED
// on an OAK-D-LITE. The depth preset MUST set stereo.i_subpixel=false (8-bit
// disparity for the encoder — see depth_dual.hpp).
class RGBDDual : public depthai_ros_driver::pipeline_gen::BasePipeline {
   public:
    std::vector<std::unique_ptr<depthai_ros_driver::dai_nodes::BaseNode>> createPipeline(std::shared_ptr<rclcpp::Node> node,
                                                                                         std::shared_ptr<dai::Device> device,
                                                                                         std::shared_ptr<dai::Pipeline> pipeline,
                                                                                         const std::string& nnType) override;
};

}  // namespace pipeline_gen
}  // namespace husarion_depthai
