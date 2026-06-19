#!/bin/bash -e

source "$SNAP/usr/bin/utils.sh"

# Iterate over the snap parameters and retrieve their value.
# If a value is set, it is forwarded to the launch file.

OPTIONS=(
  name
  enable-pointcloud
  rectify-rgb
)

LAUNCH_OPTIONS=()

CAMERA_PARAMS="$(snapctl get driver.camera-params)"

# The chip-encoded H.264 preset (streaming-h264, i_low_bandwidth=true) drops the
# raw RGB topic, so the rectify + point-cloud nodes would have no input. Force
# both off regardless of their snap config so selecting this preset from the
# cockpit Manage tab can never produce a half-broken pipeline.
for OPTION in "${OPTIONS[@]}"; do
  VALUE="$(snapctl get "driver.${OPTION}")"
  if [ "${CAMERA_PARAMS}" = "streaming-h264" ]; then
    case "${OPTION}" in
      rectify-rgb | enable-pointcloud) VALUE="false" ;;
    esac
  fi
  if [ -n "${VALUE}" ]; then
    OPTION_WITH_UNDERSCORE="${OPTION//-/_}"
    LAUNCH_OPTIONS+=("${OPTION_WITH_UNDERSCORE}:=${VALUE}")
  fi
done

# Check if ros.namespace is set and not empty
ROS_NAMESPACE="$(snapctl get ros.namespace)"
if [ -n "${ROS_NAMESPACE}" ]; then
    LAUNCH_OPTIONS+=("namespace:=${ROS_NAMESPACE}")
fi

LAUNCH_OPTIONS+=("params_file:=${SNAP_DATA}/camera-params-${CAMERA_PARAMS}.yaml")

FFMPEG_PARAMS="$(snapctl get driver.ffmpeg-params)"
LAUNCH_OPTIONS+=("ffmpeg_params_file:=${SNAP_DATA}/ffmpeg-params-${FFMPEG_PARAMS}.yaml")

# watch the log with: "journalctl -t husarion-depthai"
log_and_echo "Running with options: ${LAUNCH_OPTIONS[*]}"

# Workaround for first-startup-after-reboot race in depthai-ros: the camera
# sometimes fails to come up if the driver starts within seconds of system boot.
# We delay only on a "fresh boot" (low /proc/uptime), so subsequent daemon
# restarts don't pay the cost and don't risk a retry loop on a broken launch.
#
# TODO: verify against upstream on every depthai-ros bump:
#   https://github.com/luxonis/depthai-ros/releases — check if first-start race is fixed.
#   Smoke test: snap set husarion-depthai driver.startup-delay=0; reboot; expect topics within ~5s.
STARTUP_DELAY="$(snapctl get driver.startup-delay)"
if [ "${STARTUP_DELAY:-0}" -gt 0 ]; then
  UPTIME=$(awk '{print int($1)}' /proc/uptime)
  if [ "${UPTIME}" -lt $((STARTUP_DELAY + 60)) ]; then
    echo "[$(date +"%T")] fresh boot (uptime=${UPTIME}s) — sleeping ${STARTUP_DELAY}s"
    sleep "${STARTUP_DELAY}"
    echo "[$(date +"%T")] done"
  fi
fi

ros2 launch "$SNAP/usr/bin/depthai.launch.py" "${LAUNCH_OPTIONS[@]}"
