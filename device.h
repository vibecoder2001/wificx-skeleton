#pragma once

typedef struct _MTK_DEVICE
{
    MTK_ADAPTER* Adapter;
    WDFDEVICE FxDevice;

    // Phase 3: WDI session state
    BOOLEAN AdapterOpened;
    BOOLEAN RadioOn;
    ULONG  OsWdiVersion;     // negotiated WDI version reported by the OS
    WDFWORKITEM ScanCompleteWorkItem;
    UINT32 ActiveScanTransactionId;
    UINT16 ActiveScanPortId;
    // Same queue pattern as RESET — OS issues multiple back-to-back scans.
    struct { UINT32 TxnId; UINT16 PortId; WIFIREQUEST Request; } ScanQueue[16];
    LONG ScanQueueHead;
    LONG ScanQueueTail;

    // For deferred DOT11_RESET_COMPLETE indication. Firing inline from the
    // dispatcher made nwifi detach within 2ms — needs to be queued so the
    // WDI request is fully complete before the indication arrives.
    // OS often issues 2+ DOT11_RESETs back-to-back — small queue so we
    // don't miss any (each timed independently by WifiCx framework).
    WDFWORKITEM ResetCompleteWorkItem;
    struct { UINT32 TxnId; UINT16 PortId; } ResetQueue[16];
    LONG ResetQueueHead;        // workitem reads from here
    LONG ResetQueueTail;        // dispatcher writes here

    // Same pattern for SET_RADIO_STATE: WiFiCx's Task expects a matching
    // (TxnId,PortId) indication to complete; firing inline killed adapter.
    WDFWORKITEM RadioStatusWorkItem;
    struct { UINT32 TxnId; UINT16 PortId; BOOLEAN SoftOn; } RadioQueue[16];
    LONG RadioQueueHead;
    LONG RadioQueueTail;
} MTK_DEVICE, *PMTK_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MTK_DEVICE, MtkGetDeviceContext);

EVT_WDF_DEVICE_PREPARE_HARDWARE     EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     EvtDeviceReleaseHardware;