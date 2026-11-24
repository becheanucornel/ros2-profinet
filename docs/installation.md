# Installation

You need Ubuntu 22.04 with ROS 2 Humble, a network port that is used only for
PROFINET, because the PLC sets its IP address, and:

```sh
sudo apt install build-essential cmake git python3-colcon-common-extensions
```

There are two parts. The **PROFINET service** is installed once per machine. The
**ROS 2 package** is built in each workspace that uses it.

## 1. Get the repository

Into the `src` folder of your ROS 2 workspace:

```sh
cd ~/ros2_ws/src
git clone -b humble https://github.com/becheanucornel/ros2-profinet.git
```

If your project is a Git repository itself, add it as a submodule instead:

```sh
git submodule add -b humble https://github.com/becheanucornel/ros2-profinet.git src/ros2-profinet
```

## 2. Install the PROFINET service

```sh
cd ~/ros2_ws/src/ros2-profinet/profinet_device
sh third_party/fetch.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo groupadd --system profinet
sudo usermod -aG profinet $USER      # log in again afterwards
sudo cmake --install build
```

The service runs as root, because it needs `NET_ADMIN`, `NET_RAW`, `SYS_NICE` and
`IPC_LOCK`. It does not start without real-time permission. The ROS 2 node needs no
rights, but its user must be in the group `profinet`: whoever can connect to the
service decides what the PLC receives.

## 3. Build the ROS 2 package

```sh
cd ~/ros2_ws
colcon build && . install/setup.sh
```

colcon finds only `profinet_bridge`. It skips the PROFINET service.

## 4. Write config_profinet.xml

Copy [the example](../profinet_bridge/config/config_profinet.xml) into your own
project and edit it there ([all settings](configuration.md)). If you edit it inside
this repository, `git pull` can conflict with your changes.

Put in the [IDs of your organisation](../gsdml/README.md#1-your-ids). Check the file, and
give its IDs, port and station name to the service:

```sh
ros2 run profinet_bridge profinet_check config_profinet.xml
ros2 run profinet_bridge profinet_check --service-env config_profinet.xml | sudo tee /etc/default/profinet_device
sudo systemctl daemon-reload
sudo systemctl enable --now profinet_device
```

If you change one of these settings later, write `/etc/default/profinet_device` again
and run `sudo systemctl restart profinet_device`.

Once the PLC or TIA Portal has set the device name, it is stored in
`/var/lib/profinet_device`, and `station_name` is ignored until the device is reset.

## 5. Start the node

```sh
ros2 launch profinet_bridge profinet.launch.py config:=/path/to/config_profinet.xml
```

## Update

```sh
cd ~/ros2_ws/src/ros2-profinet && git pull
cd profinet_device && sh third_party/fetch.sh   # see below
cmake --build build && sudo cmake --install build
sudo systemctl daemon-reload && sudo systemctl restart profinet_device
cd ~/ros2_ws && colcon build
```

If the update changed `profinet_device/third_party`, first delete `p-net` and `osal` in
it, so that `fetch.sh` gets them again.

Restart the node afterwards. If the date in the name of the GSDML file has changed,
install the new file in TIA Portal only if you want to use its new modules.
