#include "precomp.h"
#include <wlan/2.0/TlvGeneratorParser.hpp>
#include "wdi_tlv.h"

TLV_CONTEXT
WdiTlvContext(_In_ ULONG OsWdiVersion)
{
    TLV_CONTEXT ctx = { 0, OsWdiVersion ? OsWdiVersion : WDI_VERSION_LATEST };
    return ctx;
}

const UINT8*
WdiTlvBody(
    _In_ PWDI_MESSAGE_HEADER MsgHdr,
    _In_ UINT InLen,
    _Out_ ULONG* OutLen)
{
    *OutLen = 0;
    if (!MsgHdr || InLen <= sizeof(WDI_MESSAGE_HEADER)) {
        return NULL;
    }
    *OutLen = InLen - (ULONG)sizeof(WDI_MESSAGE_HEADER);
    return (const UINT8*)MsgHdr + sizeof(WDI_MESSAGE_HEADER);
}

NTSTATUS
WdiIndicateTaskComplete(
    _In_ WDFDEVICE WdfDevice,
    _In_ UINT16 IndicationId,
    _In_ UINT16 PortId,
    _In_ UINT32 TransactionId)
{
    WDFMEMORY mem = NULL;
    PWDI_MESSAGE_HEADER hdr = NULL;
    NTSTATUS status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        'IFiW',
        sizeof(WDI_MESSAGE_HEADER),
        &mem,
        (PVOID*)&hdr);
    if (!NT_SUCCESS(status)) return status;

    RtlZeroMemory(hdr, sizeof(*hdr));
    hdr->PortId = PortId;
    hdr->TransactionId = TransactionId;
    hdr->Status = STATUS_SUCCESS;

    WifiDeviceReceiveIndication(WdfDevice, IndicationId, mem);
    WdfObjectDelete(mem);
    return STATUS_SUCCESS;
}

NTSTATUS
WdiIndicateTlv(
    _In_ WDFDEVICE WdfDevice,
    _In_ UINT16 IndicationId,
    _In_ UINT16 PortId,
    _In_ UINT32 TransactionId,
    _Inout_ _At_(*GenBuf, _Pre_notnull_ _Post_null_) UINT8** GenBuf,
    _In_ ULONG BufLen)
{
    if (!GenBuf || !*GenBuf) return STATUS_INVALID_PARAMETER;

    NTSTATUS status = STATUS_SUCCESS;
    UINT8* buf = *GenBuf;

    if (BufLen < sizeof(WDI_MESSAGE_HEADER)) {
        status = STATUS_BUFFER_TOO_SMALL;
        goto done;
    }

    // Caller-shape contract: the Generate*FromIhv functions reserve
    // the header bytes at the front but don't fill them — that's
    // ours. Overwrite cleanly.
    PWDI_MESSAGE_HEADER ih = (PWDI_MESSAGE_HEADER)buf;
    RtlZeroMemory(ih, sizeof(*ih));
    ih->PortId = PortId;
    ih->TransactionId = TransactionId;
    ih->Status = STATUS_SUCCESS;

    WDFMEMORY mem = NULL;
    PVOID memBuf = NULL;
    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        'IFiW',
        BufLen,
        &mem,
        &memBuf);
    if (!NT_SUCCESS(status)) goto done;

    RtlCopyMemory(memBuf, buf, BufLen);
    WifiDeviceReceiveIndication(WdfDevice, IndicationId, mem);
    WdfObjectDelete(mem);

done:
    FreeGenerated(buf);
    *GenBuf = NULL;
    return status;
}
