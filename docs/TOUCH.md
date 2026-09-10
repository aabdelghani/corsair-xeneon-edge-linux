# Xeneon Edge: the touch stack on Linux

Status legend matches [PROTOCOL.md](../PROTOCOL.md): **VERIFIED** (proven
against this device), **CANDIDATE**, **HYPOTHESIS**.

`PROTOCOL.md` covers the Bragi control channel (`1b1c:1d0d`). Touch does not
travel on that channel at all. This document covers the second USB device, and
answers the question that keeps coming up: does the Edge do multi-touch on
Linux, or single-contact only?

**Short answer: full multi-touch, and it works out of the box. No quirk, no
udev rule, no manual mode switch.** VERIFIED.

The panel advertises 15 simultaneous contacts. Measured on hardware with
`xeneonctl touch --live`: **peak 10 simultaneous contacts**, which is both
hands flat on the panel and as far as a person can test without help. Nothing
degraded or dropped on the way to 10.

Reproduce any of this with:

```
xeneonctl touch            # static report: kernel + X server view
xeneonctl touch --live 15  # measure real contacts under your fingers
```

## 1. The Edge is two USB devices: VERIFIED

| Device | USB ID | Role |
|--------|--------|------|
| `CORSAIR XENEON EDGE` | `1b1c:1d0d` | Bragi vendor channel, usage page `0xFF1B`. Display and device control. See `PROTOCOL.md`. |
| `wch.cn TouchScreen` | `27c0:0859` | The touch controller. A separate WCH part; nothing Corsair-specific about it. |

The touch controller exposes **three** HID interfaces:

| Interface | Descriptor | Kernel driver | evdev node |
|-----------|-----------|---------------|------------|
| `...0859.0012` | 704 bytes | **`hid-multitouch`** | `/dev/input/eventN` (the real digitizer) |
| `...0859.0013` | 38 bytes | `hid-generic` | none |
| `...0859.0014` | 80 bytes | `hid-generic` | `/dev/input/eventM` (absolute-pointer emulation) |

Both evdev nodes are named `wch.cn TouchScreen`, which is a good way to lose an
afternoon. They are not interchangeable. Only `.0012` carries contacts.

## 2. What the digitizer descriptor declares: VERIFIED

Parsed from `/sys/bus/hid/devices/0003:27C0:0859.0012/report_descriptor`:

- **10 Digitizer/Finger collections**, each with Tip Switch (`0D:42`) and
  Contact ID (`0D:51`).
- **Contact Count Maximum = 15**, in a feature report.
- Scan Time and Contact Count, as a Windows-8-style touchscreen requires.
- A **Device Configuration** collection (`0D:0E`) on **feature report `0x21`**,
  two bytes: Device Mode (`0D:52`) and Device Identifier (`0D:53`), logical
  range 0-10. This is the input-mode switch.

So the hardware advertises far more than the four or five contacts most people
need, and it advertises it in the standard place.

## 3. What actually happens on Linux: VERIFIED

`hid-multitouch` claims interface `.0012` automatically at enumeration, and it
does so generically. `hid-core` scans the report descriptor, sees a digitizer
with per-finger Contact IDs, and classifies the interface into HID group
`0x0002` (`HID_GROUP_MULTITOUCH`). The resulting modalias is:

```
hid:b0003g0002v000027C0p00000859
```

which is claimed by `hid-multitouch`'s wildcard alias `hid:b*g0002v*p*`. There
is **no `27c0:0859` entry** anywhere in the module's 111 aliases, and no quirk
in `hid-ids.h`. Nothing about this panel is special-cased. It simply describes
itself correctly and the generic path picks it up.

As part of `mt_set_input_mode()` the driver writes the Device Configuration
feature report (`0x21`) to put the panel into multi-input mode. That write is
the unlock. Without it the panel stays in the pointer emulation that interface
`.0014` provides, which is single-contact by design.

The result, from the X server:

```
id=23  "wch.cn TouchScreen"  node=/dev/input/event22
    XITouchClass: max 15 simultaneous contacts, direct touch
id=22  "wch.cn TouchScreen"  node=/dev/input/event23
    XITouchClass: absent (pointer-emulation interface)
```

The digitizer node also exposes the type-B multitouch axes (`ABS_MT_SLOT`,
`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`), which is what a compositor or
toolkit needs for gestures.

## 4. If you are porting to another OS

The panel is not the problem. Two things decide whether you get multi-touch:

1. **Something must write feature report `0x21`** to set Device Mode. On Linux
   `hid-multitouch` does this for you. If your OS binds only a generic HID
   driver, or binds a mouse driver to interface `.0014` and stops, you will see
   exactly one contact plus a button press, and the digitizer interface will
   look dead. The interface is not dead. It has not been switched on.
2. **Write it to interface `.0012`.** A control transfer aimed at the wrong
   interface number is silently ignored by this controller. It does not stall
   and it does not answer, which is easy to misread as unsupported firmware.

`hid-multitouch` reads the current feature report first, replaces only the
Device Mode byte, and writes both bytes back, preserving the Device Identifier.
Writing a bare mode byte, or writing both bytes with a guessed identifier, is
worth ruling out before concluding the collection is unbacked.

## 5. How this project uses touch

`src/x11/TouchEventSource.cpp` selects XI2 raw touch on the root window and
tracks contacts by touch id, so it follows several at once. The touch modes in
`TouchControl` decide whether the panel drives the system cursor, a second
independent X pointer, or nothing at all while the app still watches it.

`src/x11/TouchProbe.cpp` implements `xeneonctl touch`. It is strictly
read-only: it reads sysfs and queries the X server, and sends nothing to either
USB device.
