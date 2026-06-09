# Deploying the skeleton

## Prerequisites on the target machine

```
bcdedit /set testsigning on
```

Reboot. Confirm "Test Mode" watermark appears in the lower-right
corner of the desktop. Without testsigning, Windows refuses to load
the test-signed `.sys`.

## First install (root-enumerated, Hyper-V)

The skeleton's INF supports both a root-enumerated software device
(default — for VMs / no real HW) and a PCI bind to a Realtek
RTL8852BE for ad-hoc testing.

```powershell
cd C:\path\to\drivers\wificx-skeleton
pnputil /add-driver wificx-skl.inf /install
```

The root-enumerated device appears under `Network adapters` as
"WiFiCX_SKL Sample Adapter". `wlansvc` picks it up immediately and
starts driving scans.

## The inbox-driver-outranks-test-signed-driver gotcha

When you point the INF at real PCI hardware (e.g. an MT7921 card)
and run `/install`, Windows may still bind the device to the
**inbox** driver (Realtek WHQL, MediaTek WHQL, etc.) because the
WHQL signing rank is higher than your test signature.

**Symptom:** `pnputil /enum-devices /class Net` shows the device
bound to the manufacturer's driver, not yours. Your `.sys` is
loaded by the kernel but not bound to the device.

**Fix:**

```powershell
# Find the inbox driver's oem##.inf:
pnputil /enum-drivers | Where-Object { $_ -match 'Realtek|MediaTek' }

# Remove it (replace ## with the actual number):
pnputil /delete-driver oem##.inf /uninstall /force

# Re-scan PnP so your driver picks up the orphaned device:
pnputil /scan-devices
```

Then your INF binds. Verify with:

```powershell
pnputil /enum-devices /class Net | Select-String 'WiFiCX_SKL'
```

## Uninstall

```powershell
pnputil /delete-driver wificx-skl.inf /uninstall /force
```

If anyone is still using the adapter (wlansvc keeps a handle), this
fails with `0xE000020B`. Stop dependent services first:

```powershell
Stop-Service wlansvc -Force
pnputil /delete-driver wificx-skl.inf /uninstall /force
Start-Service wlansvc
```

## Trace capture

The skeleton's ETW provider name is `WiFiCxSample.Trace.Provider`.
GUID: see `wificx-skl.rc` (look for `TraceLoggingRegister` or
`TRACELOGGING_DEFINE_PROVIDER` in the .cpp).

Quick capture:

```powershell
logman start wficx -p "{<guid>}" -ets
# ... reproduce ...
logman stop wficx -ets
tracefmt wficx.etl > wficx.txt
```

Or use Windows Performance Recorder with a custom profile pointed
at the provider.

## Updating an installed driver

Same `pnputil /add-driver ... /install` works as an upgrade — the
older OEM .inf gets replaced. If the device gets confused, a
`pnputil /scan-devices` is usually enough; rarely, a Device Manager
"Update driver" right-click is needed to force re-bind.

For iteration speed during development, the build script + a
`Z:\drivers\wificx-skeleton\` shared-folder drop + a one-liner in
the VM that uninstalls, copies, and reinstalls is the path of
least resistance.
