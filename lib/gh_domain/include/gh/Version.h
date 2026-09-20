// SPDX-License-Identifier: MIT
#pragma once

/**
 * Wersja firmware. W CI nadpisywana tagiem gita przez `-D GH_FIRMWARE_VERSION`
 * (patrz .github/workflows/release.yml), lokalnie ma wartosc deweloperska.
 */
#ifndef GH_FIRMWARE_VERSION
#define GH_FIRMWARE_VERSION "0.0.0-dev"
#endif

namespace gh {
const char* firmwareVersion();
}  // namespace gh
