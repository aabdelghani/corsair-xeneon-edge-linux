# Xeneon Edge: HID protocol notes

> This document covers the Bragi control channel (`1b1c:1d0d`) only. Touch
> travels on a **separate** USB device (`27c0:0859`) and is documented in
> [docs/TOUCH.md](docs/TOUCH.md).

Status legend: **VERIFIED** (proven against this device), **CANDIDATE** (from
open-source analogues, not yet sent), **HYPOTHESIS** (inference).

All protocol *facts* below are re-implemented from open sources; no code is
copied. Sources are GPL (OpenRGB GPL-2, OpenLinkHub GPL-3), hence this project
is GPL-3.0.

## 1. Device identity: VERIFIED

- USB `1b1c:1d0d`, one vendor HID interface, hidraw node `/dev/hidraw4`.
- USB strings: manufacturer `CORSAIR`, product `XENEON EDGE`, serial `634326065656`.
- Report descriptor (32 bytes, read from `sysfs .../report_descriptor`):
  - Usage Page `0xFF1B` (vendor), Usage `0x91`, Application collection
  - **Report ID `0x01`**, Input 63 bytes, Output 63 bytes
  - => transfers are 64-byte reports: `[0x01][63 payload bytes]`

The `0xFF1B` vendor page + 64-byte in/out reports is the signature of Corsair's
modern **"Bragi" / Protocol V2** device family.

## 2. Bragi framing: HYPOTHESIS (to confirm in M4)

Two framings appear in the wild for this family; the Edge is expected to match
one of them. The payload (everything after the report ID) begins with a command
pair `{group, id}`:

- **iCUE-Link style** (OpenLinkHub `src/devices/lsh`): `[report_id=0x00]
  [0x00][0x01][cmd bytes...][data...]` on 512-byte packets. The `0x01` at
  offset 2 is a fixed "start" marker.
- **Peripheral-V2 style** (OpenRGB `CorsairPeripheralV2Controller`): `[report_id]
  [write_cmd=0x08/0x09][command][value...]` on 65-byte writes.

The Edge's 64-byte reports (report id `0x01`) most resemble the compact V2
peripheral framing. First bytes to try for the fw-version read (M4):
`payload = {0x02, 0x13}` (see command table).

## 3. Command table

| Name | Bytes (group,id) | Dir | Status | Source |
|------|------------------|-----|--------|--------|
| Get firmware version | `0x02 0x13` | read | **VERIFIED (framing)** | OpenLinkHub `cmdGetFirmware`; OpenRGB `CMD_GET=0x02` |
| Get property (generic) | `0x02 <prop>` | read | CANDIDATE | OpenRGB `CORSAIR_V2_CMD_GET` |
| Set property (generic) | `0x01 <prop> <val>` | write | CANDIDATE | OpenRGB `CMD_SET=0x01`; OpenLinkHub `cmdSoftwareMode 01 03 00 02` |
| Enter software mode | `0x01 0x03 0x00 0x02` | write | CANDIDATE | OpenLinkHub `cmdSoftwareMode` |
| Enter hardware mode | `0x01 0x03 0x00 0x01` | write | CANDIDATE | OpenLinkHub `cmdHardwareMode` |
| Open endpoint | `0x0D 0x01` | write | CANDIDATE | OpenLinkHub `cmdOpenEndpoint`; OpenRGB `CMD_START_TX=0x0D` |
| Close endpoint | `0x05 0x01 0x01` | write | CANDIDATE | OpenLinkHub `cmdCloseEndpoint`; OpenRGB `CMD_STOP_TX=0x05` |
| Read data | `0x08 0x01` | read | CANDIDATE | OpenLinkHub `cmdRead` |
| LCD brightness (Link) | `0x03 0x0b 0x64 0x01` | write | HYPOTHESIS | OpenLinkHub `cmdLcdBrightness`: Edge is a display, may differ |
| LCD power off (Link) | `0x03 0x0b 0x00 0x01` | write | HYPOTHESIS | OpenLinkHub `cmdLcdOff` |

## 4. Read-only recon (M3): what `edgeline probe` does

1. Print USB identity + strings (VERIFIED above).
2. Decode + dump the 32-byte report descriptor from sysfs.
3. Passive read: `hid_read_timeout` with a short timeout to see whether the
   device emits unsolicited input reports (e.g. touch/telemetry). **No writes.**

## 4a. M4 result: framing VERIFIED against the device

Sending `TX = 01 02 13 00…` (report id 0x01, GET, prop 0x13) produced:

```
RX = 01 02 13 00 00 14 FF FF FF FF FF FF FF FF FF FF FF FF FF FF
     FF FF FF FF FF FF 00 00 …
```

The device **echoes the command** (`02 13`) then returns `00 00 14` followed by
twenty `FF` bytes. This confirms:
- The Bragi framing (report id 0x01, `{GET, prop}` payload) is correct: the
  Edge is a live, responding HID endpoint.
- Property `0x13` on the Edge returns `0x14`(=20) then 20×`0xFF`, i.e. this
  property slot is empty/not-a-version on the Edge (firmware is likely a
  different property id, or requires the open-endpoint transaction first).

Alternate framing `TX = 01 08 01 02 13…` (Link-style read) returned
`01 08 01 00 13 0D …`: also a valid echo, useful for the block-read path later.

Next protocol step (M5): enumerate property ids by GET-sweeping and/or run the
open-endpoint (`0x0D 0x01`) → read (`0x08 0x01`) transaction to pull structured
data (firmware, panel info, orientation).

## 5. Writes are gated (M4+)

Every outbound report goes through `core/WriteGate`, which requires explicit
user confirmation and logs the exact 64-byte TX and any RX to
`~/.local/share/edgeline/hid.log`. The first gated write will be the
CANDIDATE fw-version read; results get promoted to VERIFIED here.

## 6. Deliberately out of scope

Firmware flashing, Corsair telemetry/cloud, and any binary blobs. Display
picture controls (brightness/contrast/color) use DDC/CI via `ddcutil`, not HID
(see `src/core/DdcClient`).

## Sources
- OpenRGB `Controllers/CorsairPeripheralV2Controller/` (GPL-2)
- OpenLinkHub `src/devices/lsh/lsh.go` and `src/devices/xeneonedge/` (GPL-3)
- The device's own report descriptor (this machine)
