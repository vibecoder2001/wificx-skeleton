#include "precomp.h"
#include "wdi_scan_cache.h"

struct _WDI_SCAN_CACHE {
    KSPIN_LOCK           Lock;
    SIZE_T               MaxEntries;
    SIZE_T               Count;
    WDI_SCAN_CACHE_ENTRY Entries[ANYSIZE_ARRAY];
};

NTSTATUS
WdiScanCacheCreate(_In_ SIZE_T MaxEntries, _Outptr_ WDI_SCAN_CACHE** Out)
{
    *Out = NULL;
    if (MaxEntries == 0 || MaxEntries > 4096) {
        return STATUS_INVALID_PARAMETER;
    }
    SIZE_T bytes = FIELD_OFFSET(WDI_SCAN_CACHE, Entries) +
                   MaxEntries * sizeof(WDI_SCAN_CACHE_ENTRY);
    WDI_SCAN_CACHE* c =
        (WDI_SCAN_CACHE*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, 'cSiW');
    if (!c) return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeSpinLock(&c->Lock);
    c->MaxEntries = MaxEntries;
    c->Count = 0;
    *Out = c;
    return STATUS_SUCCESS;
}

void
WdiScanCacheDestroy(_In_opt_ WDI_SCAN_CACHE* Cache)
{
    if (Cache) ExFreePool(Cache);
}

void
WdiScanCacheClear(_In_ WDI_SCAN_CACHE* Cache)
{
    KIRQL irql;
    KeAcquireSpinLock(&Cache->Lock, &irql);
    Cache->Count = 0;
    KeReleaseSpinLock(&Cache->Lock, irql);
}

static SIZE_T
FindByBssidLocked(WDI_SCAN_CACHE* Cache, const UCHAR* Bssid)
{
    for (SIZE_T i = 0; i < Cache->Count; ++i) {
        if (RtlEqualMemory(Cache->Entries[i].Bssid, Bssid, 6)) {
            return i;
        }
    }
    return (SIZE_T)-1;
}

NTSTATUS
WdiScanCacheInsert(
    _In_ WDI_SCAN_CACHE* Cache,
    _In_reads_(6) const UCHAR* Bssid,
    _In_ UINT32 FreqMhz,
    _In_ INT32 Rssi,
    _In_reads_bytes_(BeaconLen) const UCHAR* Beacon,
    _In_ SIZE_T BeaconLen)
{
    SIZE_T copyLen = BeaconLen;
    if (copyLen > WDI_SCAN_CACHE_BEACON_MAX) {
        copyLen = WDI_SCAN_CACHE_BEACON_MAX;
    }

    KIRQL irql;
    KeAcquireSpinLock(&Cache->Lock, &irql);

    SIZE_T idx = FindByBssidLocked(Cache, Bssid);
    if (idx == (SIZE_T)-1) {
        if (Cache->Count >= Cache->MaxEntries) {
            KeReleaseSpinLock(&Cache->Lock, irql);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        idx = Cache->Count++;
        RtlCopyMemory(Cache->Entries[idx].Bssid, Bssid, 6);
    }

    WDI_SCAN_CACHE_ENTRY& e = Cache->Entries[idx];
    e.ChannelCenterFrequencyMhz = FreqMhz;
    e.RssiDbm                   = Rssi;
    KeQueryTickCount(&e.LastSeenTick);
    e.BeaconLength              = copyLen;
    if (copyLen) RtlCopyMemory(e.Beacon, Beacon, copyLen);

    KeReleaseSpinLock(&Cache->Lock, irql);
    return STATUS_SUCCESS;
}

SIZE_T
WdiScanCacheSnapshot(
    _In_ WDI_SCAN_CACHE* Cache,
    _Out_writes_to_(MaxCount, return) WDI_SCAN_CACHE_ENTRY* Out,
    _In_ SIZE_T MaxCount)
{
    KIRQL irql;
    KeAcquireSpinLock(&Cache->Lock, &irql);

    SIZE_T n = Cache->Count < MaxCount ? Cache->Count : MaxCount;
    if (n) RtlCopyMemory(Out, Cache->Entries, n * sizeof(WDI_SCAN_CACHE_ENTRY));

    KeReleaseSpinLock(&Cache->Lock, irql);
    return n;
}
