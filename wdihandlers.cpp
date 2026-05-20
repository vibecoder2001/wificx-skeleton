#include "precomp.h"
#include <wlan/2.0/TlvGeneratorParser.hpp>
#include "trace.h"
#include "device.h"
#include "adapter.h"
#include "fakebss.h"
#include "wdihandlers.h"

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
                                    UINT, UINT outLen, UINT* bw) {
    dev->RadioOn = TRUE;
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
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

// ---------- scan ----------

static NTSTATUS
HandleStartScan(MTK_DEVICE* dev, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                UINT, UINT outLen, UINT* bw)
{
    dev->ActiveScanTransactionId = hdr->TransactionId;
    dev->ActiveScanPortId = hdr->PortId;
    WdfWorkItemEnqueue(dev->ScanCompleteWorkItem);
    // Workitem fires WDI_INDICATION_SCAN_COMPLETE. BSS entry list
    // (WDI_INDICATION_BSS_ENTRY_LIST) is not yet emitted — needs TLV
    // encoding via wlan\2.0\TlvGenerated_.hpp.
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// ---------- adapter caps ----------

static NTSTATUS
HandleGetAdapterCaps(MTK_DEVICE*, WIFIREQUEST req, PWDI_MESSAGE_HEADER hdr,
                    UINT, UINT outLen, UINT* bw)
{
    // Caps are framework-synthesized from WifiDeviceSet*Capabilities;
    // we just acknowledge with a header echo.
    *bw = WriteWdiResponseHeader(req, hdr, outLen);
    return *bw ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

// ---------- BSS-list indication (TLV-encoded) ----------

static UCHAR
FreqToChannel(UINT32 freqMhz)
{
    if (freqMhz <= 2484) return (UCHAR)((freqMhz - 2407) / 5);
    return (UCHAR)((freqMhz - 5000) / 5);
}

static WDI_BAND_ID
FreqToBand(UINT32 freqMhz)
{
    return (freqMhz < 4000) ? WDI_BAND_ID_2400 : WDI_BAND_ID_5000;
}

void
WdiEmitBssEntryList(_In_ WDFDEVICE WdfDevice)
{
    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);

    SIZE_T fakeCount = 0;
    const FAKE_BSS_ENTRY* fake = FakeBssGetTable(&fakeCount);

    // Build WDI BSS entry array from our fake table. Stack-allocate since
    // FAKE_BSS_COUNT is small and fixed (4).
    WDI_BSS_ENTRY_CONTAINER entries[FAKE_BSS_COUNT];

    for (SIZE_T i = 0; i < fakeCount; ++i) {
        WDI_BSS_ENTRY_CONTAINER& e = entries[i];
        // (default ctor zeros Optional + BSSID)

        RtlCopyMemory(e.BSSID.Address, fake[i].Bssid, 6);

        e.SignalInfo.RSSI = fake[i].RssiDbm;
        e.SignalInfo.LinkQuality =
            (UINT32)max(0, min(100, (INT32)(2 * (fake[i].RssiDbm + 100))));

        e.ChannelInfo.ChannelNumber = FreqToChannel(fake[i].ChannelCenterFrequencyMhz);
        e.ChannelInfo.BandId = FreqToBand(fake[i].ChannelCenterFrequencyMhz);

        // Beacon frame: full 802.11 body (12B fixed + IEs).
        e.BeaconFrame.SimpleAssign(
            const_cast<UINT8*>(fake[i].BeaconBuffer),
            (UINT32)fake[i].BeaconLength);
        e.Optional.BeaconFrame_IsPresent = TRUE;
    }

    WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS params;
    params.DeviceDescriptor.SimpleAssign(entries, (UINT32)fakeCount);
    params.Optional.DeviceDescriptor_IsPresent = TRUE;

    TLV_CONTEXT ctx = { 0, dev->OsWdiVersion ? dev->OsWdiVersion : WDI_VERSION_LATEST };
    ULONG bufLen = 0;
    UINT8* genBuf = NULL;
    NDIS_STATUS gs = GenerateWdiIndicationBssEntryListFromIhv(
        &params,
        sizeof(WDI_MESSAGE_HEADER),   // reserve room at front for the header
        &ctx,
        &bufLen,
        &genBuf);
    if (gs != NDIS_STATUS_SUCCESS || genBuf == NULL || bufLen < sizeof(WDI_MESSAGE_HEADER)) {
        DbgPrint("MTK: GenerateWdiIndicationBssEntryListFromIhv failed 0x%x\n", gs);
        if (genBuf) FreeGenerated(genBuf);
        return;
    }

    // Fill the reserved WDI_MESSAGE_HEADER.
    PWDI_MESSAGE_HEADER hdr = (PWDI_MESSAGE_HEADER)genBuf;
    RtlZeroMemory(hdr, sizeof(*hdr));
    hdr->PortId = dev->ActiveScanPortId;
    hdr->TransactionId = dev->ActiveScanTransactionId;
    hdr->Status = STATUS_SUCCESS;

    // Copy to a WDFMEMORY for WifiDeviceReceiveIndication.
    WDFMEMORY memory = NULL;
    PVOID memBuf = NULL;
    NTSTATUS ns = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        'IFiW',
        bufLen,
        &memory,
        &memBuf);
    if (NT_SUCCESS(ns)) {
        RtlCopyMemory(memBuf, genBuf, bufLen);
        WifiDeviceReceiveIndication(WdfDevice, WDI_INDICATION_BSS_ENTRY_LIST, memory);
        WdfObjectDelete(memory);
    } else {
        DbgPrint("MTK: WdfMemoryCreate for BSS list failed 0x%x\n", ns);
    }

    FreeGenerated(genBuf);
}
