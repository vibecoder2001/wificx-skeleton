#include "precomp.h"
#include "chip_fake.h"
#include "chip_fake_bss.h"
#include "wdihandlers.h"      // WdiDeviceGetScanCache
#include "wdi_scan_cache.h"
#include "wdi_connect.h"
#include "wdi_frame.h"

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

//
// Build a minimal MMPDU body for a synthesized Assoc Req / Resp.
// The body starts after the 24-byte 802.11 MAC header — so first
// 2 bytes are CapabilityInfo (req) or CapabilityInfo (resp); see
// the strict contract in wdi_mmpdu.h.
//
// Format:
//   AssocReq body  = Capability(2) + ListenInterval(2) + IEs
//   AssocResp body = Capability(2) + StatusCode(2) + AID(2) + IEs
//
// IEs we include: SSID, Supported Rates, DS Param. Enough for the
// MSM to consider the association valid for an Open BSS.
//
static ULONG
BuildFakeAssocReqBody(_Out_writes_(BufLen) UINT8* Buf, ULONG BufLen,
                      const FAKE_BSS_ENTRY* bss)
{
    if (BufLen < 4) return 0;
    UINT8* p = Buf;
    // Capability + Privacy if RSN.
    UINT16 cap = (UINT16)(WDI_CAPAB_ESS |
                          (bss->UseRsn ? WDI_CAPAB_PRIVACY : 0));
    p[0] = (UCHAR)(cap & 0xFF); p[1] = (UCHAR)(cap >> 8);
    p[2] = 0x0a; p[3] = 0x00;        // Listen Interval = 10
    p += 4;
    SIZE_T remaining = BufLen - 4;

    p += WdiIeAppend(p, remaining, WDI_IE_ID_SSID,
                     bss->Ssid, bss->SsidLength);
    remaining = BufLen - (ULONG)(p - Buf);

    static const UCHAR rates[] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    p += WdiIeAppend(p, remaining, WDI_IE_ID_SUPPORTED_RATES,
                     rates, sizeof(rates));
    remaining = BufLen - (ULONG)(p - Buf);

    UCHAR ch = WdiFreqToChannel(bss->ChannelCenterFrequencyMhz);
    p += WdiIeAppend(p, remaining, WDI_IE_ID_DS_PARAMETER_SET, &ch, 1);

    return (ULONG)(p - Buf);
}

static ULONG
BuildFakeAssocRespBody(_Out_writes_(BufLen) UINT8* Buf, ULONG BufLen,
                       const FAKE_BSS_ENTRY* bss, UINT16 aid)
{
    if (BufLen < 6) return 0;
    UINT8* p = Buf;
    UINT16 cap = (UINT16)(WDI_CAPAB_ESS |
                          (bss->UseRsn ? WDI_CAPAB_PRIVACY : 0));
    p[0] = (UCHAR)(cap & 0xFF); p[1] = (UCHAR)(cap >> 8);
    p[2] = 0x00; p[3] = 0x00;        // StatusCode = SUCCESS
    p[4] = (UCHAR)(aid & 0xFF);
    p[5] = (UCHAR)((aid >> 8) | 0xC0);   // AID + the two MSB AID bits the AP sets
    p += 6;
    SIZE_T remaining = BufLen - 6;

    static const UCHAR rates[] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    p += WdiIeAppend(p, remaining, WDI_IE_ID_SUPPORTED_RATES,
                     rates, sizeof(rates));
    remaining = BufLen - (ULONG)(p - Buf);

    UCHAR ch = WdiFreqToChannel(bss->ChannelCenterFrequencyMhz);
    p += WdiIeAppend(p, remaining, WDI_IE_ID_DS_PARAMETER_SET, &ch, 1);

    return (ULONG)(p - Buf);
}

static const FAKE_BSS_ENTRY*
FindFakeBssByBssid(const UCHAR* Bssid)
{
    SIZE_T n = 0;
    const FAKE_BSS_ENTRY* t = FakeBssGetTable(&n);
    for (SIZE_T i = 0; i < n; ++i) {
        if (RtlEqualMemory(t[i].Bssid, Bssid, 6)) return &t[i];
    }
    return NULL;
}

static NTSTATUS
FakeConnect(_In_ WDFDEVICE WdfDevice, _In_opt_ WDI_CHIP_CTX /*Ctx*/,
            _In_ const WDI_CONNECT_TARGET* Target)
{
    // Identify which of our synthetic BSSes the OS asked for. If
    // it picked a BSSID we don't know, refuse — keeps the fake
    // backend honest and gives wlansvc a clean failure path.
    const FAKE_BSS_ENTRY* bss = FindFakeBssByBssid(Target->Bssid);
    if (!bss) return STATUS_NOT_FOUND;

    UINT8 reqBody[128];
    UINT8 respBody[128];
    ULONG reqLen  = BuildFakeAssocReqBody(reqBody,  sizeof(reqBody),  bss);
    ULONG respLen = BuildFakeAssocRespBody(respBody, sizeof(respBody), bss,
                                           /*aid*/ 1);
    if (!reqLen || !respLen) return STATUS_INSUFFICIENT_RESOURCES;

    WdiEmitAssociationResult(WdfDevice,
                             bss->Bssid,
                             reqBody,  reqLen,
                             respBody, respLen,
                             /*StatusCode*/ 0);
    return STATUS_SUCCESS;
}

static NTSTATUS
FakeDisconnect(_In_ WDFDEVICE /*WdfDevice*/, _In_opt_ WDI_CHIP_CTX /*Ctx*/)
{
    return STATUS_SUCCESS;
}

static const WDI_CHIP_OPS g_FakeOps = {
    FakeInit,
    FakeDeinit,
    FakeSetRadio,
    FakeStartScan,
    FakeAbortScan,
    FakeConnect,
    FakeDisconnect,
};

const WDI_CHIP_OPS*
ChipFakeGetOps(void)
{
    return &g_FakeOps;
}
