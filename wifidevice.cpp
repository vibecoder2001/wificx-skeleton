#include "precomp.h"
#include "trace.h"
#include "device.h"
#include "wifidevice.h"
#include "adapter.h"
#include "wdihandlers.h"

_Use_decl_annotations_
NTSTATUS
EvtWiFiDeviceCreateAdapter(
	_In_ WDFDEVICE WdfDevice,
	_Inout_ NETADAPTER_INIT* AdapterInit
)
{
    TraceEntry();

    NTSTATUS status = STATUS_SUCCESS;
    DbgPrint("MTK: EvtWiFiDeviceCreateAdapter entry, AdapterInit=%p\n", AdapterInit);

    GOTO_WITH_INSUFFICIENT_RESOURCES_IF_NULL(Exit, status, AdapterInit);

    NET_ADAPTER_DATAPATH_CALLBACKS datapathCallbacks;
    NET_ADAPTER_DATAPATH_CALLBACKS_INIT(
        &datapathCallbacks,
        EvtAdapterCreateTxQueue,
        EvtAdapterCreateRxQueue);

    NetAdapterInitSetDatapathCallbacks(
        AdapterInit,
        &datapathCallbacks);

    // WiFi adapters MUST declare TX demux before NetAdapterCreate, or
    // NetAdapterCx asserts after start. One peer-address demux range is
    // enough for a STA-only fake adapter.
    WIFI_ADAPTER_TX_DEMUX peerDemux;
    WIFI_ADAPTER_TX_PEER_ADDRESS_DEMUX_INIT(&peerDemux, 1);
    WifiAdapterInitAddTxDemux(AdapterInit, &peerDemux);

    WDF_OBJECT_ATTRIBUTES adapterAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&adapterAttributes, MTK_ADAPTER);

    NETADAPTER netAdapter;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        NetAdapterCreate(AdapterInit, &adapterAttributes, &netAdapter));
    DbgPrint("MTK: NetAdapterCreate OK\n");

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiAdapterInitialize(netAdapter));
    DbgPrint("MTK: WifiAdapterInitialize OK\n");

    MTK_ADAPTER* adapter = MtkGetAdapterContext(netAdapter);
    MTK_DEVICE* device = MtkGetDeviceContext(WdfDevice);

    device->Adapter = adapter;

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        MtkInitializeAdapterContext(adapter, WdfDevice, netAdapter));
    DbgPrint("MTK: MtkInitializeAdapterContext OK\n");

    {
        // Bring up disconnected with a finite link speed (matching the
        // capability cap below). NDIS_LINK_SPEED_UNKNOWN == (ULONG64)-1
        // exceeds any declared max and trips NetAdapterCx validation.
        NET_ADAPTER_LINK_STATE linkState;
        NET_ADAPTER_LINK_STATE_INIT(
            &linkState,
            54'000'000ULL,
            MediaConnectStateDisconnected,
            MediaDuplexStateFull,
            NetAdapterPauseFunctionTypeUnsupported,
            NetAdapterAutoNegotiationFlagNone);
        NetAdapterSetLinkState(adapter->NetAdapter, &linkState);
    }
    DbgPrint("MTK: NetAdapterSetLinkState OK\n");

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        MtkAdapterStart(adapter));
    DbgPrint("MTK: MtkAdapterStart OK\n");

    // Send WDI_INDICATION_OPEN_COMPLETE (95) to tell the OS the adapter
    // is ready to handle WDI commands. WiFiCx docs say TASK_OPEN is no
    // longer invoked, but wlansvc still appears to wait for this
    // indication before enumerating the adapter.
    {
        WDFMEMORY mem = NULL;
        PWDI_MESSAGE_HEADER hdr = NULL;
        if (NT_SUCCESS(WdfMemoryCreate(
                WDF_NO_OBJECT_ATTRIBUTES,
                NonPagedPoolNx,
                'IFiW',
                sizeof(WDI_MESSAGE_HEADER),
                &mem,
                (PVOID*)&hdr))) {
            RtlZeroMemory(hdr, sizeof(*hdr));
            hdr->PortId = WDI_PORT_ID_ADAPTER;
            hdr->TransactionId = 0;            // unsolicited
            hdr->Status = STATUS_SUCCESS;
            WifiDeviceReceiveIndication(WdfDevice, WDI_INDICATION_OPEN_COMPLETE, mem);
            WdfObjectDelete(mem);
            DbgPrint("MTK: fired WDI_INDICATION_OPEN_COMPLETE\n");
        }
    }

Exit:
    DbgPrint("MTK: EvtWiFiDeviceCreateAdapter exit status=0x%x\n", status);
    TraceExitResult(status);

    return status;
}

_Use_decl_annotations_
void
EvtWiFiDeviceSendCommand(
    _In_ WDFDEVICE WdfDevice,
    _Inout_ WIFIREQUEST Request
)
{
    TraceEntry();

    UINT16 MessageID = WifiRequestGetMessageId(Request);
    UINT InputBufferLen, OutputBufferLen;
    PWDI_MESSAGE_HEADER Buffer = (PWDI_MESSAGE_HEADER)
        WifiRequestGetInOutBuffer(Request, &InputBufferLen, &OutputBufferLen);

    TraceLoggingWrite(WiFiCxSampleTraceProvider,
        "WiFiCommand",
        TraceLoggingHexInt16(MessageID),
        TraceLoggingUInt32(InputBufferLen),
        TraceLoggingUInt32(OutputBufferLen),
        TraceLoggingUInt16(Buffer->PortId),
        TraceLoggingUInt32(Buffer->TransactionId));

    WdiDispatchCommand(WdfDevice, Request);

    TraceExit();
}

_Use_decl_annotations_
NTSTATUS
EvtWifiDeviceCreateWiFiDirectDevice(
    _In_ WDFDEVICE Device,
    _In_ WIFIDIRECT_DEVICE_INIT* WfdDeviceInit
)
{
    TraceEntry();

    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES wfdDeviceAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&wfdDeviceAttributes, MTK_WIFIDIRECTDEVICE);

    WIFIDIRECTDEVICE wfdDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDirectDeviceCreate(WfdDeviceInit, &wfdDeviceAttributes, &wfdDevice));

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDirectDeviceInitialize(wfdDevice));

    PMTK_WIFIDIRECTDEVICE wfdContext = MtkGetWifiDirectContext(wfdDevice);
    wfdContext->WdfDevice = Device;
    wfdContext->WifiDirectDevice = wfdDevice;

Exit:
    TraceExitResult(status);

    return status;
}