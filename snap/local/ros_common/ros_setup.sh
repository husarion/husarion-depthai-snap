#!/usr/bin/bash -e

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "ros_setup: $message"
}

TRANSPORT="$(snapctl get transport)"
# watch the log with: "journalctl -t ${SNAP_NAME}"
log "transport: ${TRANSPORT}"

if [ "$TRANSPORT" = "builtin" ]; then
  log "using builtin transport setting"
else
  PROFILE_FILE="${SNAP_COMMON}/${TRANSPORT}.xml"
  export FASTRTPS_DEFAULT_PROFILES_FILE=$PROFILE_FILE
  log "using ${PROFILE_FILE} transport setting"
  log "$(cat $FASTRTPS_DEFAULT_PROFILES_FILE)"
fi

export ROS_LOCALHOST_ONLY="$(snapctl get ros-localhost-only)"
export ROS_DOMAIN_ID="$(snapctl get ros-domain-id)"

log "ROS_LOCAHOST_ONLY=${ROS_LOCALHOST_ONLY}"
log "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"

exec $@