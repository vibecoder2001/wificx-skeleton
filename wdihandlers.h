#pragma once

#include "wdi_scan_cache.h"

// Top-level dispatch entry point invoked from EvtWiFiDeviceSendCommand.
// Either completes the request synchronously or arms async completion.
void
WdiDispatchCommand(
    _In_ WDFDEVICE WdfDevice,
    _In_ WIFIREQUEST Request);

// Send a WDI_INDICATION_BSS_ENTRY_LIST drained from the device's
// scan cache. Called from the scan-complete workitem before the
// SCAN_COMPLETE indication itself.
void
WdiEmitBssEntryList(
    _In_ WDFDEVICE WdfDevice);

// Accessor exposed to chip backends so they can push scan results
// into the device's cache during their StartScan op without needing
// to include device.h.
WDI_SCAN_CACHE*
WdiDeviceGetScanCache(_In_ WDFDEVICE WdfDevice);
