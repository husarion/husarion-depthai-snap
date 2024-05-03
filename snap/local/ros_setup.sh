#!/usr/bin/bash -e

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "ros_setup: $message"
}

FASTDDS_FILE=$(snapctl get fastdds-default-profiles-file)

if [ ! -f "${SNAP_COMMON}/${FASTDDS_FILE}" ]; then
  # eg. /var/snap/rosbot-xl/common/fastdds.xml
  log "${SNAP_COMMON}/${FASTDDS_FILE} does not exist."
fi

if [ -n "${FASTDDS_FILE}" ] && [ -f "${SNAP_COMMON}/${FASTDDS_FILE}" ]; then
  export FASTRTPS_DEFAULT_PROFILES_FILE=${SNAP_COMMON}/${FASTDDS_FILE}
  log "Using FASTRTPS profile: ${FASTRTPS_DEFAULT_PROFILES_FILE}"
  log "$(cat $FASTRTPS_DEFAULT_PROFILES_FILE)"
else
  TRANSPORT="$(snapctl get transport)"
  # watch the log with: "journalctl -t rosbot-xl"
  log "transport: ${TRANSPORT}"

  case "$TRANSPORT" in
  shm)
    export FASTRTPS_DEFAULT_PROFILES_FILE=$SNAP/usr/share/rosbot-xl/config/shm-only.xml
    log "$(cat $FASTRTPS_DEFAULT_PROFILES_FILE)"
    ;;
  udp)
    export FASTRTPS_DEFAULT_PROFILES_FILE=$SNAP/usr/share/rosbot-xl/config/udp-only.xml
    log "$(cat $FASTRTPS_DEFAULT_PROFILES_FILE)"
    ;;
  builtin)
    log "using builtin transport"
    ;;
  esac
fi

export ROS_LOCALHOST_ONLY="$(snapctl get ros-localhost-only)"
export ROS_DOMAIN_ID="$(snapctl get ros-domain-id)"

log "ROS_LOCAHOST_ONLY=${ROS_LOCALHOST_ONLY}"
log "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"

exec $@