# WDI Contracts

Subtle WDI semantics that are not documented in `dot11wdi.h`, but
which you will absolutely need to know. Each section is one rule
learned the hard way; the cost to discover the first time is in
parentheses.

---

## 1. Association/Disassociation/Deauth indications carry the MMPDU **body** only (≈ 2 weeks)

The WDI indication TLVs for `AssociationRequestFrame`,
`AssociationResponseFrame`, `DisassociationFrame`, and `DeauthFrame`
fields carry the **MMPDU body** — the bytes **after** the 24-byte
802.11 MAC header. **Not** the on-air frame. **Not** the
FC/Duration/Addresses/Seq prefix. **Body bytes only.**

### What goes wrong if you pass the full frame

- The OS MSM parses Capability, Status, AID, and IEs starting at
  offset 0 of the supplied buffer.
- With the 24-byte header still present, every field is mis-aligned
  by 24 bytes. The MSM never finds the RSN IE.
- For an RSNA connect, that silently means the OS skips the 4-way
  handshake. No PTK derivation. No data path.
- **There is no error indication.** wlansvc shows "Connected" but
  no traffic flows.

### Correct usage

Use `wdi_mmpdu.h`'s `WdiMmpduStripHeader()` to slice a captured
on-air frame to the body before assigning into the WDI field:

```cpp
const UINT8* body;
ULONG bodyLen;
WdiMmpduStripHeader(onAirFrame, frameLen, &body, &bodyLen);
container.AssociationResponseFrame.SimpleAssign(
    (UINT8*)body, bodyLen);
container.Optional.AssociationResponseFrame_IsPresent = TRUE;
```

Or use the high-level `WdiEmitAssociationResult()` /
`WdiEmitDisassociation()` helpers in `wdi_connect.h` — they take
body buffers as parameters and `NT_ASSERT` the shape via
`WdiMmpduAssertIsBody()` in debug builds.

### Cross-check

Verified by direct comparison with the Microsoft WDI sample, whose
RX path uses `MMPDU_BODY = buf + 24`, and by kd-tracing through
nwifi's IE walker.

---

## 2. TransactionId discipline on indications (≈ 1 week)

Every indication carries a header with `(PortId, TransactionId)`.
The OS `Task::OnDeviceIndicationArrived` consumes any indication
whose `(PortId, TxnId)` matches an outstanding Task as **the Task's
completion** — and **discards the payload.**

### The rule

| Indication role                    | TransactionId |
|------------------------------------|---------------|
| Task-completion (SCAN_COMPLETE, DOT11_RESET_COMPLETE, SET_RADIO_STATE_COMPLETE, CONNECT_COMPLETE, ...) | **MUST match the Task's TxnId** |
| Unsolicited (BSS_ENTRY_LIST, RADIO_STATUS push, ASSOCIATION_RESULT, DISASSOCIATION from AP-initiated kick, ...) | **MUST be 0** |

### Why this is treacherous

`BSS_ENTRY_LIST` looks like it should match the scan task — but if
you put the scan's TxnId on it, the Task hijacks the indication as
its completion and the BSS payload is **silently dropped.** Scan
returns to wlansvc with zero results.

### Correct usage

`wdi_tlv.h` has two helpers that encode the rule in the name:

- `WdiIndicateTaskComplete(...)` — header-only, takes Task's TxnId.
- `WdiIndicateTlv(...)` — TLV-bodied, caller passes TxnId; use `0`
  for unsolicited.

Don't construct indication headers by hand. The helpers exist so
that "TxnId for unsolicited?" isn't a question on the call site.

---

## 3. AssociationResult mandatory fields (≈ 1 day)

`WDI_INDICATION_ASSOCIATION_RESULT_LIST_PARAMETERS` contains an
`ArrayOfElements<WDI_ASSOCIATION_RESULT_CONTAINER>`. Each container
has many fields that look optional but are actually required by
`GenerateWdiIndicationAssociationResultFromIhv()`. Leave them empty
and it returns `0xC0010015` with `bufLen=0`; no indication fires;
the OS upcalls assoc-failure with `0xC000023C` (network
unreachable).

The minimal set the generator accepts:

- `BSSID.Address` — peer BSSID.
- `AssociationResultParameters.AssociationStatus` —
  `WDI_ASSOC_STATUS_SUCCESS` or a failure code.
- `AssociationResultParameters.StatusCode` — the 802.11 status code.
- `AssociationResultParameters.AuthAlgorithm` — the matched
  `WDI_AUTH_ALGO_*`.
- `AssociationResultParameters.Unicast / MulticastData /
  MulticastMgmt CipherAlgorithm` — even if `_NONE`.
- `AssociationResultParameters.PortAuthorized` — `TRUE` for an
  Open BSS that's ready to push data; `FALSE` for RSNA-pending
  (4-way handshake hasn't completed yet).
- `AssociationResultParameters.BandID`.
- `ActivePhyTypeList` — mandatory. Empty list = generator failure.

`wdi_connect.cpp::WdiEmitAssociationResult` populates these
defaults; backends only have to vary what's chip-specific.

---

## 4. Setting `PortAuthorized=TRUE` before the data path works = BSOD (≈ 4 hours)

`PortAuthorized=TRUE` tells the OS the link is ready to push data.
The OS immediately starts a DHCP exchange through the TX queue. If
`EvtTxQueueAdvance` is a no-op (does not consume packets and does
not advance the ring), NetAdapterCx will eventually BSOD when its
ring-state invariants are violated.

Two ways out:

1. Report `PortAuthorized=FALSE` + `AssociationStatus` failure (or
   `RSNA_PSK` + clear-port, which has the OS wait for a 4-way that
   never comes). The fake chip backend in this skeleton does the
   former — clean failure, no TX exercised.
2. Implement a real `EvtTxQueueAdvance` that at minimum drains
   packets to the bit bucket (set `Ignore=1`, advance `BeginIndex`).

A real chip backend needs (2) before it can return Connect success.

---

## 5. Inbox-driver-outranks-test-signed-driver gotcha (≈ 1 hour)

When you `pnputil /add-driver wificx-skl.inf /install`, Windows may
still bind the device to the **inbox** driver (Realtek WHQL, Intel
WHQL, etc.) because the WHQL signing rank is higher than your test
signature.

Symptom: `pnputil /enum-devices` shows the device bound to the
manufacturer's WHQL driver, not yours. Your `.sys` is loaded but
not driving the device.

Fix: uninstall the inbox driver first.

```
pnputil /delete-driver oem##.inf /uninstall /force
pnputil /scan-devices
```

See `docs/deploy.md` for the full sequence.
