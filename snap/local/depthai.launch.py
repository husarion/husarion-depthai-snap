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

    rgb_topic_name = name + "/rgb/image_raw"
    if LaunchConfiguration("rectify_rgb").perform(context) == "true":
        rgb_topic_name = name + "/rgb/image_rect"

    container_name = f"/{namespace}/{name}_container" if namespace else f"/{name}_container"

    actions = []

    actions.append(
        ComposableNodeContainer(
            name=name + "_container",
            namespace=namespace,
            package="rclcpp_components",
            # _mt (multithreaded) executor — single-threaded `component_container`
            # was losing 99% of intra-container deliveries between depthai Camera
            # and image_proc::RectifyNode after external subscriber churn, leaving
            # image_rect at ~1 Hz until daemon restart.
            executable="component_container_mt",
            composable_node_descriptions=[
                ComposableNode(
                    package="depthai_ros_driver",
                    plugin="depthai_ros_driver::Camera",
                    name=name,
                    namespace=namespace,
                    parameters=[params_file],
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
                    # ffmpeg_params_file restricts this node's own lazy image_transport
                    # republishers (enable_pub_plugins — excludes compressedDepth, wrong
                    # for 8-bit RGB) and configures the ffmpeg encoder. See
                    # ffmpeg-params-default.yaml.
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
                        ("image_rect/zstd", name + "/rgb/image_rect/zstd"),
                        ("image_rect/ffmpeg", name + "/rgb/image_rect/ffmpeg"),
                    ],
                )
            ],
        )
    )

    actions.append(
        LoadComposableNodes(
            condition=IfCondition(LaunchConfiguration("pointcloud")),
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
        DeclareLaunchArgument("rectify_rgb", default_value="true"),
        DeclareLaunchArgument("ffmpeg_params_file"),
        DeclareLaunchArgument(
            "pointcloud",
            default_value="false",
            description="Load depth_image_proc::PointCloudXyzrgbNode (requires driver.depth=true).",
        ),
    ]

    return LaunchDescription(
        declared_arguments + [OpaqueFunction(function=launch_setup)]
    )
