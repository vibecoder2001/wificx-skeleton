# Forking this skeleton for real hardware

This skeleton is a template. Forking it for a real WiFi chip means
**replacing the fake chip backend** (`chip_fake.cpp` + the synthetic
BSS table in `chip_fake_bss.cpp`) with real-HW code, and leaving
the WDI/WiFiCx infrastructure above the vtable mostly untouched.

Step-by-step:

## 1. Point the INF at your hardware

`wificx-skl.inx`:

```inf
[Standard.NT$ARCH$.10.0...22000]
%WiFiCX_SKL.DeviceDesc% = WiFiCX_SKL_PCI.ndi, PCI\VEN_xxxx&DEV_yyyy
```

Replace `xxxx` / `yyyy` with your card's vendor/device IDs.
`pnputil /enum-devices` after install will confirm the bind.

## 2. Implement the chip-ops vtable

Create `chip_<vendor>.cpp` modeled on `chip_fake.cpp`. Implement
each `WDI_CHIP_OPS` member against your chip's interface (PCIe
config, MMIO registers, MCU/H2C commands, FW load). The
contract for each op is in `docs/chip_ops.md`.

The 11-op surface as of writing:

```
Init, Deinit
SetRadio
StartScan, AbortScan
Connect, Disconnect
ProgramKey, RemoveAllKeys, SetDefaultKeyId
Reset
```

Expect to add 2-4 more ops as you wire features the skeleton
doesn't cover yet (multicast filter programming, dynamic power
save, beacon-template push for WFD GO mode, etc.).

## 3. Swap the vtable installation

`device.cpp::MtkInitializeHardware`:

```cpp
pDevice->ChipOps = ChipFakeGetOps();   // before
pDevice->ChipOps = ChipMyChipGetOps(); // after
```

## 4. Delete the fake-only files

- `chip_fake.cpp`, `chip_fake.h`
- `chip_fake_bss.cpp`, `chip_fake_bss.h`

…and drop their entries from `wificx-skl.vcxproj` /
`wificx-skl.vcxproj.Filters`.

Or keep them around as a `#ifdef DEBUG_FAKE_CHIP` fallback — handy
when iterating on the WDI side without real HW attached.

## 5. Implement the TX/RX queues

The skeleton's `txqueue.cpp` / `rxqueue.cpp` are stubs:
`EvtTxQueueAdvance` is a no-op (doesn't consume packets — see
`wdi_contract.md` §4 for why returning `PortAuthorized=TRUE`
without a real TX advance BSODs NetAdapterCx). At minimum, the
advance handler must:

- Walk the packet ring from `BeginIndex` to `EndIndex`.
- For each packet, either push to HW or set `Ignore=1`.
- Update `BeginIndex` to mark progress.
- Do the same for the fragment ring.

Real chip backends drive DMA descriptors here, then complete
packets back to the framework when HW signals TX done (typically
via an ISR / DPC).

## 6. Wire the chip's FW debug stream

Route your chip's FW log channel through `WdiFwDebug()` /
`WdiFwDebugBytes()` macros in `trace.h`. ETW listeners (kd, WPP
viewers) pick them up uniformly. Skeleton already reserves the
event class.

## 7. Honor the PM tunables

`wificx-skl.inx` exposes `RuntimePmEnable` / `DeepSleepEnable` /
`IdleTimeoutMs` / `AspmL1ssEnable`. Read them in your `Init` op
via `WdfDeviceOpenRegistryKey(PLUGPLAY_REGKEY_DRIVER, ...)`. All
defaults are **off** — flip them on case-by-case during bring-up
once the data path is solid. PM-resume races are the most
common driver bug class.

## 8. Decide on assoc-result success semantics

The fake backend returns Connect failure (`StatusCode=1`) so the
OS never reaches the supplicant or tries to push data through the
TX queue. A real backend that has implemented (5) and (6) above
returns success (StatusCode=0), with:

- `PortAuthorized=TRUE` for an Open BSS.
- `PortAuthorized=FALSE` for RSNA — the OS supplicant runs the
  4-way handshake and pushes derived keys via `ProgramKey` once
  it completes.

## What you do NOT touch

The WDI layer above the vtable should not need vendor-specific
changes:

- `wdihandlers.cpp` — WDI message dispatcher + parser
- `wdi_tlv.{h,cpp}` — indication firing helpers, TxnId discipline
- `wdi_mmpdu.{h,cpp}` — MMPDU-body contract enforcement
- `wdi_connect.{h,cpp}` — connect indication emitters
- `wdi_keys.{h,cpp}` — key descriptor normalization
- `wdi_scan_cache.{h,cpp}` — BSSID-keyed result cache
- `wdi_frame.{h,cpp}` — 802.11 frame/IE utilities

If you find yourself patching these, ask whether the change belongs
upstream in the skeleton.
