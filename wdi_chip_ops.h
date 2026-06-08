#pragma once

//
// WDI_CHIP_OPS — the contract between the WDI/WiFiCx infrastructure
// in this skeleton (handlers, indication framing, state machines) and
// the chip-specific backend that actually talks to hardware.
//
// The skeleton ships a fake backend (chip_fake.cpp) that synthesizes
// behavior good enough to load on Hyper-V and exercise wlansvc.
// A real-HW fork replaces the vtable installation in
// MtkInitializeHardware with its own ChipXxxGetOps().
//
// Threading: callbacks may run at IRQL <= DISPATCH_LEVEL on arbitrary
// threads. Long-running work belongs on a workitem; the WDI handlers
// already use that pattern for SCAN/RESET/SET_RADIO_STATE completion.
//
// Lifetime: Init runs from EvtDevicePrepareHardware; Deinit from
// EvtDeviceReleaseHardware. All other ops run between those two.
//

// Opaque per-chip context. The fake backend uses NULL; a real chip
// would stash its dev/MCU/H2C handle here.
typedef void* WDI_CHIP_CTX;

// Raw scan-result entry handed from the chip backend to the WDI BSS-
// indication path. Phase 0 keeps this minimal — Phase 2 replaces the
// caller's static array with a scan-result cache and may grow this
// struct (probe-response merge, age, beacon TSF).
typedef struct _WDI_BSS_RAW_ENTRY {
    UCHAR  Bssid[6];
    UINT32 ChannelCenterFrequencyMhz;
    INT32  RssiDbm;
    const UINT8* BeaconBuffer;   // 802.11 mgmt body (post-header)
    SIZE_T BeaconLength;
} WDI_BSS_RAW_ENTRY;

typedef struct _WDI_CHIP_OPS {
    //
    // Lifecycle.
    //

    // Init: called once from EvtDevicePrepareHardware. The backend
    // should allocate its per-chip context here and return it via
    // OutCtx. If it fails, Deinit is NOT called.
    NTSTATUS (*Init)(_In_ WDFDEVICE WdfDevice, _Out_ WDI_CHIP_CTX* OutCtx);

    // Deinit: called once from EvtDeviceReleaseHardware iff Init
    // succeeded. The backend must release everything it allocated.
    void     (*Deinit)(_In_ WDFDEVICE WdfDevice, _In_opt_ WDI_CHIP_CTX Ctx);

    //
    // Radio toggle. The WDI layer already tracks the software state
    // in MTK_DEVICE::RadioOn — this op only needs to push the change
    // down to hardware (or no-op for the fake backend).
    //
    NTSTATUS (*SetRadio)(_In_opt_ WDI_CHIP_CTX Ctx, _In_ BOOLEAN SoftOn);

    //
    // Scan: fill the caller's array with up to MaxCount BSS entries.
    // Returns the count actually written via *OutCount. Returning
    // STATUS_SUCCESS with *OutCount == 0 is valid and means "scan
    // completed but no BSSes were found".
    //
    // Phase-0 contract: synchronous, returns a synthetic list every
    // call. Phase 2 will replace this surface with an explicit
    // StartScan(channels, ssids) + AbortScan() + a result cache that
    // the backend feeds asynchronously.
    //
    NTSTATUS (*GetScanResults)(_In_opt_ WDI_CHIP_CTX Ctx,
                               _Out_writes_to_(MaxCount, *OutCount)
                                   WDI_BSS_RAW_ENTRY* Entries,
                               _In_ SIZE_T MaxCount,
                               _Out_ SIZE_T* OutCount);
} WDI_CHIP_OPS;
