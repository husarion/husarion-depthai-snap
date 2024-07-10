#!/bin/bash -e

# Get the namespace and device namespace from snapctl
NAMESPACE="$(snapctl get driver.namespace)"
NAME="$(snapctl get driver.name)"

# Construct the topic name, ensuring no double slashes
TOPIC="/${NAMESPACE}/${NAME}/rgb/image_raw/ffmpeg"

# Remove any leading or trailing slashes and ensure the topic starts with a single slash
TOPIC=$(echo $TOPIC | sed 's:^/*::' | sed 's:/*$::')
TOPIC="/$TOPIC"

echo "Running image_view with topic: $TOPIC"

# find . -name "libcanberra-gtk-module.so"
# export LD_LIBRARY_PATH=$SNAP/usr/lib/$(uname -m)-linux-gnu/gtk-2.0/modules/libcanberra-gtk-module.so:$LD_LIBRARY_PATH

# Run the image_view node with the constructed topic
ros2 run image_view image_view --ros-args --remap image/ffmpeg:=$TOPIC -p image_transport:=ffmpeg
