#pragma once

//
// 802.11 frame and IE parsing primitives. Chip-agnostic; pure
// transforms over byte buffers. No driver state, no allocations.
//
// Add new helpers here as later phases need them (RSN IE parsing,
// 802.11 MAC-header decoding, etc.) — keep this header focused on
// what's actually consumed; speculative helpers rot.
//

// ---------- Channel / band ----------

// Convert an 802.11 center frequency in MHz to a channel number.
// 2.4 GHz: 1..14; 5 GHz: standard 5GHz mapping; 6 GHz: standard
// 6GHz mapping (channel 1 at 5955 MHz).
UCHAR
WdiFreqToChannel(_In_ UINT32 FreqMhz);

// Convert an 802.11 center frequency in MHz to the WDI band id.
WDI_BAND_ID
WdiFreqToBand(_In_ UINT32 FreqMhz);

// ---------- IEEE 802.11 IE walker ----------

// IE IDs we name (full set in 802.11-2020 §9.4.2). Add as needed.
#define WDI_IE_ID_SSID                  0
#define WDI_IE_ID_SUPPORTED_RATES       1
#define WDI_IE_ID_DS_PARAMETER_SET      3
#define WDI_IE_ID_RSN                   48
#define WDI_IE_ID_EXTENDED_SUPP_RATES   50

// Append one IE (id, length, payload) to a buffer. Returns bytes
// written (2 + Len) or 0 if the IE doesn't fit. Caller advances
// its own offset.
SIZE_T
WdiIeAppend(
    _Out_writes_to_(Remaining, return) UCHAR* Dst,
    _In_ SIZE_T Remaining,
    _In_ UCHAR Id,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ UCHAR Len);

// Find the first IE with the given id in an IE byte stream.
// Returns the IE's *payload* pointer (past the 2-byte id/len) and
// length via *OutLen. Returns NULL and *OutLen = 0 if absent or
// the stream is malformed.
const UCHAR*
WdiIeFind(
    _In_reads_bytes_(BufLen) const UCHAR* Buf,
    _In_ SIZE_T BufLen,
    _In_ UCHAR Id,
    _Out_ SIZE_T* OutLen);

// ---------- 802.11 beacon body layout ----------
//
// Fixed-parameter prefix on a Beacon (and Probe Response) frame
// body: 8B Timestamp + 2B BeaconInterval + 2B CapabilityInfo,
// then variable IEs.
//
#define WDI_BEACON_FIXED_LEN            12
#define WDI_BEACON_OFF_TIMESTAMP        0
#define WDI_BEACON_OFF_BEACON_INTERVAL  8
#define WDI_BEACON_OFF_CAPABILITY       10

// Capability info bits (802.11-2020 §9.4.1.4)
#define WDI_CAPAB_ESS                   0x0001
#define WDI_CAPAB_PRIVACY               0x0010

// ---------- MAC address helpers ----------

FORCEINLINE BOOLEAN
WdiMacIsZero(_In_reads_(6) const UCHAR* Mac)
{
    return (Mac[0] | Mac[1] | Mac[2] | Mac[3] | Mac[4] | Mac[5]) == 0;
}

FORCEINLINE BOOLEAN
WdiMacIsMulticast(_In_reads_(6) const UCHAR* Mac)
{
    return (Mac[0] & 0x01) != 0;
}

FORCEINLINE BOOLEAN
WdiMacIsBroadcast(_In_reads_(6) const UCHAR* Mac)
{
    return Mac[0] == 0xFF && Mac[1] == 0xFF && Mac[2] == 0xFF &&
           Mac[3] == 0xFF && Mac[4] == 0xFF && Mac[5] == 0xFF;
}

FORCEINLINE BOOLEAN
WdiMacEqual(_In_reads_(6) const UCHAR* A, _In_reads_(6) const UCHAR* B)
{
    return RtlEqualMemory(A, B, 6);
}
