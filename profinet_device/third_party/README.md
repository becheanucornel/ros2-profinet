# Third-party software

`fetch.sh` gets these at exact commits and applies `patches/`. To use your own
copies, which is wise because RT-Labs has made newer versions evaluation-only:
`UPSTREAM=https://github.com/your-account sh fetch.sh`

| Software | Version | License |
|---|---|---|
| [p-net](https://github.com/rtlabs-com/p-net) | v0.2.0 (April 2022) | GPLv3 or commercial |
| [osal](https://github.com/rtlabs-com/osal) | 88784fc | BSD-3-Clause |
| [cmake-tools](https://github.com/rtlabs-com/cmake-tools) | 45c751c | BSD-3-Clause |

## Patches to p-net

| Patch | Without it |
|---|---|
| `0001-pull.patch` | The daemon crashes when the PLC disconnects. It must remove the modules then, to notice a changed PLC project, and p-net used a connection that no longer exists. |
| `0002-fast-ports.patch` | No connection on links faster than 1 Gbit/s, such as 2.5 Gbit/s ports and most virtual ones: p-net knows no speed above that and says that the port is too slow. |
| `0003-stack-lock.patch` | Possible crash when a connection closes while a frame arrives: the receive thread used memory and a mutex that the other thread had freed. It adds `pnal_lock()`, and also fixes saving the name and IP address while they change. |

`USE_SCHED_FIFO` is turned on in `profinet_device/CMakeLists.txt`, because the receive
thread otherwise has no real-time priority.
