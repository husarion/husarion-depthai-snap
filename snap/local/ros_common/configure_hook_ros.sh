#!/bin/sh -e

# The configure hook is called every time one the following actions happen:
# - initial snap installation
# - snap refresh
# - whenever the user runs snap set|unset to change a configuration option

# Define a function to log and echo messages
log_and_echo() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "configure hook: $message"
    # Echo the message to standard error
    echo >&2 "$message"
}

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "configure hook: $message"
}

# Make sure ROS_LOCALHOST_ONLY is valid
OPT="ros-localhost-only"
ROS_LOCALHOST_ONLY="$(snapctl get ${OPT})"
if [ -n "${ROS_LOCALHOST_ONLY}" ]; then
  case "${ROS_LOCALHOST_ONLY}" in
  1) ;;
  0) ;;
  *)
    log_and_echo "'${ROS_LOCALHOST_ONLY}' is not a supported value for '${OPT}'." \
      "Possible values are 0 or 1."
    exit 1
    ;;
  esac
fi

# Make sure ROS_DOMAIN_ID is valid
OPT="ros-domain-id"
ROS_DOMAIN_ID="$(snapctl get ${OPT})"

is_integer() {
  expr "$1" : '-\?[0-9][0-9]*$' >/dev/null 2>&1
}

if ! is_integer "${ROS_DOMAIN_ID}" || [ "${ROS_DOMAIN_ID}" -lt 0 ] || [ "${ROS_DOMAIN_ID}" -gt 232 ]; then
  log_and_echo "'${ROS_DOMAIN_ID}' is not a supported value for '${OPT}'. Possible values are integers between 0 and 232."
  exit 1
fi

# Get the transport setting using snapctl
OPT="transport"
TRANSPORT_SETTING="$(snapctl get ${OPT})"

# Only exit with status 1 if conditions are not met
if [ "$TRANSPORT_SETTING" != "builtin" ] && [ ! -f "${SNAP_COMMON}/${TRANSPORT_SETTING}.xml" ]; then
  log_and_echo "'${SNAP_COMMON}/${TRANSPORT_SETTING}.xml' does not exist."
  exit 1
fi

if [ "$TRANSPORT_SETTING" = "builtin" ] || [ "$TRANSPORT_SETTING" = "shm" ]; then
  if ! snapctl is-connected shm-plug; then
    log_and_echo "to use 'builtin' and 'shm' tranport shm-plug need to be connected, please run:"
    log_and_echo "sudo snap connect ${SNAP_NAME}:shm-plug ${SNAP_NAME}:shm-slot"
    exit 1
  fi
fi

# Make sure ros-humble-ros-base is connected
ROS_PLUG="ros-humble-ros-base"

if ! snapctl is-connected ${ROS_PLUG}; then
    log_and_echo "Plug '${ROS_PLUG}' isn't connected. Please run:"
    log_and_echo "snap connect ${SNAP_NAME}:${ROS_PLUG} ${ROS_PLUG}:${ROS_PLUG}"
    exit 1
fi
