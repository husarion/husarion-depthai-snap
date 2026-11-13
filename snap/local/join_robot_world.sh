#!/bin/bash -e
# Copyright 2026 Husarion sp. z o.o.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Daemon command-chain step before ros_setup.sh: re-read the ros-world mount on
# every start, so a plug connected before rosbot rendered its files, or a later
# re-render, is picked up without a configure. Not mounted: ros.env stays as
# the configure hook rendered it.
source "$SNAP/usr/bin/utils.sh"
source "$SNAP/usr/bin/robot_world.sh"

hsc_join_robot_world "${SNAP_COMMON}/ros-world" || true

exec "$@"
