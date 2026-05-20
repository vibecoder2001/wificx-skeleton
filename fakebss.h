#pragma once

#define FAKE_BSS_COUNT 4
#define FAKE_BSS_MAX_IE_SIZE 128
#define FAKE_BSS_BEACON_FIXED_LEN 12   // Timestamp(8) + BeaconInterval(2) + Capability(2)
#define FAKE_BSS_MAX_BEACON_SIZE (FAKE_BSS_BEACON_FIXED_LEN + FAKE_BSS_MAX_IE_SIZE)

typedef struct _FAKE_BSS_ENTRY {
    UCHAR  Bssid[6];
    UCHAR  SsidLength;
    UCHAR  Ssid[32];
    UINT32 ChannelCenterFrequencyMhz;
    INT32  RssiDbm;
    UINT16 BeaconIntervalTU;
    UINT16 CapabilityInfo;     // 802.11 capability bits (ESS=0x0001, Privacy=0x0010)
    BOOLEAN UseRsn;            // True => append RSN IE for WPA2-PSK/CCMP
    UCHAR  IeBuffer[FAKE_BSS_MAX_IE_SIZE];
    SIZE_T IeLength;           // populated on first FakeBssGetTable() call
    UCHAR  BeaconBuffer[FAKE_BSS_MAX_BEACON_SIZE]; // 12B fixed params + IEs
    SIZE_T BeaconLength;
} FAKE_BSS_ENTRY;

// Populates IeBuffer / BeaconBuffer on first call.
const FAKE_BSS_ENTRY* FakeBssGetTable(_Out_ SIZE_T* count);
