#include "precomp.h"
#include "chip_fake.h"
#include "chip_fake_bss.h"
#include "wdihandlers.h"      // WdiDeviceGetScanCache
#include "wdi_scan_cache.h"

//
// Fake chip backend. Implements WDI_CHIP_OPS with synthetic behavior
// so the skeleton loads on Hyper-V without real hardware.
//
// Phase 2: StartScan clears the scan cache and pushes the static
// fakebss table into it. The WDI layer drains the cache afterwards
// to emit BSS_ENTRY_LIST + SCAN_COMPLETE, exactly like a real
// backend would after firmware-driven scan results.
//

static NTSTATUS
FakeInit(_In_ WDFDEVICE /*WdfDevice*/, _Out_ WDI_CHIP_CTX* OutCtx)
{
    // No per-chip state needed yet. Future fake-backend features
    // (synthetic association state, fake key table) will allocate
    // here and return a real pointer.
    *OutCtx = NULL;
    return STATUS_SUCCESS;
}

static void
FakeDeinit(_In_ WDFDEVICE /*WdfDevice*/, _In_opt_ WDI_CHIP_CTX /*Ctx*/)
{
}

static NTSTATUS
FakeSetRadio(_In_opt_ WDI_CHIP_CTX /*Ctx*/, _In_ BOOLEAN /*SoftOn*/)
{
    // The fake backend has no radio to gate — the WDI layer's
    // RadioOn flag is the entire truth.
    return STATUS_SUCCESS;
}

static NTSTATUS
FakeStartScan(_In_ WDFDEVICE WdfDevice, _In_opt_ WDI_CHIP_CTX /*Ctx*/)
{
    WDI_SCAN_CACHE* cache = WdiDeviceGetScanCache(WdfDevice);
    if (!cache) return STATUS_DEVICE_NOT_READY;

    // Fresh scan: rebuild the cache from scratch each time. A real
    // backend may instead accumulate with eviction policy.
    WdiScanCacheClear(cache);

    SIZE_T tableCount = 0;
    const FAKE_BSS_ENTRY* table = FakeBssGetTable(&tableCount);
    for (SIZE_T i = 0; i < tableCount; ++i) {
        (void)WdiScanCacheInsert(
            cache,
            table[i].Bssid,
            table[i].ChannelCenterFrequencyMhz,
            table[i].RssiDbm,
            table[i].BeaconBuffer,
            table[i].BeaconLength);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
FakeAbortScan(_In_ WDFDEVICE /*WdfDevice*/, _In_opt_ WDI_CHIP_CTX /*Ctx*/)
{
    // Fake scan is synchronous; there's nothing to cancel.
    return STATUS_SUCCESS;
}

static const WDI_CHIP_OPS g_FakeOps = {
    FakeInit,
    FakeDeinit,
    FakeSetRadio,
    FakeStartScan,
    FakeAbortScan,
};

const WDI_CHIP_OPS*
ChipFakeGetOps(void)
{
    return &g_FakeOps;
}
