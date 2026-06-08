#pragma once

#include "wdi_chip_ops.h"

//
// Returns the fake-chip ops vtable. Used by MtkInitializeHardware
// when no real-HW backend is wired up — enough to load on Hyper-V and
// keep wlansvc happy with synthetic scan results.
//
// A real-HW fork should add its own ChipXxxGetOps() and swap this
// call in device.cpp.
//
const WDI_CHIP_OPS* ChipFakeGetOps(void);
