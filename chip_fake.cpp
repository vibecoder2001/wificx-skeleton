#include "precomp.h"
#include "chip_fake.h"
#include "fakebss.h"

//
// Fake chip backend. Implements WDI_CHIP_OPS with synthetic behavior
// so the skeleton loads on Hyper-V without real hardware.
//
// Phase 0 wraps the existing fakebss.cpp table behind the new vtable;
// the table itself stays as-is and gets replaced in Phase 2 by a real
// scan-result cache (at which point fakebss.cpp will be renamed to
// chip_fake_bss.cpp — the fake chip's private synthetic BSS source).
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
FakeGetScanResults(_In_opt_ WDI_CHIP_CTX /*Ctx*/,
                   _Out_writes_to_(MaxCount, *OutCount)
                       WDI_BSS_RAW_ENTRY* Entries,
                   _In_ SIZE_T MaxCount,
                   _Out_ SIZE_T* OutCount)
{
    SIZE_T tableCount = 0;
    const FAKE_BSS_ENTRY* table = FakeBssGetTable(&tableCount);

    SIZE_T n = tableCount < MaxCount ? tableCount : MaxCount;
    for (SIZE_T i = 0; i < n; ++i) {
        RtlCopyMemory(Entries[i].Bssid, table[i].Bssid, 6);
        Entries[i].ChannelCenterFrequencyMhz = table[i].ChannelCenterFrequencyMhz;
        Entries[i].RssiDbm                   = table[i].RssiDbm;
        Entries[i].BeaconBuffer              = table[i].BeaconBuffer;
        Entries[i].BeaconLength              = table[i].BeaconLength;
    }
    *OutCount = n;
    return STATUS_SUCCESS;
}

static const WDI_CHIP_OPS g_FakeOps = {
    FakeInit,
    FakeDeinit,
    FakeSetRadio,
    FakeGetScanResults,
};

const WDI_CHIP_OPS*
ChipFakeGetOps(void)
{
    return &g_FakeOps;
}
