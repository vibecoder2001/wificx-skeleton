#include "precomp.h"
#include "wdi_frame.h"

//
// Pure 802.11 frame/IE utilities. No driver state — these can be
// called from any IRQL, on any thread.
//

UCHAR
WdiFreqToChannel(_In_ UINT32 FreqMhz)
{
    // 2.4 GHz: ch 1..13 at 2412 + 5*(ch-1); ch 14 at 2484.
    if (FreqMhz == 2484) return 14;
    if (FreqMhz >= 2412 && FreqMhz <= 2472) {
        return (UCHAR)((FreqMhz - 2407) / 5);
    }
    // 6 GHz: ch 1 at 5955 MHz, step 5 MHz, up to ch 233 (7115 MHz).
    if (FreqMhz >= 5955 && FreqMhz <= 7115) {
        return (UCHAR)((FreqMhz - 5950) / 5);
    }
    // 5 GHz: ch n at 5000 + 5*n. Covers UNII-1..UNII-3 / DFS bands.
    if (FreqMhz >= 5000 && FreqMhz <= 5895) {
        return (UCHAR)((FreqMhz - 5000) / 5);
    }
    return 0;
}

WDI_BAND_ID
WdiFreqToBand(_In_ UINT32 FreqMhz)
{
    if (FreqMhz < 4000) return WDI_BAND_ID_2400;
    // The WDI band-id enum doesn't currently distinguish 6 GHz from
    // 5 GHz in this skeleton's build of wificx.h. Treat anything
    // >= 4 GHz as 5000 for now; revisit when 6 GHz support lands.
    return WDI_BAND_ID_5000;
}

SIZE_T
WdiIeAppend(
    _Out_writes_to_(Remaining, return) UCHAR* Dst,
    _In_ SIZE_T Remaining,
    _In_ UCHAR Id,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ UCHAR Len)
{
    if (Remaining < (SIZE_T)(2 + Len)) return 0;
    Dst[0] = Id;
    Dst[1] = Len;
    if (Len && Data) {
        RtlCopyMemory(Dst + 2, Data, Len);
    }
    return (SIZE_T)(2 + Len);
}

const UCHAR*
WdiIeFind(
    _In_reads_bytes_(BufLen) const UCHAR* Buf,
    _In_ SIZE_T BufLen,
    _In_ UCHAR Id,
    _Out_ SIZE_T* OutLen)
{
    *OutLen = 0;
    SIZE_T off = 0;
    while (off + 2 <= BufLen) {
        UCHAR ieId  = Buf[off];
        UCHAR ieLen = Buf[off + 1];
        if (off + 2 + ieLen > BufLen) {
            // Truncated IE — bail out rather than walk off the end.
            return NULL;
        }
        if (ieId == Id) {
            *OutLen = ieLen;
            return Buf + off + 2;
        }
        off += (SIZE_T)2 + ieLen;
    }
    return NULL;
}
