build:
    #!/bin/bash
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1
    snapcraft

install:
    #!/bin/bash
    unsquashfs husarion-depthai*.snap
    sudo snap try squashfs-root/
    sudo snap connect husarion-depthai:raw-usb
    sudo snap connect husarion-depthai:c189-plug
    sudo husarion-depthai.stop

remove:
    #!/bin/bash
    sudo snap remove husarion-depthai
    sudo rm -rf squashfs-root/

clean:
    #!/bin/bash
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1
    snapcraft clean   

iterate:
    #!/bin/bash
    start_time=$(date +%s)
    
    echo "Starting script..."

    sudo snap remove husarion-depthai
    sudo rm -rf squashfs-root/
    sudo rm -rf husarion-depthai*.snap
    export SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1
    snapcraft clean
    snapcraft
    unsquashfs husarion-depthai*.snap
    sudo snap try squashfs-root/
    sudo snap connect husarion-depthai:raw-usb
    # sudo snap connect husarion-depthai:c189-plug rosbot:c189-slot

    end_time=$(date +%s)
    duration=$(( end_time - start_time ))

    hours=$(( duration / 3600 ))
    minutes=$(( (duration % 3600) / 60 ))
    seconds=$(( duration % 60 ))

    printf "Script completed in %02d:%02d:%02d (hh:mm:ss)\n" $hours $minutes $seconds

swap-enable:
    #!/bin/bash
    sudo fallocate -l 3G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    sudo swapon --show

    # Make the swap file permanent:
    sudo bash -c "echo '/swapfile swap swap defaults 0 0' >> /etc/fstab"

    # Adjust swappiness:
    sudo sysctl vm.swappiness=10
    sudo bash -c "echo 'vm.swappiness=10' >> /etc/sysctl.conf"

swap-disable:
    #!/bin/bash
    sudo swapoff /swapfile
    sudo rm /swapfile
    sudo sed -i '/\/swapfile swap swap defaults 0 0/d' /etc/fstab  # Remove the swap file entry
    sudo sed -i '/vm.swappiness=10/d' /etc/sysctl.conf  # Remove or comment out the swappiness setting
    sudo sysctl -p  # Reload sysctl configuration

publish:
    #!/bin/bash
    snapcraft login
    snapcraft upload --release edge husarion-depthai*.snap