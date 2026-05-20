#include "precomp.h"

#include "trace.h"
#include "device.h"
#include "adapter.h"
#include "wdihandlers.h"

EVT_WDF_WORKITEM EvtScanCompleteWorkItem;

NTSTATUS
MtkInitializeHardware(
    _In_ MTK_DEVICE* pDevice,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    TraceEntry();
    //
    // Read the registry parameters
    //
    NTSTATUS status = STATUS_SUCCESS;

    WIFI_DEVICE_CAPABILITIES deviceCapabilities = { 0 };
    deviceCapabilities.Size = sizeof(deviceCapabilities);
    deviceCapabilities.HardwareRadioState = 1;
    deviceCapabilities.SoftwareRadioState = 1;
    deviceCapabilities.ActionFramesSupported = 0;
    deviceCapabilities.NumRxStreams = 1;
    deviceCapabilities.NumTxStreams = 1;
    deviceCapabilities.Support_eCSA = 0;
    deviceCapabilities.MACAddressRandomization = 0;
    deviceCapabilities.BluetoothCoexistenceSupport = WDI_BLUETOOTH_COEXISTENCE_PERFORMANCE_MAINTAINED;
    deviceCapabilities.SupportsNonWdiOidRequests = 0;
    deviceCapabilities.FastTransitionSupported = 0;
    deviceCapabilities.MU_MIMOSupported = 0;
    deviceCapabilities.BSSTransitionSupported = 0;
    deviceCapabilities.SAEAuthenticationSupported = 0;
    deviceCapabilities.MBOSupported = 0;
    deviceCapabilities.BeaconReportsImplemented = 0;

    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetDeviceCapabilities(pDevice->FxDevice, &deviceCapabilities));
    DbgPrint("MTK: WifiDeviceSetDeviceCapabilities OK\n");

    static DOT11_AUTH_CIPHER_PAIR cipherPairs;
    cipherPairs.AuthAlgoId = DOT11_AUTH_ALGO_80211_OPEN;
    cipherPairs.CipherAlgoId = DOT11_CIPHER_ALGO_NONE;

    WIFI_STATION_CAPABILITIES stationCapabilities = {};
    stationCapabilities.Size = sizeof(stationCapabilities);
    stationCapabilities.ScanSSIDListSize = 32;
    stationCapabilities.DesiredSSIDListSize = 32;
    stationCapabilities.PrivacyExemptionListSize = 16;
    stationCapabilities.KeyMappingTableSize = 16;
    stationCapabilities.DefaultKeyTableSize = 16;
    stationCapabilities.WEPKeyValueMaxLength = 16;
    stationCapabilities.MaxNumPerSTA = 16;
    stationCapabilities.NumSupportedUnicastAlgorithms = 1;
    stationCapabilities.UnicastAlgorithmsList = &cipherPairs;
    stationCapabilities.NumSupportedMulticastDataAlgorithms = 1;
    stationCapabilities.MulticastDataAlgorithmsList = &cipherPairs;
    stationCapabilities.NumSupportedMulticastMgmtAlgorithms = 1;
    stationCapabilities.MulticastMgmtAlgorithmsList = &cipherPairs;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetStationCapabilities(pDevice->FxDevice, &stationCapabilities));
    DbgPrint("MTK: WifiDeviceSetStationCapabilities OK\n");

    static WDI_CHANNEL_MAPPING_ENTRY ch2g[11] = {
        { 1, 2412 }, { 2, 2417 }, { 3, 2422 }, { 4, 2427 },
        { 5, 2432 }, { 6, 2437 }, { 7, 2442 }, { 8, 2447 },
        { 9, 2452 }, { 10, 2457 }, { 11, 2462 },
    };
    static WDI_CHANNEL_MAPPING_ENTRY ch5g[4] = {
        { 36, 5180 }, { 40, 5200 }, { 44, 5220 }, { 48, 5240 },
    };
    static UINT32 widths[1] = { 20 };
    static WDI_PHY_TYPE phyTypes[1] = { WDI_PHY_TYPE_HT };

    WIFI_BAND_INFO bandInfo[2] = {};
    bandInfo[0].BandID = WDI_BAND_ID_2400;
    bandInfo[0].BandState = TRUE;
    bandInfo[0].NumValidPhyTypes = ARRAYSIZE(phyTypes);
    bandInfo[0].ValidPhyTypeList = phyTypes;
    bandInfo[0].NumValidChannelTypes = ARRAYSIZE(ch2g);
    bandInfo[0].ValidChannelTypes = ch2g;
    bandInfo[0].NumChannelWidths = ARRAYSIZE(widths);
    bandInfo[0].ChannelWidthList = widths;

    bandInfo[1].BandID = WDI_BAND_ID_5000;
    bandInfo[1].BandState = TRUE;
    bandInfo[1].NumValidPhyTypes = ARRAYSIZE(phyTypes);
    bandInfo[1].ValidPhyTypeList = phyTypes;
    bandInfo[1].NumValidChannelTypes = ARRAYSIZE(ch5g);
    bandInfo[1].ValidChannelTypes = ch5g;
    bandInfo[1].NumChannelWidths = ARRAYSIZE(widths);
    bandInfo[1].ChannelWidthList = widths;

    WIFI_BAND_CAPABILITIES bandCapabilities = { 0 };
    bandCapabilities.Size = sizeof(bandCapabilities);
    bandCapabilities.NumBands = 2;
    bandCapabilities.BandInfoList = bandInfo;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetBandCapabilities(pDevice->FxDevice, &bandCapabilities));
    DbgPrint("MTK: WifiDeviceSetBandCapabilities OK\n");

    static WIFI_PHY_INFO phyInfo[1] = {};
    phyInfo[0].PhyType = WDI_PHY_TYPE_HT;
    phyInfo[0].NumberDataRateEntries = 0;

    WIFI_PHY_CAPABILITIES phyCapabilities = { 0 };
    phyCapabilities.Size = sizeof(phyCapabilities);
    phyCapabilities.NumPhyTypes = ARRAYSIZE(phyInfo);
    phyCapabilities.PhyInfoList = phyInfo;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetPhyCapabilities(pDevice->FxDevice, &phyCapabilities));
    DbgPrint("MTK: WifiDeviceSetPhyCapabilities OK\n");

    WIFI_WIFIDIRECT_CAPABILITIES wifiDirectCapabilities = { 0 };
    wifiDirectCapabilities.Size = sizeof(wifiDirectCapabilities);
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetWiFiDirectCapabilities(pDevice->FxDevice, &wifiDirectCapabilities));
    DbgPrint("MTK: WifiDeviceSetWiFiDirectCapabilities OK\n");

    WDF_WORKITEM_CONFIG wiConfig;
    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtScanCompleteWorkItem);
    WDF_OBJECT_ATTRIBUTES wiAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = pDevice->FxDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfWorkItemCreate(&wiConfig, &wiAttr, &pDevice->ScanCompleteWorkItem));

    pDevice->OsWdiVersion = WifiDeviceGetOsWdiVersion(pDevice->FxDevice);
    DbgPrint("MTK: OS WDI version = 0x%x\n", pDevice->OsWdiVersion);

Exit:
    TraceExitResult(status);
    return status;
}

_Use_decl_annotations_
NTSTATUS
EvtDevicePrepareHardware(
    _In_ WDFDEVICE device,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    MTK_DEVICE* devContext = MtkGetDeviceContext(device);

    TraceEntry();

    NTSTATUS status = STATUS_SUCCESS;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status, MtkInitializeHardware(devContext, resourcesRaw, resourcesTranslated));

Exit:
    TraceExitResult(status);
    return status;
}

_Use_decl_annotations_
NTSTATUS
EvtDeviceReleaseHardware(
    _In_ WDFDEVICE device,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    UNREFERENCED_PARAMETER(resourcesTranslated);
    MTK_DEVICE* devContext = MtkGetDeviceContext(device);

    TraceEntry();

    NTSTATUS status = STATUS_SUCCESS;
    TraceExitResult(status);
    return status;
}

_Use_decl_annotations_
void
EvtScanCompleteWorkItem(
    _In_ WDFWORKITEM WorkItem
)
{
    WDFDEVICE wdfDevice = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    MTK_DEVICE* dev = MtkGetDeviceContext(wdfDevice);

    // First: announce the fabricated BSS list via TLV-encoded indication.
    WdiEmitBssEntryList(wdfDevice);

    // Then: tell the OS the scan task is complete (header-only payload).
    WDFMEMORY memory = NULL;
    PWDI_MESSAGE_HEADER hdr = NULL;
    NTSTATUS status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        'IFiW',
        sizeof(WDI_MESSAGE_HEADER),
        &memory,
        (PVOID*)&hdr);
    if (!NT_SUCCESS(status)) {
        return;
    }

    RtlZeroMemory(hdr, sizeof(*hdr));
    hdr->PortId = dev->ActiveScanPortId;
    hdr->TransactionId = dev->ActiveScanTransactionId;
    hdr->Status = STATUS_SUCCESS;

    WifiDeviceReceiveIndication(wdfDevice, WDI_INDICATION_SCAN_COMPLETE, memory);
    WdfObjectDelete(memory);
}