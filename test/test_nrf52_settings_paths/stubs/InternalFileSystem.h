// Stand-in for the nRF52 BSP's InternalFileSystem.h: declares the InternalFS
// instance nrf52_flash.cpp and settings_store_nrf52.cpp both operate on.
// Angle-bracket include (`#include <InternalFileSystem.h>` in both real
// sources), shadowed by plain -I ordering -- see Adafruit_LittleFS.h for the
// fake filesystem this instance is backed by.
#pragma once

#ifndef NATIVE_BUILD
#error "test_nrf52_settings_paths/stubs/InternalFileSystem.h darf nur im nativen Testbuild verwendet werden"
#endif

#include "Adafruit_LittleFS.h"

// C++17 inline variable -- one definition shared by every translation unit
// that includes this header, so InternalFS is the SAME object (and thus the
// same underlying g_fake_fs-backed store) in nrf52_flash.cpp and
// settings_store_nrf52.cpp, exactly like the real singleton.
inline Adafruit_LittleFS InternalFS;
