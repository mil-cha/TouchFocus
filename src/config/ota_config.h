// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

namespace touchfocus::config {

constexpr const char *OTA_HOSTNAME = "TouchFocus";

#if __has_include("ota_private.h")
#include "ota_private.h"
#endif

#ifdef TOUCHFOCUS_OTA_PASSWORD
constexpr const char *OTA_PASSWORD = TOUCHFOCUS_OTA_PASSWORD;
#else
constexpr const char *OTA_PASSWORD = "";
#endif

}  // namespace touchfocus::config
