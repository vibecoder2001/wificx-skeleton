#pragma once

// Top-level dispatch entry point invoked from EvtWiFiDeviceSendCommand.
// Either completes the request synchronously or arms async completion.
void
WdiDispatchCommand(
    _In_ WDFDEVICE WdfDevice,
    _In_ WIFIREQUEST Request);

// Send a WDI_INDICATION_BSS_ENTRY_LIST built from the fake BSS table.
// Called from the scan-complete workitem before SCAN_COMPLETE itself.
void
WdiEmitBssEntryList(
    _In_ WDFDEVICE WdfDevice);
