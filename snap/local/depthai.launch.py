import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    OpaqueFunction,
)
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import ComposableNodeContainer, LoadComposableNodes
from launch_ros.descriptions import ComposableNode, ParameterFile


def launch_setup(context, *args, **kwargs):
    log_level = "info"
    if context.environment.get("DEPTHAI_DEBUG") == "1":
        log_level = "debug"

    params_file = ParameterFile(LaunchConfiguration("params_file"), allow_substs=True)
    ffmpeg_params_file = ParameterFile(
        LaunchConfiguration("ffmpeg_params_file"), allow_substs=True
    )

    name = LaunchConfiguration("name").perform(context)
    namespace = LaunchConfiguration("namespace").perform(context)
    enable_pointcloud = LaunchConfiguration("enable_pointcloud").perform(context) == "true"

    rgb_topic_name = name + "/rgb/image_raw"
    if LaunchConfiguration("rectify_rgb").perform(context) == "true":
        rgb_topic_name = name + "/rgb/image_rect"

    # When pointcloud is on, force RGB↔stereo timestamp sync so PointCloudXyzrgbNode
    # gets matched frames. Mirrors upstream camera.launch.py pointcloud.enable handling.
    pcl_param_overrides = {}
    if enable_pointcloud:
        pcl_param_overrides = {
            "pipeline_gen": {"i_enable_sync": True},
            "rgb": {"i_synced": True},
            "stereo": {"i_synced": True},
        }

    container_name = f"/{namespace}/{name}_container" if namespace else f"/{name}_container"

    actions = []

    actions.append(
        ComposableNodeContainer(
            name=name + "_container",
            namespace=namespace,
            package="rclcpp_components",
            executable="component_container",
            composable_node_descriptions=[
                ComposableNode(
                    package="depthai_ros_driver",
                    plugin="depthai_ros_driver::Camera",
                    name=name,
                    namespace=namespace,
                    parameters=[params_file, ffmpeg_params_file, pcl_param_overrides],
                )
            ],
            arguments=["--ros-args", "--log-level", log_level],
            output="both",
        )
    )

    actions.append(
        LoadComposableNodes(
            condition=IfCondition(LaunchConfiguration("rectify_rgb")),
            target_container=container_name,
            composable_node_descriptions=[
                ComposableNode(
                    package="image_proc",
                    plugin="image_proc::RectifyNode",
                    name=name + "_rectify_color_node",
                    namespace=namespace,
                    parameters=[ffmpeg_params_file],
                    remappings=[
                        ("image", name + "/rgb/image_raw"),
                        ("camera_info", name + "/rgb/camera_info"),
                        ("image_rect", name + "/rgb/image_rect"),
                        ("image_rect/compressed", name + "/rgb/image_rect/compressed"),
                        (
                            "image_rect/compressedDepth",
                            name + "/rgb/image_rect/compressedDepth",
                        ),
                        ("image_rect/theora", name + "/rgb/image_rect/theora"),
                        ("image_rect/ffmpeg", name + "/rgb/image_rect/ffmpeg"),
                        ("image_rect/zstd", name + "/rgb/image_rect/zstd"),
                    ],
                )
            ],
        )
    )

    actions.append(
        LoadComposableNodes(
            condition=IfCondition(LaunchConfiguration("enable_pointcloud")),
            target_container=container_name,
            composable_node_descriptions=[
                ComposableNode(
                    package="depth_image_proc",
                    plugin="depth_image_proc::PointCloudXyzrgbNode",
                    name=name + "_point_cloud_xyzrgb_node",
                    namespace=namespace,
                    remappings=[
                        ("depth_registered/image_rect", name + "/stereo/image_raw"),
                        ("rgb/image_rect_color", rgb_topic_name),
                        ("rgb/camera_info", name + "/rgb/camera_info"),
                        ("points", name + "/points"),
                    ],
                ),
            ],
        )
    )

    return actions


def generate_launch_description():
    depthai_prefix = get_package_share_directory("depthai_ros_driver")
    declared_arguments = [
        DeclareLaunchArgument("name", default_value="oak"),
        DeclareLaunchArgument("namespace", default_value="", description="Namespace for the nodes."),
        DeclareLaunchArgument(
            "params_file",
            default_value=os.path.join(depthai_prefix, "config", "rgbd.yaml"),
        ),
        DeclareLaunchArgument("ffmpeg_params_file"),
        DeclareLaunchArgument("rectify_rgb", default_value="true"),
        DeclareLaunchArgument(
            "enable_pointcloud",
            default_value="false",
            description="Load depth_image_proc::PointCloudXyzrgbNode and force RGB/stereo sync.",
        ),
    ]

    return LaunchDescription(
        declared_arguments + [OpaqueFunction(function=launch_setup)]
    )
