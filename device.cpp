#include "precomp.h"

#include "trace.h"
#include "device.h"
#include "adapter.h"

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

    DOT11_AUTH_CIPHER_PAIR cipherPairs;
    cipherPairs.AuthAlgoId = DOT11_AUTH_ALGO_80211_OPEN;
    cipherPairs.CipherAlgoId = DOT11_CIPHER_ALGO_NONE;

    WIFI_STATION_CAPABILITIES stationCapabilities = { 0 };
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

    static const WDI_CHANNEL_INFO ch2g[11] = {
        { 1,  2412, 0, 0, 20, 0 }, { 2,  2417, 0, 0, 20, 0 },
        { 3,  2422, 0, 0, 20, 0 }, { 4,  2427, 0, 0, 20, 0 },
        { 5,  2432, 0, 0, 20, 0 }, { 6,  2437, 0, 0, 20, 0 },
        { 7,  2442, 0, 0, 20, 0 }, { 8,  2447, 0, 0, 20, 0 },
        { 9,  2452, 0, 0, 20, 0 }, { 10, 2457, 0, 0, 20, 0 },
        { 11, 2462, 0, 0, 20, 0 },
    };
    static const WDI_CHANNEL_INFO ch5g[4] = {
        { 36, 5180, 0, 0, 20, 0 }, { 40, 5200, 0, 0, 20, 0 },
        { 44, 5220, 0, 0, 20, 0 }, { 48, 5240, 0, 0, 20, 0 },
    };
    static const UINT8 widths[1] = { 20 };
    static const DOT11_PHY_TYPE phyTypes[1] = { dot11_phy_type_ht };

    WIFI_BAND_INFO bandInfo[2] = { 0 };
    bandInfo[0].BandID = WDI_BAND_ID_2400;
    bandInfo[0].BandState = TRUE;
    bandInfo[0].NumValidPhyTypes = ARRAYSIZE(phyTypes);
    bandInfo[0].ValidPhyTypeList = phyTypes;
    bandInfo[0].NumValidChannelTypes = ARRAYSIZE(ch2g);
    bandInfo[0].ValidChannelTypeList = ch2g;
    bandInfo[0].NumChannelWidths = ARRAYSIZE(widths);
    bandInfo[0].ChannelWidthList = widths;

    bandInfo[1].BandID = WDI_BAND_ID_5000;
    bandInfo[1].BandState = TRUE;
    bandInfo[1].NumValidPhyTypes = ARRAYSIZE(phyTypes);
    bandInfo[1].ValidPhyTypeList = phyTypes;
    bandInfo[1].NumValidChannelTypes = ARRAYSIZE(ch5g);
    bandInfo[1].ValidChannelTypeList = ch5g;
    bandInfo[1].NumChannelWidths = ARRAYSIZE(widths);
    bandInfo[1].ChannelWidthList = widths;

    WIFI_BAND_CAPABILITIES bandCapabilities = { 0 };
    bandCapabilities.Size = sizeof(bandCapabilities);
    bandCapabilities.NumBands = 2;
    bandCapabilities.BandInfoList = bandInfo;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetBandCapabilities(pDevice->FxDevice, &bandCapabilities));

    static WIFI_PHY_INFO phyInfo[1] = { 0 };
    phyInfo[0].PhyType = dot11_phy_type_ht;
    phyInfo[0].BandID = WDI_BAND_ID_2400;
    phyInfo[0].ShortPreambleOptionImplemented = TRUE;
    phyInfo[0].ShortSlotTimeOptionImplemented = TRUE;

    WIFI_PHY_CAPABILITIES phyCapabilities = { 0 };
    phyCapabilities.Size = sizeof(phyCapabilities);
    phyCapabilities.NumPhyTypes = ARRAYSIZE(phyInfo);
    phyCapabilities.PhyInfoList = phyInfo;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetPhyCapabilities(pDevice->FxDevice, &phyCapabilities));

    WIFI_WIFIDIRECT_CAPABILITIES wifiDirectCapabilities = { 0 };
    wifiDirectCapabilities.Size = sizeof(wifiDirectCapabilities);
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        WifiDeviceSetWiFiDirectCapabilities(pDevice->FxDevice, &wifiDirectCapabilities));

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