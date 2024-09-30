#!/bin/bash -e

source $SNAP/usr/bin/utils.sh

# Iterate over the snap parameters and retrieve their value.
# If a value is set, it is forwarded to the launch file.

OPTIONS=(
  name
  parent-frame
  camera-model
  cam-pos-x
  cam-pos-y
  cam-pos-z
  cam-roll
  cam-pitch
  cam-yaw
  # params-file
)

LAUNCH_OPTIONS=""

for OPTION in "${OPTIONS[@]}"; do
  VALUE="$(snapctl get driver.${OPTION})"
  if [ -n "${VALUE}" ]; then
    OPTION_WITH_UNDERSCORE=$(echo ${OPTION} | tr - _)
    LAUNCH_OPTIONS+="${OPTION_WITH_UNDERSCORE}:=${VALUE} "
  fi
done

# Check if ros.namespace is set and not empty
ROS_NAMESPACE="$(snapctl get ros.namespace)"
if [ -n "${ROS_NAMESPACE}" ]; then
    LAUNCH_OPTIONS+="namespace:=${ROS_NAMESPACE} "
fi

CAMERA_PARAMS="$(snapctl get driver.camera-params)"
LAUNCH_OPTIONS+="params_file:=${SNAP_COMMON}/camera-params-${CAMERA_PARAMS}.yaml "

FFMPEG_PARAMS="$(snapctl get driver.ffmpeg-params)"
LAUNCH_OPTIONS+="ffmpeg_params_file:=${SNAP_COMMON}/ffmpeg-params-${FFMPEG_PARAMS}.yaml "

if [ "${LAUNCH_OPTIONS}" ]; then
  # watch the log with: "journalctl -t husarion-astra"
  log_and_echo "Running with options: ${LAUNCH_OPTIONS}"
fi

# TODO: workaround for the booting issue: sometimes the snap doesn't work correctly after reboot (only reboot) but logs seems to look fine
sleep 7

ros2 launch $SNAP/usr/bin/depthai.launch.py ${LAUNCH_OPTIONS}
