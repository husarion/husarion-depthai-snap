#pragma once

#include <memory>
#include <string>
#include <vector>

#include "depthai_ros_driver/pipeline/base_pipeline.hpp"

namespace husarion_depthai {
namespace pipeline_gen {

// Custom depthai_ros_driver pipeline plugin: publishes raw + on-chip-H.264 for
// each enabled stream off one OAK. Selected via `camera.i_pipeline_type`.
//
// RGBDual (this class) — RGB stream only; the connected unit is an OAK-1.
// A future RGBDDual will add the depth stream (depth 16UC1 raw + disparity H.264
// view) by appending a depth dual-publish node alongside the RGB one — same plugin
// pattern, no fork.
class RGBDual : public depthai_ros_driver::pipeline_gen::BasePipeline {
   public:
    std::vector<std::unique_ptr<depthai_ros_driver::dai_nodes::BaseNode>> createPipeline(std::shared_ptr<rclcpp::Node> node,
                                                                                         std::shared_ptr<dai::Device> device,
                                                                                         std::shared_ptr<dai::Pipeline> pipeline,
                                                                                         const std::string& nnType) override;
};

}  // namespace pipeline_gen
}  // namespace husarion_depthai
