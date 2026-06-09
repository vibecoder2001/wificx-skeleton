#include "precomp.h"
#include <wlan/2.0/TlvGeneratorParser.hpp>
#include "trace.h"
#include "device.h"
#include "adapter.h"
#include "wdihandlers.h"
#include "wdi_frame.h"
#include "wdi_scan_cache.h"
#include "wdi_tlv.h"

// The TLV generator (WificxTLVGenParse.lib) and its ArrayOfElements<T>
// templates call standard C++ new/delete. Kernel mode has no CRT, so
// route them all to ExAllocatePool2 / ExFreePool with our pool tag.
void* __cdecl operator new(size_t sz) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, sz ? sz : 1, 'IFiW');
}
void* __cdecl operator new[](size_t sz) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, sz ? sz : 1, 'IFiW');
}
void* __cdecl operator new(size_t sz, size_t /*tag*/) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, sz ? sz : 1, 'IFiW');
}
void __cdecl operator delete(void* p) noexcept {
    if (p) ExFreePool(p);
}
void __cdecl operator delete(void* p, size_t) noexcept {
    if (p) ExFreePool(p);
}
void __cdecl operator delete[](void* p) noexcept {
    if (p) ExFreePool(p);
}
void __cdecl operator delete[](void* p, size_t) noexcept {
    if (p) ExFreePool(p);
}

// WDI OID constants come from dot11wdi.h (pulled in via wificx.h).
// Async results (scan complete, BSS entry list) are returned via
// WifiDeviceReceiveIndication using WDI_INDICATION_* message IDs,
// not via per-OID output buffers.

static NTSTATUS HandleGetAdapterCaps(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                     UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleTaskOpen(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                               UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleTaskClose(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleSetAdapterConfig(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                       UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleSetRadioState(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                    UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleCreatePort(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                 UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleDeletePort(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                 UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleStartScan(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleDot11Reset(WDFDEVICE WdfDevice, WIFIREQUEST req,
                                 PWDI_MESSAGE_HEADER hdr, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleGetStatistics(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                    UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleSetLocationPrivacy(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                         UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleTaskConnect(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                  UINT inLen, UINT outLen, _Out_ UINT* bytesWritten);
static NTSTATUS HandleTaskDisconnect(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                     UINT outLen, _Out_ UINT* bytesWritten);

void
WdiDispatchCommand(
    _In_ WDFDEVICE WdfDevice,
    _In_ WIFIREQUEST Request)
{
    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);
    UINT16 messageId = WifiRequestGetMessageId(Request);
    UINT inLen = 0, outLen = 0;
    PWDI_MESSAGE_HEADER hdr =
        (PWDI_MESSAGE_HEADER)WifiRequestGetInOutBuffer(Request, &inLen, &outLen);

    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    UINT bytesWritten = 0;

    // NOTE: WifiRequestGetMessageId returns the raw 16-bit message ID,
    // NOT the full OID (which has the WDI_OID_PREFIX 0x0e440000 OR'd in).
    // Switch on the bare WDI_* IDs from dot11wdi.h, not OID_WDI_*.
    switch (messageId)
    {
    case WDI_GET_ADAPTER_CAPABILITIES:
        status = HandleGetAdapterCaps(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_OPEN:
        status = HandleTaskOpen(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_CLOSE:
        status = HandleTaskClose(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_SET_ADAPTER_CONFIGURATION:
        status = HandleSetAdapterConfig(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_SET_RADIO_STATE:
        status = HandleSetRadioState(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_CREATE_PORT:
        status = HandleCreatePort(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_DELETE_PORT:
        status = HandleDeletePort(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_SCAN:
        status = HandleStartScan(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_DOT11_RESET:
        status = HandleDot11Reset(WdfDevice, Request, hdr, outLen, &bytesWritten); break;
    case WDI_GET_STATISTICS:
        status = HandleGetStatistics(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_SET_LOCATION_PRIVACY:
        status = HandleSetLocationPrivacy(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_CONNECT:
        status = HandleTaskConnect(dev, Request, hdr, inLen, outLen, &bytesWritten); break;
    case WDI_TASK_DISCONNECT:
        status = HandleTaskDisconnect(dev, Request, hdr, outLen, &bytesWritten); break;
    default:
        DbgPrint("WDI: unhandled MessageID 0x%x (in=%u out=%u)\n", messageId, inLen, outLen);
        break;
    }

    if (status != STATUS_PENDING) {
        WifiRequestComplete(Request, status, bytesWritten);
    }
}

// ---------- simple bookkeeping handlers ----------

// Every WDI response must start with a WDI_MESSAGE_HEADER echoed back
// to the OS. Returning 0 bytes triggers EVENT_NDIS_HARDWARE_FAILURE.
static UINT
WriteWdiResponseHeader(WIFIREQUEST req, PWDI_MESSAGE_HEADER in, UINT outLen)
{
    if (outLen < sizeof(WDI_MESSAGE_HEADER)) return 0;
    PWDI_MESSAGE_HEADER out =
        (PWDI_MESSAGE_HEADER)WifiRequestGetInOutBuffer(req, NULL, NULL);
    out->PortId = in->PortId;
    out->Reserved = 0;
    out->Status = STATUS_SUCCESS;
    out->TransactionId = in->TransactionId;
    out->IhvSpecificId = 0;
    return sizeof(WDI_MESSAGE_HEADER);
}

static NTSTATUS HandleTaskOpen(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                               UINT, UINT outLen, UINT* bw) {
    dev->AdapterOpened = TRUE;
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS HandleTaskClose(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                UINT, UINT outLen, UINT* bw) {
    dev->AdapterOpened = FALSE;
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS HandleSetAdapterConfig(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                       UINT, UINT outLen, UINT* bw) {
    // Matches the NDIS6 WDI sample's WdiSimpleSetProperty: header echo.
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS HandleSetRadioState(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                    UINT inLen, UINT outLen, UINT* bw) {
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "SetRadioStateEntry",
        TraceLoggingUInt32(inLen, "inLen"),
        TraceLoggingUInt16(hdr->PortId, "PortId"),
        TraceLoggingUInt32(hdr->TransactionId, "Txn"));
    BOOLEAN softOn = TRUE;
    if (inLen > sizeof(WDI_MESSAGE_HEADER)) {
        WDI_SET_RADIO_STATE_PARAMETERS params;
        TLV_CONTEXT ctx = { 0, dev->OsWdiVersion ? dev->OsWdiVersion : WDI_VERSION_LATEST };
        const UINT8* tlv = (const UINT8*)hdr + sizeof(WDI_MESSAGE_HEADER);
        ULONG tlvLen = inLen - sizeof(WDI_MESSAGE_HEADER);
        NDIS_STATUS ps = ParseWdiTaskSetRadioStateToIhv(tlvLen, tlv, &ctx, &params);
        if (ps == NDIS_STATUS_SUCCESS) {
            softOn = (BOOLEAN)(params.SoftwareRadioState != 0);
            CleanupParsedWdiTaskSetRadioStateToIhv(&params);
        }
    }
    dev->RadioOn = softOn;
    if (dev->ChipOps && dev->ChipOps->SetRadio) {
        (void)dev->ChipOps->SetRadio(dev->ChipCtx, softOn);
    }
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "SetRadioStateHandler",
        TraceLoggingUInt8(softOn, "SoftOn"),
        TraceLoggingUInt16(hdr->PortId, "PortId"),
        TraceLoggingUInt32(hdr->TransactionId, "Txn"));

    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    if (*bw == 0) return STATUS_BUFFER_TOO_SMALL;

    // Every SET_RADIO_STATE task needs its matching *_COMPLETE indication
    // (id 22) — fire it deferred via workitem (firing inline killed the
    // adapter, same pattern as DOT11_RESET).
    LONG slot = InterlockedIncrement(&dev->RadioQueueTail) - 1;
    if (slot - dev->RadioQueueHead < 16) {
        UINT idx = slot & 0xF;
        dev->RadioQueue[idx].PortId = hdr->PortId;
        dev->RadioQueue[idx].TxnId  = hdr->TransactionId;
        dev->RadioQueue[idx].SoftOn = softOn;
        WdfWorkItemEnqueue(dev->RadioStatusWorkItem);
    } else {
        InterlockedDecrement(&dev->RadioQueueTail);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS HandleCreatePort(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                 UINT, UINT outLen, UINT* bw) {
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS HandleDeletePort(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                 UINT, UINT outLen, UINT* bw) {
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// WDI_GET_STATISTICS (0x22 = 34) — needs a TLV-encoded WDI_GET_STATISTICS_
// PARAMETERS with at least one MAC stats entry (broadcast MAC) and one PHY
// stats entry. Header echo alone makes nwifi reject with 0xC0000001.
static NTSTATUS HandleGetStatistics(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                    UINT, UINT outLen, UINT* bw)
{
    WDI_GET_STATISTICS_PARAMETERS params;

    WDI_MAC_STATISTICS_CONTAINER macStats[1];   // zeroed via default ctor
    static const UINT8 bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    RtlCopyMemory(macStats[0].MACAddress.Address, bcast, 6);
    params.PeerMACStatistics.SimpleAssign(macStats, 1);

    WDI_PHY_STATISTICS_CONTAINER phyStats[1];   // zeroed via default ctor
    phyStats[0].PhyType = WDI_PHY_TYPE_HT;
    params.PhyStatistics.SimpleAssign(phyStats, 1);

    TLV_CONTEXT ctx = { 0, dev->OsWdiVersion ? dev->OsWdiVersion : WDI_VERSION_LATEST };
    ULONG bufLen = 0;
    UINT8* genBuf = NULL;
    NDIS_STATUS gs = GenerateWdiGetStatisticsFromIhv(
        &params,
        sizeof(WDI_MESSAGE_HEADER),
        &ctx,
        &bufLen,
        &genBuf);
    if (gs != NDIS_STATUS_SUCCESS || genBuf == NULL) {
        DbgPrint("MTK: GenerateWdiGetStatistics failed 0x%x\n", gs);
        if (genBuf) FreeGenerated(genBuf);
        *bw = 0;
        return STATUS_UNSUCCESSFUL;
    }
    if (bufLen > outLen) {
        DbgPrint("MTK: GetStatistics buf too large %u > %u\n", bufLen, outLen);
        FreeGenerated(genBuf);
        *bw = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    PVOID outBuf = WifiRequestGetInOutBuffer(req, NULL, NULL);
    RtlCopyMemory(outBuf, genBuf, bufLen);
    PWDI_MESSAGE_HEADER outHdr = (PWDI_MESSAGE_HEADER)outBuf;
    outHdr->PortId = hdr->PortId;
    outHdr->Reserved = 0;
    outHdr->Status = STATUS_SUCCESS;
    outHdr->TransactionId = hdr->TransactionId;
    outHdr->IhvSpecificId = 0;

    *bw = bufLen;
    FreeGenerated(genBuf);
    return STATUS_SUCCESS;
}

// ---------- connect / disconnect ----------

static NTSTATUS
HandleTaskConnect(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                  UINT inLen, UINT outLen, UINT* bw)
{
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "TaskConnect",
        TraceLoggingUInt16(hdr->PortId, "PortId"),
        TraceLoggingUInt32(hdr->TransactionId, "Txn"),
        TraceLoggingUInt32(inLen, "inLen"));

    if (!dev->RadioOn) {
        *bw = WriteWdiResponseHeader(req, hdr, outLen);
        return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
    }

    WDI_CONNECT_TARGET target;
    RtlZeroMemory(&target, sizeof(target));
    target.Channel = 0;
    target.SsidLen = 0;
    target.UseRsn  = FALSE;

    ULONG tlvLen = 0;
    const UINT8* tlv = WdiTlvBody(hdr, inLen, &tlvLen);
    if (tlv) {
        WDI_TASK_CONNECT_PARAMETERS cp;
        TLV_CONTEXT ctx = WdiTlvContext(dev->OsWdiVersion);
        NDIS_STATUS ps = ParseWdiTaskConnectToIhv(tlvLen, tlv, &ctx, &cp);
        if (ps == NDIS_STATUS_SUCCESS) {
            if (cp.PreferredBSSEntryList.ElementCount > 0 &&
                cp.PreferredBSSEntryList.pElements != nullptr) {
                const WDI_CONNECT_BSS_ENTRY_CONTAINER* be =
                    &cp.PreferredBSSEntryList.pElements[0];
                RtlCopyMemory(target.Bssid, be->BSSID.Address, 6);
                target.Channel = (UCHAR)be->ChannelInfo.ChannelNumber;
            }
            if (cp.ConnectParameters.SSIDList.ElementCount > 0 &&
                cp.ConnectParameters.SSIDList.pElements != nullptr) {
                const WDI_SSID* s = &cp.ConnectParameters.SSIDList.pElements[0];
                if (s->pElements != nullptr &&
                    s->ElementCount >= 1 && s->ElementCount <= 32) {
                    target.SsidLen = (UCHAR)s->ElementCount;
                    RtlCopyMemory(target.Ssid, s->pElements, s->ElementCount);
                }
            }
            for (UINT32 ai = 0;
                 ai < cp.ConnectParameters.AuthenticationAlgorithms.ElementCount;
                 ai++) {
                WDI_AUTH_ALGORITHM a =
                    cp.ConnectParameters.AuthenticationAlgorithms.pElements[ai];
                if (a == WDI_AUTH_ALGO_RSNA_PSK || a == WDI_AUTH_ALGO_RSNA) {
                    target.UseRsn = TRUE;
                    break;
                }
            }
            CleanupParsedWdiTaskConnectToIhv(&cp);
        }
    }

    // Queue the task. Workitem fires ASSOCIATION_RESULT (chip side)
    // and CONNECT_COMPLETE (WDI side) on the way out.
    LONG slot = InterlockedIncrement(&dev->ConnectQueueTail) - 1;
    if (slot - dev->ConnectQueueHead < 8) {
        UINT idx = slot & 0x7;
        dev->ConnectQueue[idx].PortId = hdr->PortId;
        dev->ConnectQueue[idx].TxnId  = hdr->TransactionId;
        dev->ConnectQueue[idx].Target = target;
        WdfWorkItemEnqueue(dev->ConnectCompleteWorkItem);
    } else {
        InterlockedDecrement(&dev->ConnectQueueTail);
        DbgPrint("MTK: CONNECT queue full, dropping\n");
    }

    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS
HandleTaskDisconnect(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                     UINT outLen, UINT* bw)
{
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "TaskDisconnect",
        TraceLoggingUInt16(hdr->PortId, "PortId"),
        TraceLoggingUInt32(hdr->TransactionId, "Txn"));

    LONG slot = InterlockedIncrement(&dev->DisconnectQueueTail) - 1;
    if (slot - dev->DisconnectQueueHead < 8) {
        UINT idx = slot & 0x7;
        dev->DisconnectQueue[idx].PortId = hdr->PortId;
        dev->DisconnectQueue[idx].TxnId  = hdr->TransactionId;
        WdfWorkItemEnqueue(dev->DisconnectCompleteWorkItem);
    } else {
        InterlockedDecrement(&dev->DisconnectQueueTail);
    }

    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// WDI_SET_LOCATION_PRIVACY (0x8A = 138) — sync ack.
static NTSTATUS HandleSetLocationPrivacy(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                                          UINT, UINT outLen, UINT* bw) {
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// ---------- scan ----------

static NTSTATUS
HandleStartScan(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                UINT, UINT outLen, UINT* bw)
{
    // Radio off: ack the task but emit no BSS list / SCAN_COMPLETE only.
    // Otherwise wlansvc would still see fake networks even with radio off.
    if (!dev->RadioOn) {
        *bw = WriteWdiResponseHeader(req, hdr, outLen);
        return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
    }
    // Ack the SCAN task synchronously so the Task is gone BEFORE the
    // BSS_ENTRY_LIST indication arrives — otherwise the indication is
    // hijacked by Task::OnDeviceIndicationArrived and never reaches the
    // CPort BSS-ingest path (confirmed via kd trace).
    LONG slot = InterlockedIncrement(&dev->ScanQueueTail) - 1;
    if (slot - dev->ScanQueueHead < 16) {
        UINT idx = slot & 0xF;
        dev->ScanQueue[idx].PortId  = hdr->PortId;
        dev->ScanQueue[idx].TxnId   = hdr->TransactionId;
        dev->ScanQueue[idx].Request = NULL;  // workitem must NOT complete it
        dev->ActiveScanPortId = hdr->PortId;
        dev->ActiveScanTransactionId = hdr->TransactionId;
        WdfWorkItemEnqueue(dev->ScanCompleteWorkItem);
    } else {
        InterlockedDecrement(&dev->ScanQueueTail);
        DbgPrint("MTK: SCAN queue full, dropping\n");
    }
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// ---------- adapter caps ----------

static NTSTATUS
HandleGetAdapterCaps(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                    UINT, UINT outLen, UINT* bw)
{
    // Caps are framework-synthesized from WifiDeviceSet*Capabilities; bare
    // header echo. (WDI_GET_ADAPTER_CAPABILITIES = 37 is rarely sent on
    // WiFiCx — the framework satisfies most queries internally.)
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// WDI_TASK_DOT11_RESET (id=7) — sync ack, plus DEFERRED indication via
// workitem. WiFiCx tracks outstanding TASK_*'s and fires hardware-failure
// at ~30s via Task::OnTimerCallback if the matching *_COMPLETE indication
// never arrives. Firing the indication inline killed nwifi within 2ms
// (probably arrived before WifiRequestComplete returned). Queue it on a
// workitem so it dispatches after the request is fully completed.
static NTSTATUS HandleDot11Reset(WDFDEVICE WdfDevice, WIFIREQUEST req,
                                 PWDI_MESSAGE_HEADER hdr, UINT outLen, UINT* bw)
{
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    if (!*bw) return STATUS_BUFFER_TOO_SMALL;

    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);

    // Multiple DOT11_RESET requests can stack up (OS often issues two
    // back-to-back). Append to a small fixed queue; the workitem drains it.
    // Drop if queue full (16 slots).
    LONG slot = InterlockedIncrement(&dev->ResetQueueTail) - 1;
    if (slot - dev->ResetQueueHead < 16) {
        UINT idx = slot & 0xF;   // 16-slot circular buffer
        dev->ResetQueue[idx].PortId = hdr->PortId;
        dev->ResetQueue[idx].TxnId  = hdr->TransactionId;
        WdfWorkItemEnqueue(dev->ResetCompleteWorkItem);
    } else {
        // queue full — give up the slot
        InterlockedDecrement(&dev->ResetQueueTail);
        DbgPrint("MTK: RESET queue full, dropping\n");
    }
    return STATUS_SUCCESS;
}

// ---------- BSS-list indication (TLV-encoded) ----------

WDI_SCAN_CACHE*
WdiDeviceGetScanCache(_In_ WDFDEVICE WdfDevice)
{
    return MtkGetDeviceContext(WdfDevice)->ScanCache;
}

void
WdiEmitBssEntryList(_In_ WDFDEVICE WdfDevice)
{
    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);
    if (!dev->ScanCache) return;

    // Drain a snapshot from the cache. Each snapshot entry owns its
    // beacon bytes inline, so the WDI_BSS_ENTRY_CONTAINER pointers
    // below stay valid through TLV generation as long as `snap`
    // lives. Heap-allocated to keep the kernel stack small.
    constexpr SIZE_T MAX_ENTRIES = 32;
    SIZE_T snapBytes = MAX_ENTRIES * sizeof(WDI_SCAN_CACHE_ENTRY);
    WDI_SCAN_CACHE_ENTRY* snap =
        (WDI_SCAN_CACHE_ENTRY*)ExAllocatePool2(POOL_FLAG_NON_PAGED, snapBytes, 'sBiW');
    if (!snap) return;

    SIZE_T rawCount = WdiScanCacheSnapshot(dev->ScanCache, snap, MAX_ENTRIES);
    if (rawCount == 0) {
        ExFreePool(snap);
        return;
    }

    WDI_BSS_ENTRY_CONTAINER* entries =
        (WDI_BSS_ENTRY_CONTAINER*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
            rawCount * sizeof(WDI_BSS_ENTRY_CONTAINER), 'eBiW');
    if (!entries) {
        ExFreePool(snap);
        return;
    }
    // ExAllocatePool2 zero-initializes — matches what a stack-array
    // default-construction would give the TLV generator (Optional
    // flags clear, no IsPresent bits set spuriously).

    for (SIZE_T i = 0; i < rawCount; ++i) {
        WDI_BSS_ENTRY_CONTAINER& e = entries[i];

        RtlCopyMemory(e.BSSID.Address, snap[i].Bssid, 6);

        e.SignalInfo.RSSI = snap[i].RssiDbm;
        e.SignalInfo.LinkQuality =
            (UINT32)max(0, min(100, (INT32)(2 * (snap[i].RssiDbm + 100))));

        e.ChannelInfo.ChannelNumber = WdiFreqToChannel(snap[i].ChannelCenterFrequencyMhz);
        e.ChannelInfo.BandId = WdiFreqToBand(snap[i].ChannelCenterFrequencyMhz);

        e.BeaconFrame.SimpleAssign(
            snap[i].Beacon,
            (UINT32)snap[i].BeaconLength);
        e.Optional.BeaconFrame_IsPresent = TRUE;

        e.EntryAgeInfo.HostTimeStamp = (UINT64)snap[i].LastSeenTick.QuadPart;
        e.EntryAgeInfo.CachedInformation = FALSE;
        e.Optional.EntryAgeInfo_IsPresent = TRUE;
    }

    WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS params;
    params.DeviceDescriptor.SimpleAssign(entries, (UINT32)rawCount);
    params.Optional.DeviceDescriptor_IsPresent = TRUE;

    TLV_CONTEXT ctx = WdiTlvContext(dev->OsWdiVersion);
    ULONG bufLen = 0;
    UINT8* genBuf = NULL;
    NDIS_STATUS gs = GenerateWdiIndicationBssEntryListFromIhv(
        &params,
        sizeof(WDI_MESSAGE_HEADER),
        &ctx,
        &bufLen,
        &genBuf);
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "BssListGenerate",
        TraceLoggingHexUInt32(gs, "gs"),
        TraceLoggingUInt32(bufLen, "bufLen"),
        TraceLoggingUInt32((UINT32)rawCount, "rawCount"));

    if (gs == NDIS_STATUS_SUCCESS && genBuf != NULL) {
        // Unsolicited — TxnId=0 routes to CPort BSS-ingest path
        // instead of being hijacked by the outstanding SCAN Task.
        (void)WdiIndicateTlv(WdfDevice, WDI_INDICATION_BSS_ENTRY_LIST,
                             dev->ActiveScanPortId, 0,
                             &genBuf, bufLen);
    } else {
        DbgPrint("MTK: GenerateWdiIndicationBssEntryListFromIhv failed 0x%x\n", gs);
        if (genBuf) FreeGenerated(genBuf);
    }

    ExFreePool(entries);
    ExFreePool(snap);
}
