#!/bin/sh -e

# Define a function to log messages
log() {
  local message="$1"
  # Log the message with logger
  logger -t "${SNAP_NAME}" "install hook: $message"
}


snapctl set transport="udp"
snapctl set ros-localhost-only=0
snapctl set ros-domain-id=0

if ! snapctl is-connected ros-humble-ros-base; then
  log "Plug 'ros-humble-ros-base' isn't connected, please run:"
  log "sudo snap connect ${SNAP_NAME}:ros-humble-ros-base ros-humble-ros-base:ros-humble-ros-base"
fi

if ! snapctl is-connected shm-plug; then
  log "Plug 'shm-plug' isn't connected, please run:"
  log "sudo snap connect ${SNAP_NAME}:shm-plug ${SNAP_NAME}:shm-slot"
fi

# copy DDS config files to shared folder
cp -r $SNAP/usr/share/${SNAP_NAME}/config/*.xml ${SNAP_COMMON}/