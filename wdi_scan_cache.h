#pragma once

//
// WDI_SCAN_CACHE — a small BSSID-keyed table that the chip backend
// fills as scan results arrive and the WDI layer snapshots when it's
// time to emit a WDI_INDICATION_BSS_ENTRY_LIST.
//
// Lifetime: created in MtkInitializeHardware, destroyed in release-
// hardware. Insert/Clear/Snapshot are safe to call concurrently
// (KSPIN_LOCK inside).
//
// Inline beacon storage: each entry owns its captured beacon bytes
// inline, capped at WDI_SCAN_CACHE_BEACON_MAX. That keeps the cache
// allocation-free on the insert path and snapshot result self-
// contained (callers can stack-allocate the output array and pass
// pointers from it straight into WDI_BSS_ENTRY_CONTAINER without
// worrying about lifetime).
//

#define WDI_SCAN_CACHE_BEACON_MAX 256

typedef struct _WDI_SCAN_CACHE_ENTRY {
    UCHAR         Bssid[6];
    UCHAR         _pad[2];
    UINT32        ChannelCenterFrequencyMhz;
    INT32         RssiDbm;
    LARGE_INTEGER LastSeenTick;          // KeQueryTickCount at insert
    SIZE_T        BeaconLength;
    UCHAR         Beacon[WDI_SCAN_CACHE_BEACON_MAX];
} WDI_SCAN_CACHE_ENTRY;

typedef struct _WDI_SCAN_CACHE WDI_SCAN_CACHE;

NTSTATUS
WdiScanCacheCreate(_In_ SIZE_T MaxEntries, _Outptr_ WDI_SCAN_CACHE** Out);

void
WdiScanCacheDestroy(_In_opt_ WDI_SCAN_CACHE* Cache);

void
WdiScanCacheClear(_In_ WDI_SCAN_CACHE* Cache);

// Merge by BSSID. If an entry with the same BSSID already exists it
// is overwritten in place (newer-beacon-wins). New entries are added
// until the table is full; further inserts then drop silently. A
// future revision can add an LRU eviction policy when real drivers
// need it.
//
// Beacons larger than WDI_SCAN_CACHE_BEACON_MAX are truncated.
NTSTATUS
WdiScanCacheInsert(
    _In_ WDI_SCAN_CACHE* Cache,
    _In_reads_(6) const UCHAR* Bssid,
    _In_ UINT32 FreqMhz,
    _In_ INT32 Rssi,
    _In_reads_bytes_(BeaconLen) const UCHAR* Beacon,
    _In_ SIZE_T BeaconLen);

// Copy up to MaxCount entries out of the cache (no removal — the
// cache persists). Returns the count actually written. Each output
// entry is fully self-contained (inline beacon bytes).
SIZE_T
WdiScanCacheSnapshot(
    _In_ WDI_SCAN_CACHE* Cache,
    _Out_writes_to_(MaxCount, return) WDI_SCAN_CACHE_ENTRY* Out,
    _In_ SIZE_T MaxCount);
