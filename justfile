[private]
default:
    @just --list --unsorted

build target="humble":
    #!/bin/bash
    set -euo pipefail
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1

    if [ {{target}} == "humble" ]; then
        export ROS_DISTRO=humble
        export CORE_VERSION=core22
    elif [ {{target}} == "jazzy" ]; then
        export ROS_DISTRO=jazzy
        export CORE_VERSION=core24
    else
        echo "Unknown target: $target"
        exit 1
    fi

    ./render_template.py ./snapcraft_template.yaml.jinja2 snap/snapcraft.yaml

    snapcraft pack

install:
    #!/bin/bash
    set -euo pipefail
    unsquashfs husarion-depthai*.snap
    sudo snap try squashfs-root/
    sudo /var/snap/husarion-depthai/common/post_install.sh
    sudo husarion-depthai.stop

remove:
    #!/bin/bash
    sudo snap remove husarion-depthai || true
    sudo rm -rf squashfs-root/

clean:
    #!/bin/bash
    set -euo pipefail
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1
    snapcraft clean

iterate target="jazzy":
    #!/bin/bash
    set -euo pipefail
    start_time=$(date +%s)

    echo "Starting script..."

    sudo snap remove husarion-depthai || true
    sudo rm -rf squashfs-root/
    sudo rm -rf husarion-depthai*.snap
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1

    if [ {{target}} == "humble" ]; then
        export ROS_DISTRO=humble
    elif [ {{target}} == "jazzy" ]; then
        export ROS_DISTRO=jazzy
    else
        echo "Unknown target: {{target}}"
        exit 1
    fi

    snapcraft clean
    sudo rm -rf snap/snapcraft.yaml
    ./render_template.py ./snapcraft_template.yaml.jinja2 snap/snapcraft.yaml
    chmod 444 snap/snapcraft.yaml
    snapcraft pack

    sudo unsquashfs husarion-depthai*.snap
    sudo snap try squashfs-root/
    sudo /var/snap/husarion-depthai/common/post_install.sh
    sudo husarion-depthai.stop
    # sudo snap connect husarion-depthai:c189-plug rosbot:c189-slot

    end_time=$(date +%s)
    duration=$(( end_time - start_time ))

    hours=$(( duration / 3600 ))
    minutes=$(( (duration % 3600) / 60 ))
    seconds=$(( duration % 60 ))

    printf "Script completed in %02d:%02d:%02d (hh:mm:ss)\n" $hours $minutes $seconds

swap-enable:
    #!/bin/bash
    set -euo pipefail
    # 8G: a snapcraft pack of the heavier snaps (rosbot/moveit) OOMs on a 7.6 GB
    # no-swap box, and the in-docker colcon build of the pipeline plugin OOM-kills
    # (exit 137) too. 8G covers both. Idempotent: skip if /swapfile is already on.
    if swapon --show 2>/dev/null | grep -q /swapfile; then
        echo "/swapfile already active"; swapon --show; exit 0
    fi
    sudo fallocate -l 8G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    sudo swapon --show

    # Make the swap file permanent (skip if already in fstab):
    grep -q "^/swapfile " /etc/fstab || sudo bash -c "echo '/swapfile swap swap defaults 0 0' >> /etc/fstab"

    # Adjust swappiness:
    sudo sysctl vm.swappiness=10
    grep -q "^vm.swappiness=" /etc/sysctl.conf || sudo bash -c "echo 'vm.swappiness=10' >> /etc/sysctl.conf"

swap-disable:
    #!/bin/bash
    set -euo pipefail
    sudo swapoff /swapfile
    sudo rm /swapfile
    sudo sed -i '/\/swapfile swap swap defaults 0 0/d' /etc/fstab  # Remove the swap file entry
    sudo sed -i '/vm.swappiness=10/d' /etc/sysctl.conf  # Remove or comment out the swappiness setting
    sudo sysctl -p  # Reload sysctl configuration

prepare-store-credentials:
    #!/bin/bash
    set -euo pipefail
    snapcraft export-login --snaps=husarion-depthai \
      --acls package_access,package_push,package_update,package_release \
      exported.txt

publish:
    #!/bin/bash
    set -euo pipefail
    export SNAPCRAFT_STORE_CREDENTIALS=$(cat exported.txt)
    snapcraft login
    snapcraft upload --release edge husarion-depthai*.snap

list-lxd-cache:
    #!/bin/bash
    sudo du -h --max-depth=1 /var/snap/lxd/common/lxd/storage-pools/default/containers/ | sort -h

remove-lxd-cache:
    #!/bin/bash
    set -euo pipefail
    lxc project switch snapcraft

    for container in $(lxc list --format csv -c n); do
        lxc delete $container --force
    done

    echo "verify:"
    sudo du -h --max-depth=1 /var/snap/lxd/common/lxd/storage-pools/default/containers/
    df -h

    lxc project switch default
