#include "precomp.h"

#include <netadaptercx.h>

#include "trace.h"
#include "device.h"
#include "adapter.h"

NTSTATUS
MtkInitializeAdapterContext(
    _In_ MTK_ADAPTER* adapter,
    _In_ WDFDEVICE device,
    _In_ NETADAPTER netAdapter
)
/*++
Routine Description:

    Allocate MTK_ADAPTER data block and do some initialization

Arguments:

    adapter     Pointer to receive pointer to our adapter

Return Value:

    NTSTATUS failure code, or STATUS_SUCCESS

--*/
{
    TraceEntry();

    NTSTATUS status = STATUS_SUCCESS;

    adapter->NetAdapter = netAdapter;
    adapter->WdfDevice = device;

    //
    // Get WDF miniport device context.
    //
    MtkGetDeviceContext(adapter->WdfDevice)->Adapter = adapter;

    //spinlock
    WDF_OBJECT_ATTRIBUTES  attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = adapter->WdfDevice;

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfSpinLockCreate(&attributes, &adapter->Lock));

Exit:
    TraceExitResult(status);

    return status;

}

static
EVT_NET_ADAPTER_RETURN_RX_BUFFER EvtAdapterReturnRxBuffer;

_Use_decl_annotations_
void
EvtAdapterReturnRxBuffer(
    _In_ NETADAPTER netAdapter,
    _In_ PVOID rxBufferVa,
    _In_ PVOID rxBufferContext
)
{
    UNREFERENCED_PARAMETER(netAdapter);
    UNREFERENCED_PARAMETER(rxBufferVa);
    UNREFERENCED_PARAMETER(rxBufferContext);
    // No-op: we never produce RX buffers in the fake driver.
}

static
void
MtkAdapterSetDatapathCapabilities(
    _In_ MTK_ADAPTER const* adapter
)
{
    NET_ADAPTER_TX_CAPABILITIES txCapabilities;
    NET_ADAPTER_TX_CAPABILITIES_INIT(&txCapabilities, 1);
    txCapabilities.FragmentRingNumberOfElementsHint = MTK_MIN_TCB * MTK_MAX_PHYS_BUF_COUNT;
    txCapabilities.MaximumNumberOfFragments = MTK_MAX_PHYS_BUF_COUNT;

    NET_ADAPTER_RX_CAPABILITIES rxCapabilities;
    NET_ADAPTER_RX_CAPABILITIES_INIT_DRIVER_MANAGED(
        &rxCapabilities,
        EvtAdapterReturnRxBuffer,
        MTK_MAX_PACKET_SIZE + FRAME_CRC_SIZE + RSVD_BUF_SIZE,
        1);
    rxCapabilities.FragmentBufferAlignment = 64;
    rxCapabilities.FragmentRingNumberOfElementsHint = 32;

    NetAdapterSetDataPathCapabilities(adapter->NetAdapter, &txCapabilities, &rxCapabilities);
}

_Use_decl_annotations_
NTSTATUS
MtkAdapterStart(
    MTK_ADAPTER* adapter
)
{
    TraceEntryNetAdapter(adapter->NetAdapter);

    NTSTATUS status = STATUS_SUCCESS;

    {
        //barebones init 
        ULONG64 maxXmitLinkSpeed = MTK_MEDIA_MAX_SPEED;
        ULONG64 maxRcvLinkSpeed = MTK_MEDIA_MAX_SPEED;

        NET_ADAPTER_LINK_LAYER_CAPABILITIES linkLayerCapabilities;
        NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(
            &linkLayerCapabilities,
            maxXmitLinkSpeed,
            maxRcvLinkSpeed);

        NetAdapterSetLinkLayerCapabilities(adapter->NetAdapter, &linkLayerCapabilities);
        NetAdapterSetLinkLayerMtuSize(adapter->NetAdapter, MTK_MAX_PACKET_SIZE - ETH_LENGTH_OF_HEADER);

        // Locally-administered MAC (U/L bit set in first octet, unicast).
        const UCHAR fakeMac[ETHERNET_ADDRESS_LENGTH] =
            { 0x02, 0x57, 0x49, 0x46, 0x49, 0x01 };
        RtlCopyMemory(adapter->PermanentAddress.Address, fakeMac, ETHERNET_ADDRESS_LENGTH);
        adapter->PermanentAddress.Length = ETHERNET_ADDRESS_LENGTH;

        RtlCopyMemory(&adapter->CurrentAddress, &adapter->PermanentAddress, sizeof(adapter->PermanentAddress));

        NetAdapterSetPermanentLinkLayerAddress(adapter->NetAdapter, &adapter->PermanentAddress);
        NetAdapterSetCurrentLinkLayerAddress(adapter->NetAdapter, &adapter->CurrentAddress);
    }

    MtkAdapterSetDatapathCapabilities(adapter);

    GOTO_IF_NOT_NT_SUCCESS(
        Exit, status,
        NetAdapterStart(adapter->NetAdapter));

Exit:
    TraceExitResult(status);

    return status;
}