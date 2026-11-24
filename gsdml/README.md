# GSDML file

This folder is for the Windows PC with TIA Portal. Nothing here depends on the
Linux machine or on ROS 2. You put in your IDs once. After that, the file does not
change, whatever the topics are.

The file follows the schema of TIA Portal V21 for GSDML V2.4. It has not been
imported in TIA Portal yet, so menu names below may differ.

## 1. Your IDs

A PROFINET device is identified by two numbers:

| Number | Meaning | From |
|---|---|---|
| Vendor ID | The organisation | [PI assigns it](https://www.profibus.com/products/ident-numbers), free of charge |
| Device ID | The device | You choose one that no other device of your organisation has |

The file has placeholders instead, and TIA Portal does not accept it before they are
replaced. Never use the IDs of someone else.

1. Copy the file out of this repository, so that `git pull` does not conflict with it.
2. Open the copy in a text editor and replace:

   | Placeholder | With | Example |
   |---|---|---|
   | `YOUR_VENDOR_ID` | Vendor ID, as `0x` and four hexadecimal digits | `0x1234` |
   | `YOUR_DEVICE_ID` | Device ID, in the same way | `0x0001` |
   | `YOUR_ORGANISATION` (two places) | Name that TIA Portal shows as the manufacturer | `Example Ltd` |

3. Put the same two IDs into `config_profinet.xml` on the Linux machine. They must be
   the same in both places, but only the PLC can notice a difference: the device does
   not know the GSDML file. `profinet_check` shows the IDs of the device.

Keep the copy, and use it for all your projects. All your machines with this device have
the same two IDs, and differ in their PROFINET names. Changing an ID later means
replacing the device in the PLC projects.

## 2. Use

1. In TIA Portal: **Options > Manage general station description files (GSD)**,
   choose the folder of your copy, select the file and install it.
2. In the hardware catalog, find **ROS 2 device** under *Other field devices >
   PROFINET IO > I/O*, and place it in the network view.
3. Connect it to the PLC. Use an update time of 8 ms or more at first.
4. Give the device its PROFINET name: either the one in `config_profinet.xml`, or
   assign one online with **Assign device name**. **Flash LED** is written to the
   log of the Linux machine, which has no PROFINET LED.
5. Place modules in slots 2 to 32. Slot 1 always holds **Node status**. Use the
   same modules and slots as in `config_profinet.xml`.

**IN** modules carry data to the PLC, **OUT** modules carry data from the PLC.

| Type | Counts | Bytes per element |
|---|---|---|
| BOOL | 8, 16, 32 | 1 bit |
| INT | 1, 2, 4, 8 | 2 |
| DINT | 1, 2, 4, 8 | 4 |
| REAL | 1, 2, 4, 8, 16 | 4 |
| RAW | 8, 16, 32, 64 | 1 |

Each direction can have 1440 bytes, status bytes included. `profinet_check` shows
how many are used.

## Node status (slot 1)

| Offset | Type | Meaning |
|---|---|---|
| 0 | DWORD | Mapping ID. Compare it with the value `profinet_check` prints, and ignore all data if it differs: someone changed the XML file. |
| 4 | WORD | State: bit 0 node connected, bit 1 a slot has another module than the XML, bit 2 data of an IN slot is stale or bad, bit 3 a slot of the XML is missing in the PLC |
| 6 | WORD | Heartbeat, counts up while the node runs |

The PLC always receives this module, also while the ROS 2 node is stopped. Then
bit 0 is off, the heartbeat stands still, and all other input data is bad.

## Certification

The project is not certified by PI. An organisation that puts a device on the market
is responsible for its certification.

## Changing the file

Never change the ident number or the data of a module that is in use. Add new
modules instead, here, in `profinet_device/src/modules.c` and in
`profinet_bridge/profinet_bridge/modules.py`, and give the file a new date.
All three must agree.
