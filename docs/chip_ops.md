# Chip-Ops vtable contract

`wdi_chip_ops.h` defines `WDI_CHIP_OPS`, the surface between the
generic WDI/WiFiCx infrastructure in this skeleton and the chip-
specific backend that actually talks to hardware. A real-HW fork
implements its own version of every op and swaps `ChipFakeGetOps()`
in `device.cpp::MtkInitializeHardware` for `ChipXxxGetOps()`.

## Op-by-op contract

### Lifecycle

| Op       | When called                                | Contract |
|----------|---------------------------------------------|----------|
| `Init`   | `EvtDevicePrepareHardware`                  | Allocate per-chip context, stash in `*OutCtx`. If you return non-success, `Deinit` is **not** called. |
| `Deinit` | `EvtDeviceReleaseHardware` (iff Init succeeded) | Release everything you allocated. |

### Radio toggle

| Op | When called | Contract |
|----|-------------|----------|
| `SetRadio` | `WDI_TASK_SET_RADIO_STATE` | Push the new state to HW (or no-op if you have no real radio). WDI layer already tracks the soft state in `MTK_DEVICE::RadioOn`. |

### Scan

| Op | When called | Contract |
|----|-------------|----------|
| `StartScan` | scan workitem, after `WDI_TASK_SCAN` is dispatched | Push every discovered BSS into the shared cache via `WdiDeviceGetScanCache(WdfDevice)` + `WdiScanCacheInsert(...)`. Synchronous from the workitem's POV — WDI layer drains the cache and emits `BSS_ENTRY_LIST` after you return. A real backend may block on its MCU scan-done event (you're at PASSIVE_LEVEL on a workitem; blocking is fine). |
| `AbortScan` | reserved for future OS-cancelled-scan paths | Optional, may be NULL. |

### Connect / Disconnect

| Op | When called | Contract |
|----|-------------|----------|
| `Connect` | connect workitem | Do your chip-specific connect, **then call `WdiEmitAssociationResult(...)`** with the AssocReq / AssocResp **MMPDU bodies** (see `wdi_contract.md`). Return success. WDI workitem fires `CONNECT_COMPLETE` after you return. |
| `Disconnect` | disconnect workitem | Tear down chip's association. WDI workitem fires `DISCONNECT_COMPLETE` on return. |

**Do NOT** assign into `WDI_ASSOCIATION_RESULT_CONTAINER`'s
`AssociationRequestFrame` / `AssociationResponseFrame` fields by
hand. Always go through `WdiEmitAssociationResult()`. It encodes
the MMPDU-body contract and the mandatory-fields list and
`NT_ASSERT`s the body shape in debug builds.

### Key management

| Op | When called | Contract |
|----|-------------|----------|
| `ProgramKey` | per key in `WDI_SET_ADD_CIPHER_KEYS` | Install / update one key in your HW security CAM. Use `WDI_KEY_DESCRIPTOR`'s flat `Key[32]` / `KeyLength` — the WDI layer has already extracted bytes from whichever cipher-specific Optional was present. |
| `RemoveAllKeys` | `WDI_SET_DELETE_CIPHER_KEYS` | Clear all keys. The WDI delete TLV is per-key in principle, but wlansvc only sends it at disconnect, so the skeleton collapses to "clear all". Extend this op if you need finer granularity. |
| `SetDefaultKeyId` | `WDI_SET_DEFAULT_KEY_ID` | Which keyid future broadcast/multicast TX should use. Backends without HW default-key tracking may no-op. |

### Reset

| Op | When called | Contract |
|----|-------------|----------|
| `Reset` | reset workitem, `WDI_TASK_DOT11_RESET` | Re-initialize HW (FW reload, MAC reset, ring reset). The WDI layer does **not** cache OS-pushed state to replay — wlansvc re-pushes connect/keys/etc. after seeing `RESET_COMPLETE`. If you need to restore *chip-internal* state (cal tables, FW config), handle it here. |

## Threading

- All ops may run at IRQL ≤ DISPATCH_LEVEL on arbitrary threads.
- Workitem-invoked ops (`StartScan`, `Connect`, `Disconnect`,
  `Reset`) run at PASSIVE_LEVEL. Blocking is fine; allocating
  pageable memory is fine.
- `SetRadio`, `ProgramKey`, `RemoveAllKeys`, `SetDefaultKeyId`,
  `Init`, `Deinit` run inline from the WDI dispatcher or PnP path.
  Keep them quick; defer to a workitem if you need to do real I/O.

## PM tunables

`wificx-skl.inx` exposes four registry values that real backends
should honor during `Init`:

- `RuntimePmEnable` (default 0)
- `DeepSleepEnable` (default 0)
- `IdleTimeoutMs` (default 0 = never)
- `AspmL1ssEnable` (default 0)

All defaults are **off**. PM-resume races are the most-reported
class of bug on Linux WiFi drivers (mt7921 resume hangs, rtw89
ASPM L1.2, etc.); start conservative and enable selectively once
the data path is solid. Read them via
`WdfDeviceOpenRegistryKey(PLUGPLAY_REGKEY_DRIVER, ...)` —
`device.cpp` shows the pattern and logs the values.

## Tracing

Use the helpers in `trace.h` from any backend:

- `TraceLoggingWrite(WiFiCxSampleTraceProvider, "EventName", ...)`
  for structured events.
- `WdiFwDebug(fmt, ...)` for FW-debug strings (route your chip's
  FW log stream here).
- `WdiFwDebugBytes(buf, len)` for binary FW dumps.
