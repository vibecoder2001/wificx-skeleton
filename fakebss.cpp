#include "precomp.h"
#include "fakebss.h"

static FAKE_BSS_ENTRY g_FakeBss[FAKE_BSS_COUNT] = {
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x01 },
      8, { 'F','a','k','e','O','p','e','n' },
      2437, -45, 100, 0x0001, FALSE, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x02 },
      8, { 'F','a','k','e','W','P','A','2' },
      2462, -60, 100, 0x0011, TRUE, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x03 },
      9, { 'F','a','k','e','G','u','e','s','t' },
      5180, -55, 100, 0x0001, FALSE, { 0 }, 0 },
    { { 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x04 },
      8, { 'F','a','k','e','H','o','m','e' },
      5220, -70, 100, 0x0011, TRUE, { 0 }, 0 },
};
static BOOLEAN g_IesBuilt = FALSE;

static SIZE_T
AppendIe(_Out_writes_bytes_(remaining) UCHAR* dst, SIZE_T remaining,
         UCHAR id, const UCHAR* data, UCHAR len)
{
    if (remaining < (SIZE_T)(2 + len)) return 0;
    dst[0] = id;
    dst[1] = len;
    if (len) RtlCopyMemory(dst + 2, data, len);
    return 2 + len;
}

static void
FakeBssBuildIes(FAKE_BSS_ENTRY* e)
{
    SIZE_T off = 0;

    // SSID IE (id 0)
    off += AppendIe(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                    0, e->Ssid, e->SsidLength);

    // Supported Rates IE (id 1): 1/2/5.5/11/6/9/12/18 Mbps, basic bit on first four.
    static const UCHAR rates[] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    off += AppendIe(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                    1, rates, sizeof(rates));

    // DS Parameter Set IE (id 3): channel number derived from frequency.
    UCHAR ch;
    if (e->ChannelCenterFrequencyMhz <= 2484) {
        ch = (UCHAR)((e->ChannelCenterFrequencyMhz - 2407) / 5);
    } else {
        ch = (UCHAR)((e->ChannelCenterFrequencyMhz - 5000) / 5);
    }
    off += AppendIe(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                    3, &ch, 1);

    // RSN IE (id 48): WPA2-PSK / CCMP.
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
        off += AppendIe(e->IeBuffer + off, FAKE_BSS_MAX_IE_SIZE - off,
                        48, rsn, sizeof(rsn));
    }

    e->IeLength = off;
}

const FAKE_BSS_ENTRY*
FakeBssGetTable(_Out_ SIZE_T* count)
{
    if (!g_IesBuilt) {
        for (SIZE_T i = 0; i < FAKE_BSS_COUNT; ++i) {
            FakeBssBuildIes(&g_FakeBss[i]);
        }
        g_IesBuilt = TRUE;
    }
    *count = FAKE_BSS_COUNT;
    return g_FakeBss;
}
