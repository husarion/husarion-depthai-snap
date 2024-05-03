#!/usr/bin/bash

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "launcher: $message"
}

# Iterate over the snap parameters and retrieve their value.
# If a value is set, it is forwarded to the launch file.
OPTIONS="namespace mecanum include-camera-mount camera-model lidar-model"
LAUNCH_OPTIONS=""

for OPTION in ${OPTIONS}; do
  VALUE="$(snapctl get driver.${OPTION})"
  if [ -n "${VALUE}" ]; then
    LAUNCH_OPTIONS+="${OPTION}:=${VALUE} "
  fi
done

# Replace '-' with '_'
LAUNCH_OPTIONS=$(echo ${LAUNCH_OPTIONS} | tr - _)

if [ "${LAUNCH_OPTIONS}" ]; then
  # watch the log with: "journalctl -t rosbot-xl"
  log "Running with options: ${LAUNCH_OPTIONS}"
fi

ros2 launch rosbot_xl_bringup combined.launch.py ${LAUNCH_OPTIONS}
