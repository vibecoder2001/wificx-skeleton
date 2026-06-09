#include "precomp.h"
#include "fakebss.h"
#include "wdi_frame.h"

static FAKE_BSS_ENTRY g_FakeBss[FAKE_BSS_COUNT] = {
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x01 },
      8, { 'F','a','k','e','O','p','e','n' },
      2437, -45, 100, 0x0001, FALSE, { 0 }, 0, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x02 },
      8, { 'F','a','k','e','W','P','A','2' },
      2462, -60, 100, 0x0011, TRUE, { 0 }, 0, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x03 },
      9, { 'F','a','k','e','G','u','e','s','t' },
      5180, -55, 100, 0x0001, FALSE, { 0 }, 0, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x04 },
      8, { 'F','a','k','e','H','o','m','e' },
      5220, -70, 100, 0x0011, TRUE, { 0 }, 0, { 0 }, 0 },
};
static BOOLEAN g_IesBuilt = FALSE;

static void
FakeBssBuildIes(FAKE_BSS_ENTRY* e)
{
    SIZE_T off = 0;

    off += WdiIeAppend(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                       WDI_IE_ID_SSID, e->Ssid, e->SsidLength);

    // Supported Rates: 1/2/5.5/11/6/9/12/18 Mbps, basic bit on first four.
    static const UCHAR rates[] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    off += WdiIeAppend(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                       WDI_IE_ID_SUPPORTED_RATES, rates, sizeof(rates));

    UCHAR ch = WdiFreqToChannel(e->ChannelCenterFrequencyMhz);
    off += WdiIeAppend(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                       WDI_IE_ID_DS_PARAMETER_SET, &ch, 1);

    if (e->UseRsn) {
        static const UCHAR rsn[] = {
            0x01, 0x00,                   // RSN version 1
            0x00, 0x0F, 0xAC, 0x04,       // Group cipher: CCMP
            0x01, 0x00,                   // Pairwise count
            0x00, 0x0F, 0xAC, 0x04,       // Pairwise: CCMP
            0x01, 0x00,                   // AKM count
            0x00, 0x0F, 0xAC, 0x02,       // AKM: PSK
            0x00, 0x00                    // RSN capabilities
        };
        off += WdiIeAppend(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                           WDI_IE_ID_RSN, rsn, sizeof(rsn));
    }

    e->IeLength = off;
}

static void
FakeBssBuildBeaconBody(FAKE_BSS_ENTRY* e)
{
    // 802.11 beacon frame body: 8B Timestamp + 2B BeaconInterval + 2B
    // CapabilityInfo, then the variable IE section.
    UCHAR* p = e->BeaconBuffer;
    RtlZeroMemory(p + WDI_BEACON_OFF_TIMESTAMP, 8);
    p[WDI_BEACON_OFF_BEACON_INTERVAL]     = (UCHAR)(e->BeaconIntervalTU & 0xff);
    p[WDI_BEACON_OFF_BEACON_INTERVAL + 1] = (UCHAR)((e->BeaconIntervalTU >> 8) & 0xff);
    p[WDI_BEACON_OFF_CAPABILITY]          = (UCHAR)(e->CapabilityInfo & 0xff);
    p[WDI_BEACON_OFF_CAPABILITY + 1]      = (UCHAR)((e->CapabilityInfo >> 8) & 0xff);
    RtlCopyMemory(p + WDI_BEACON_FIXED_LEN, e->IeBuffer, e->IeLength);
    e->BeaconLength = WDI_BEACON_FIXED_LEN + e->IeLength;
}

const FAKE_BSS_ENTRY*
FakeBssGetTable(_Out_ SIZE_T* count)
{
    if (!g_IesBuilt) {
        for (SIZE_T i = 0; i < FAKE_BSS_COUNT; ++i) {
            FakeBssBuildIes(&g_FakeBss[i]);
            FakeBssBuildBeaconBody(&g_FakeBss[i]);
        }
        g_IesBuilt = TRUE;
    }
    *count = FAKE_BSS_COUNT;
    return g_FakeBss;
}
