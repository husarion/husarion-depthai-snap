#!/usr/bin/bash

# Define a function to log and echo messages
log_and_echo() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "${SNAP_NAME}.check_daemon_running.sh: $message"
    # Echo the message to standard error
    echo >&2 "$message"
}

if snapctl services ${SNAP_NAME}.daemon | grep -qw active; then
    log_and_echo "to run ${SNAP_NAME} snap interactively, you need to stop the daemon first, run:"
    log_and_echo "sudo ${SNAP_NAME}.stop"
    exit 1
fi

exec $@