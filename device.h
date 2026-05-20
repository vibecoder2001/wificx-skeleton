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
} MTK_DEVICE, *PMTK_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MTK_DEVICE, MtkGetDeviceContext);

EVT_WDF_DEVICE_PREPARE_HARDWARE     EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     EvtDeviceReleaseHardware;