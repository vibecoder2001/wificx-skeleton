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

// Forward decl — full definition in wdi_connect.h. Avoids a circular
// include: wdi_connect.h needs WDI_CHIP_OPS for backend declarations
// in skeleton consumers, and the chip op signature needs the target.
struct _WDI_CONNECT_TARGET;

// Forward decl — full definition in wdi_keys.h.
struct _WDI_KEY_DESCRIPTOR;

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
    // Scan.
    //
    // StartScan runs on a workitem at PASSIVE_LEVEL. The backend
    // must push every discovered BSS into the shared scan cache
    // (WdiDeviceGetScanCache(WdfDevice) + WdiScanCacheInsert), then
    // return. The WDI layer drains the cache afterwards to emit
    // WDI_INDICATION_BSS_ENTRY_LIST and WDI_INDICATION_SCAN_COMPLETE.
    //
    // The fake backend performs StartScan synchronously by pushing
    // a few synthetic entries. A real backend would issue its MCU
    // scan command and either block until completion (legal here —
    // we're on a workitem) or queue completion through a separate
    // event and have StartScan block on it.
    //
    // AbortScan is optional and may be NULL — provided for future
    // OS-cancelled-scan paths.
    //
    NTSTATUS (*StartScan)(_In_ WDFDEVICE WdfDevice, _In_opt_ WDI_CHIP_CTX Ctx);
    NTSTATUS (*AbortScan)(_In_ WDFDEVICE WdfDevice, _In_opt_ WDI_CHIP_CTX Ctx);

    //
    // Connect / Disconnect.
    //
    // Connect runs on the connect workitem at PASSIVE_LEVEL. The
    // backend should:
    //   1. Do its chip-specific work (auth + assoc on air, or a
    //      synthetic success for the fake backend).
    //   2. Emit WDI_INDICATION_ASSOCIATION_RESULT via
    //      WdiEmitAssociationResult(), supplying the AssocReq and
    //      AssocResp MMPDU bodies. See wdi_mmpdu.h for the strict
    //      body-vs-frame contract — getting it wrong silently
    //      breaks RSNA. Use WdiEmitAssociationResult and never
    //      assign Association{Req,Resp}Frame fields by hand.
    //   3. Return STATUS_SUCCESS. The WDI workitem will fire
    //      WDI_INDICATION_CONNECT_COMPLETE after this returns.
    //
    // On failure, return non-success; the workitem emits
    // CONNECT_COMPLETE with a failure status.
    //
    // Disconnect: tear down the chip's association. The WDI
    // workitem fires DISCONNECT_COMPLETE on return.
    //
    NTSTATUS (*Connect)(_In_ WDFDEVICE WdfDevice,
                        _In_opt_ WDI_CHIP_CTX Ctx,
                        _In_ const struct _WDI_CONNECT_TARGET* Target);
    NTSTATUS (*Disconnect)(_In_ WDFDEVICE WdfDevice,
                           _In_opt_ WDI_CHIP_CTX Ctx);

    //
    // Key management. The WDI layer parses the WDI_SET_ADD_CIPHER_
    // KEYS / DELETE_CIPHER_KEYS / SET_DEFAULT_KEY_ID TLVs and hands
    // normalized data to these ops.
    //
    // ProgramKey: install / update one key. Called for each entry in
    // an ADD_CIPHER_KEYS batch. Real backends drive the HW security-
    // CAM (or equivalent) here; the fake backend just records.
    //
    // RemoveAllKeys: clear every key the backend is holding. The
    // WDI DELETE_CIPHER_KEYS TLV can be per-key in principle, but
    // wlansvc only sends it at disconnect — collapse to "clear all".
    // Backends that need finer granularity can extend the surface.
    //
    // SetDefaultKeyId: which keyid future broadcast/multicast TX
    // uses. Backends without HW default-key tracking may no-op.
    //
    NTSTATUS (*ProgramKey)(_In_ WDFDEVICE WdfDevice,
                           _In_opt_ WDI_CHIP_CTX Ctx,
                           _In_ const struct _WDI_KEY_DESCRIPTOR* Key);
    NTSTATUS (*RemoveAllKeys)(_In_ WDFDEVICE WdfDevice,
                              _In_opt_ WDI_CHIP_CTX Ctx);
    NTSTATUS (*SetDefaultKeyId)(_In_ WDFDEVICE WdfDevice,
                                _In_opt_ WDI_CHIP_CTX Ctx,
                                _In_ UCHAR KeyIndex);

    //
    // Reset / recovery.
    //
    // Called from the DOT11_RESET workitem before the WDI layer
    // emits DOT11_RESET_COMPLETE. Real backends re-initialize their
    // chip (FW reload, MAC reset, ring reset). On return success the
    // WDI layer fires RESET_COMPLETE and the OS treats the adapter
    // as freshly available; wlansvc will re-issue any state it
    // expects to persist (connect, keys, etc.).
    //
    // The skeleton intentionally does NOT cache OS-pushed state to
    // re-apply across resets — wlansvc re-pushes everything once it
    // sees RESET_COMPLETE. Real backends that need to restore *chip-
    // internal* state (cal tables, FW config) handle it here.
    //
    NTSTATUS (*Reset)(_In_ WDFDEVICE WdfDevice,
                      _In_opt_ WDI_CHIP_CTX Ctx);
} WDI_CHIP_OPS;
