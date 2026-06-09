#include "precomp.h"
#include <wlan/2.0/TlvGeneratorParser.hpp>
#include "device.h"
#include "wdi_connect.h"
#include "wdi_mmpdu.h"
#include "wdi_tlv.h"
#include "trace.h"

// Placement new — needed to construct the WDI_*_CONTAINER's
// non-POD members in pool-allocated memory. Stdlib's <new> isn't
// available in kernel mode; this is the standard signature.
inline void* __cdecl operator new(size_t, void* p) noexcept { return p; }
inline void  __cdecl operator delete(void*, void*) noexcept {}

void
WdiEmitAssociationResult(
    _In_ WDFDEVICE WdfDevice,
    _In_reads_(6) const UCHAR* PeerBssid,
    _In_reads_bytes_(ReqLen) const UINT8* AssocReqBody,
    _In_ ULONG ReqLen,
    _In_reads_bytes_(RespLen) const UINT8* AssocRespBody,
    _In_ ULONG RespLen,
    _In_ UINT16 StatusCode)
{
    // Contract check — see wdi_mmpdu.h for the war story.
    NT_ASSERT(WdiMmpduAssertIsBody(AssocReqBody, ReqLen));
    NT_ASSERT(WdiMmpduAssertIsBody(AssocRespBody, RespLen));

    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);

    // Heap-allocate the container so its ArrayOfElements / Optional
    // members get default-initialized via ctor (placement new).
    WDI_ASSOCIATION_RESULT_CONTAINER* entry = (WDI_ASSOCIATION_RESULT_CONTAINER*)
        ExAllocatePool2(POOL_FLAG_NON_PAGED,
                        sizeof(WDI_ASSOCIATION_RESULT_CONTAINER), 'rAiW');
    if (!entry) return;
    new (entry) WDI_ASSOCIATION_RESULT_CONTAINER();

    RtlCopyMemory(entry->BSSID.Address, PeerBssid, 6);

    // Phase-4 scope: Open BSS / clear-port. The fake backend exercises
    // this path; RSNA is Phase 5 (key mgmt).
    entry->AssociationResultParameters.AssociationStatus =
        StatusCode == 0 ? WDI_ASSOC_STATUS_SUCCESS : WDI_ASSOC_STATUS_FAILURE;
    entry->AssociationResultParameters.StatusCode = StatusCode;
    entry->AssociationResultParameters.AuthAlgorithm = WDI_AUTH_ALGO_80211_OPEN;
    entry->AssociationResultParameters.UnicastCipherAlgorithm = WDI_CIPHER_ALGO_NONE;
    entry->AssociationResultParameters.MulticastDataCipherAlgorithm = WDI_CIPHER_ALGO_NONE;
    entry->AssociationResultParameters.MulticastMgmtCipherAlgorithm = WDI_CIPHER_ALGO_NONE;
    entry->AssociationResultParameters.PortAuthorized = TRUE;
    entry->AssociationResultParameters.BandID = WDI_BAND_ID_2400;
    entry->AssociationResultParameters.WMMQoSEnabled = FALSE;

    // ActivePhyTypeList is mandatory — without it the generator
    // returns 0xC0010015 and emits nothing.
    static WDI_PHY_TYPE phyTypes[1] = { WDI_PHY_TYPE_HT };
    entry->ActivePhyTypeList.SimpleAssign(phyTypes, 1);

    entry->AssociationRequestFrame.SimpleAssign(
        const_cast<UINT8*>(AssocReqBody), ReqLen);
    entry->Optional.AssociationRequestFrame_IsPresent = TRUE;

    entry->AssociationResponseFrame.SimpleAssign(
        const_cast<UINT8*>(AssocRespBody), RespLen);
    entry->Optional.AssociationResponseFrame_IsPresent = TRUE;

    WDI_INDICATION_ASSOCIATION_RESULT_LIST params;
    params.AssociationResults.SimpleAssign(entry, 1);

    TLV_CONTEXT ctx = WdiTlvContext(dev->OsWdiVersion);
    ULONG bufLen = 0;
    UINT8* genBuf = NULL;
    NDIS_STATUS gs = GenerateWdiIndicationAssociationResultFromIhv(
        &params, sizeof(WDI_MESSAGE_HEADER), &ctx, &bufLen, &genBuf);
    TraceLoggingWrite(WiFiCxSampleTraceProvider, "AssocResultGenerate",
        TraceLoggingHexUInt32(gs, "gs"),
        TraceLoggingUInt32(bufLen, "bufLen"),
        TraceLoggingUInt16(StatusCode, "Status"));
    if (gs != NDIS_STATUS_SUCCESS || genBuf == NULL) {
        DbgPrint("MTK: GenerateWdiIndicationAssociationResult failed 0x%x\n", gs);
        if (genBuf) FreeGenerated(genBuf);
        ExFreePool(entry);
        return;
    }

    // Unsolicited — TxnId=0 so the OS doesn't consume this as a
    // completion of the outstanding CONNECT Task (it has its own
    // CONNECT_COMPLETE indication for that).
    (void)WdiIndicateTlv(WdfDevice, WDI_INDICATION_ASSOCIATION_RESULT,
                         dev->ConnectPortId, 0, &genBuf, bufLen);
    ExFreePool(entry);
}

void
WdiEmitDisassociation(
    _In_ WDFDEVICE WdfDevice,
    _In_reads_(6) const UCHAR* PeerBssid,
    _In_ BOOLEAN IsDisassoc,
    _In_ UINT16 ReasonCode,
    _In_reads_bytes_opt_(BodyLen) const UINT8* Body,
    _In_ ULONG BodyLen)
{
    NT_ASSERT(WdiMmpduAssertIsBody(Body, BodyLen));

    MTK_DEVICE* dev = MtkGetDeviceContext(WdfDevice);

    WDI_INDICATION_DISASSOCIATION_PARAMETERS params;
    RtlCopyMemory(params.DisconnectIndicationParameters.MacAddress.Address,
                  PeerBssid, 6);
    params.DisconnectIndicationParameters.DisassociationWABIReason = IsDisassoc
        ? WDI_ASSOC_STATUS_PEER_DISASSOCIATED
        : WDI_ASSOC_STATUS_PEER_DEAUTHENTICATED;

    if (Body && BodyLen > 0) {
        if (IsDisassoc) {
            params.DisassociationFrame.SimpleAssign(
                const_cast<UINT8*>(Body), BodyLen);
            params.Optional.DisassociationFrame_IsPresent = TRUE;
        } else {
            params.DeauthFrame.SimpleAssign(
                const_cast<UINT8*>(Body), BodyLen);
            params.Optional.DeauthFrame_IsPresent = TRUE;
        }
    }

    UNREFERENCED_PARAMETER(ReasonCode);  // already in body[0..1]

    TLV_CONTEXT ctx = WdiTlvContext(dev->OsWdiVersion);
    ULONG bufLen = 0;
    UINT8* genBuf = NULL;
    NDIS_STATUS gs = GenerateWdiIndicationDisassociationFromIhv(
        &params, sizeof(WDI_MESSAGE_HEADER), &ctx, &bufLen, &genBuf);
    if (gs != NDIS_STATUS_SUCCESS || genBuf == NULL) {
        DbgPrint("MTK: GenerateWdiIndicationDisassociation failed 0x%x\n", gs);
        if (genBuf) FreeGenerated(genBuf);
        return;
    }

    (void)WdiIndicateTlv(WdfDevice, WDI_INDICATION_DISASSOCIATION,
                         dev->ConnectPortId, 0, &genBuf, bufLen);
}
