# SPDX-License-Identifier: Apache-2.0
from setuptools import setup

name = "profinet_bridge"
setup(
    name=name,
    version="0.1.0",
    packages=[name],
    data_files=[
        ("share/ament_index/resource_index/packages", [f"resource/{name}"]),
        (f"share/{name}", ["package.xml"]),
        (f"share/{name}/config", ["config/config_profinet.xml", "config/config_profinet.xsd"]),
        (f"share/{name}/launch", ["launch/profinet.launch.py"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Becheanu Cornel-Constantin",
    maintainer_email="becheanucornel28@gmail.com",
    license="Apache-2.0",
    description="Maps ROS 2 topics to the modules of a PROFINET IO device.",
    entry_points={"console_scripts": [
        "profinet_bridge = profinet_bridge.node:main",
        "profinet_check = profinet_bridge.check:main",
    ]},
)
