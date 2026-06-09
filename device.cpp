#include "precomp.h"
#include <wlan/2.0/TlvGeneratorParser.hpp>

#include "trace.h"
#include "device.h"
#include "adapter.h"
#include "wdihandlers.h"
#include "chip_fake.h"
#include "wdi_tlv.h"

EVT_WDF_WORKITEM EvtScanCompleteWorkItem;
EVT_WDF_WORKITEM EvtResetCompleteWorkItem;
EVT_WDF_WORKITEM EvtRadioStatusWorkItem;
EVT_WDF_WORKITEM EvtConnectCompleteWorkItem;
EVT_WDF_WORKITEM EvtDisconnectCompleteWorkItem;

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
    deviceCapabilities.NumRadios = 1;

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

    // Common 802.11b/g rates in 500kbps units; nwifi!QueryRawData walks
    // [DOT11_PHY_ATTRIBUTES+0x48] (NumDataRateMappingEntries) and rejects
    // 0 (computes -1, fails the (count-1)<=0x7D check). Must be >= 1.
    static WIFI_PHY_INFO phyInfo[1] = {};
    phyInfo[0].PhyType = WDI_PHY_TYPE_HT;
    phyInfo[0].NumberDataRateEntries = 12;
    static const UINT16 rateValues[12] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    for (SIZE_T i = 0; i < 12; ++i) {
        phyInfo[0].DataRateList[i].DataRateFlag = 0;
        phyInfo[0].DataRateList[i].DataRateValue = rateValues[i];
    }

    WIFI_PHY_CAPABILITIES phyCapabilities = { 0 };
    phyCapabilities.Size = sizeof(phyCapabilities);
    phyCapabilities.NumPhyTypes = ARRAYSIZE(phyInfo);
    phyCapabilities.PhyInfoList = phyInfo;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetPhyCapabilities(pDevice->FxDevice, &phyCapabilities));
    DbgPrint("MTK: WifiDeviceSetPhyCapabilities OK\n");

    // vwififlt!FilterAttach REQUIRES the NDIS_MINIPORT_ADAPTER_NATIVE_802_11_
    // ATTRIBUTES struct to have:
    //   - OpModeCapability & 0x20 (WFD_GROUP_OWNER) set
    //   - VWiFiAttributes non-NULL
    //   - ExtAPAttributes non-NULL
    // WiFiCx auto-derives all of these from WifiDeviceSetWiFiDirectCapabilities.
    // ExtAPAttributes specifically is only allocated when ConcurrentGOCount > 0
    // (which flips OpMode bit 0x20). VWiFi is allocated regardless.
    //
    // Also need fields populated enough for nwifi!WFDValidateInitAttributes
    // to pass (mirrors the station-caps shape).
    WIFI_WIFIDIRECT_CAPABILITIES wifiDirectCapabilities;
    WIFI_WIFIDIRECT_CAPABILITIES_INIT(&wifiDirectCapabilities);
    wifiDirectCapabilities.WFDRoleCount = 1;
    wifiDirectCapabilities.ConcurrentGOCount = 1;        // flips bit 0x20 → ExtAP alloc
    wifiDirectCapabilities.ConcurrentClientCount = 1;
    wifiDirectCapabilities.ScanSSIDListSize = 32;
    wifiDirectCapabilities.DesiredSSIDListSize = 32;
    wifiDirectCapabilities.PrivacyExemptionListSize = 16;
    wifiDirectCapabilities.AssociationTableSize = 16;
    wifiDirectCapabilities.DefaultKeyTableSize = 16;
    wifiDirectCapabilities.WEPKeyValueMaxLength = 16;
    wifiDirectCapabilities.NumSupportedUnicastAlgorithms = 1;
    wifiDirectCapabilities.UnicastAlgorithms = &cipherPairs;
    wifiDirectCapabilities.NumSupportedMulticastDataAlgorithms = 1;
    wifiDirectCapabilities.MulticastDataAlgorithms = &cipherPairs;
    wifiDirectCapabilities.GOClientTableSize = 8;
    wifiDirectCapabilities.MaxVendorSpecificExtensionIESize = 256;
    // CRITICAL for nwifi!WFDValidateInitAttributes: it requires the auto-
    // allocated WFDAttributes struct to have [+0x20] != 0 and [+0x28] != 0.
    // wificx writes those from NumInterfaceAddresses and InterfaceAddressList
    // (WIFI_WIFIDIRECT_CAPABILITIES offset 0x68/0x70) — and ONLY from there.
    // Provide at least one MAC so the validator passes.
    static WDI_MAC_ADDRESS wfdAddrs[1] = {{{0x02, 0x57, 0x49, 0x46, 0x44, 0x01}}};
    wifiDirectCapabilities.NumInterfaceAddresses = 1;
    wifiDirectCapabilities.InterfaceAddressList = wfdAddrs;
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

    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtResetCompleteWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = pDevice->FxDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfWorkItemCreate(&wiConfig, &wiAttr, &pDevice->ResetCompleteWorkItem));

    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtRadioStatusWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = pDevice->FxDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfWorkItemCreate(&wiConfig, &wiAttr, &pDevice->RadioStatusWorkItem));

    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtConnectCompleteWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = pDevice->FxDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfWorkItemCreate(&wiConfig, &wiAttr, &pDevice->ConnectCompleteWorkItem));

    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtDisconnectCompleteWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = pDevice->FxDevice;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdfWorkItemCreate(&wiConfig, &wiAttr, &pDevice->DisconnectCompleteWorkItem));

    // Radio defaults to ON so the initial SET_RADIO_STATE(TRUE) during
    // adapter bringup is a no-op transition and skips the indication.
    pDevice->RadioOn = TRUE;

    pDevice->OsWdiVersion = WifiDeviceGetOsWdiVersion(pDevice->FxDevice);
    DbgPrint("MTK: OS WDI version = 0x%x\n", pDevice->OsWdiVersion);

    // Shared scan cache the chip backend fills and the WDI layer
    // drains. Size 32 is enough for the synthetic backend and gives
    // real backends headroom for a typical home environment.
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WdiScanCacheCreate(32, &pDevice->ScanCache));

    // Install the chip backend. Real-HW forks swap ChipFakeGetOps()
    // for their own ChipXxxGetOps() here.
    pDevice->ChipOps = ChipFakeGetOps();
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        pDevice->ChipOps->Init(pDevice->FxDevice, &pDevice->ChipCtx));
    DbgPrint("MTK: chip backend (fake) initialized\n");

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

    if (devContext->ChipOps != NULL && devContext->ChipOps->Deinit != NULL) {
        devContext->ChipOps->Deinit(device, devContext->ChipCtx);
    }

    if (devContext->ScanCache) {
        WdiScanCacheDestroy(devContext->ScanCache);
        devContext->ScanCache = NULL;
    }

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

    // Drain queue — one BSS list + SCAN_COMPLETE per queued scan.
    // The SCAN WIFIREQUEST is completed AFTER both indications so that
    // wlansvc sees the BSS entries while the scan task is still pending,
    // matching the Realtek WDI sample's wdiext_IndicateBSSEntry pattern.
    while (dev->ScanQueueHead != dev->ScanQueueTail) {
        UINT idx = dev->ScanQueueHead & 0xF;
        UINT16 portId   = dev->ScanQueue[idx].PortId;
        UINT32 txnId    = dev->ScanQueue[idx].TxnId;
        WIFIREQUEST req = dev->ScanQueue[idx].Request;
        TraceLoggingWrite(WiFiCxSampleTraceProvider, "ScanCompleteDraining",
            TraceLoggingUInt16(portId, "PortId"),
            TraceLoggingUInt32(txnId, "Txn"));

        // Set the active fields so WdiEmitBssEntryList uses the right port/txn.
        dev->ActiveScanPortId = portId;
        dev->ActiveScanTransactionId = txnId;

        // Ask the backend to run its scan and push results into the
        // shared cache. Synchronous from our perspective: when this
        // returns we drain whatever the backend put in.
        if (dev->ChipOps && dev->ChipOps->StartScan) {
            (void)dev->ChipOps->StartScan(wdfDevice, dev->ChipCtx);
        }

        // BSS_ENTRY_LIST first, then SCAN_COMPLETE — matches the
        // Realtek WDI sample order. The Task base handler routes BSS
        // into the list manager while the Task is outstanding.
        WdiEmitBssEntryList(wdfDevice);

        // Task-completion indication: TxnId matches the SCAN Task.
        if (!NT_SUCCESS(WdiIndicateTaskComplete(
                wdfDevice, WDI_INDICATION_SCAN_COMPLETE, portId, txnId))) {
            break;
        }

        if (req != NULL) {
            WifiRequestComplete(req, STATUS_SUCCESS, sizeof(WDI_MESSAGE_HEADER));
        }

        InterlockedIncrement(&dev->ScanQueueHead);
    }
}

_Use_decl_annotations_
void
EvtResetCompleteWorkItem(
    _In_ WDFWORKITEM WorkItem
)
{
    WDFDEVICE wdfDevice = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    MTK_DEVICE* dev = MtkGetDeviceContext(wdfDevice);

    // Drain queue — fire one indication per queued reset.
    while (dev->ResetQueueHead != dev->ResetQueueTail) {
        UINT idx = dev->ResetQueueHead & 0xF;
        UINT16 portId = dev->ResetQueue[idx].PortId;
        UINT32 txnId  = dev->ResetQueue[idx].TxnId;
        TraceLoggingWrite(WiFiCxSampleTraceProvider, "ResetCompleteFiring",
            TraceLoggingUInt16(portId, "PortId"),
            TraceLoggingUInt32(txnId, "Txn"));

        if (!NT_SUCCESS(WdiIndicateTaskComplete(
                wdfDevice, WDI_INDICATION_DOT11_RESET_COMPLETE,
                portId, txnId))) {
            break;
        }

        InterlockedIncrement(&dev->ResetQueueHead);
    }
}

_Use_decl_annotations_
void
EvtRadioStatusWorkItem(
    _In_ WDFWORKITEM WorkItem
)
{
    WDFDEVICE wdfDevice = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    MTK_DEVICE* dev = MtkGetDeviceContext(wdfDevice);

    while (dev->RadioQueueHead != dev->RadioQueueTail) {
        UINT idx = dev->RadioQueueHead & 0xF;
        UINT16  portId = dev->RadioQueue[idx].PortId;
        UINT32  txnId  = dev->RadioQueue[idx].TxnId;
        BOOLEAN softOn = dev->RadioQueue[idx].SoftOn;
        TraceLoggingWrite(WiFiCxSampleTraceProvider, "RadioCompleteFiring",
            TraceLoggingUInt16(portId, "PortId"),
            TraceLoggingUInt32(txnId, "Txn"),
            TraceLoggingUInt8(softOn, "SoftOn"));

        // 1) Task-completion indication for the SET_RADIO_STATE Task.
        (void)WdiIndicateTaskComplete(
            wdfDevice, WDI_INDICATION_SET_RADIO_STATE_COMPLETE,
            portId, txnId);

        // 2) Unsolicited RADIO_STATUS push so the OS updates its
        //    state tracking. PortId=0xFFFF (all-ports), TxnId=0.
        WDI_INDICATION_RADIO_STATUS_PARAMETERS params;
        params.RadioState.HardwareState = TRUE;
        params.RadioState.SoftwareState = softOn;

        TLV_CONTEXT ctx = WdiTlvContext(dev->OsWdiVersion);
        ULONG bufLen = 0;
        UINT8* genBuf = NULL;
        NDIS_STATUS gs = GenerateWdiIndicationRadioStatusFromIhv(
            &params, sizeof(WDI_MESSAGE_HEADER), &ctx, &bufLen, &genBuf);
        if (gs == NDIS_STATUS_SUCCESS && genBuf != NULL) {
            (void)WdiIndicateTlv(wdfDevice, WDI_INDICATION_RADIO_STATUS,
                                 0xFFFF, 0, &genBuf, bufLen);
        } else if (genBuf) {
            FreeGenerated(genBuf);
        }

        InterlockedIncrement(&dev->RadioQueueHead);
    }
}

_Use_decl_annotations_
void
EvtConnectCompleteWorkItem(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE wdfDevice = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    MTK_DEVICE* dev = MtkGetDeviceContext(wdfDevice);

    while (dev->ConnectQueueHead != dev->ConnectQueueTail) {
        UINT idx = dev->ConnectQueueHead & 0x7;
        UINT16 portId = dev->ConnectQueue[idx].PortId;
        UINT32 txnId  = dev->ConnectQueue[idx].TxnId;
        WDI_CONNECT_TARGET target = dev->ConnectQueue[idx].Target;

        // Park identity so WdiEmitAssociationResult finds the right port.
        dev->ConnectPortId = portId;
        dev->ConnectTxnId  = txnId;

        NTSTATUS cs = STATUS_DEVICE_NOT_READY;
        if (dev->ChipOps && dev->ChipOps->Connect) {
            cs = dev->ChipOps->Connect(wdfDevice, dev->ChipCtx, &target);
        }

        TraceLoggingWrite(WiFiCxSampleTraceProvider, "ConnectCompleteFiring",
            TraceLoggingUInt16(portId, "PortId"),
            TraceLoggingUInt32(txnId, "Txn"),
            TraceLoggingHexUInt32((UINT32)cs, "ChipStatus"));

        // CONNECT_COMPLETE is the task-completion indication. Matching
        // TxnId so the OS Task machinery completes the CONNECT Task.
        // We always fire it (success or failure); the ASSOCIATION_RESULT
        // indication that the chip backend emitted on success carries
        // the actual outcome.
        (void)WdiIndicateTaskComplete(wdfDevice,
            WDI_INDICATION_CONNECT_COMPLETE, portId, txnId);

        InterlockedIncrement(&dev->ConnectQueueHead);
    }
}

_Use_decl_annotations_
void
EvtDisconnectCompleteWorkItem(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE wdfDevice = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    MTK_DEVICE* dev = MtkGetDeviceContext(wdfDevice);

    while (dev->DisconnectQueueHead != dev->DisconnectQueueTail) {
        UINT idx = dev->DisconnectQueueHead & 0x7;
        UINT16 portId = dev->DisconnectQueue[idx].PortId;
        UINT32 txnId  = dev->DisconnectQueue[idx].TxnId;

        if (dev->ChipOps && dev->ChipOps->Disconnect) {
            (void)dev->ChipOps->Disconnect(wdfDevice, dev->ChipCtx);
        }

        (void)WdiIndicateTaskComplete(wdfDevice,
            WDI_INDICATION_DISCONNECT_COMPLETE, portId, txnId);

        // Clear connection identity so a subsequent emit can't accidentally
        // address a stale port.
        dev->ConnectPortId = 0;
        dev->ConnectTxnId  = 0;

        InterlockedIncrement(&dev->DisconnectQueueHead);
    }
}