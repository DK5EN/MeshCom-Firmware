// Pure decision logic for the Safeboot app-image plausibility check (SB-06).
//
// esp_image_verify() (checksum + structure) stays the first gate in
// checkAppImageValid(); this adds three cheap identity checks on top, using
// only fields already in flash: header chip id, header segment count and the
// esp_app_desc_t magic word. No SHA-256 by design. No project-name check
// (operator 2026-11-07): every Arduino-ESP32 build carries the generic
// "arduino-lib-builder", and a framework switch would make every old Safeboot
// refuse every new app.
//
// No ESP-IDF include: the caller passes plain integers/strings, so the
// function builds under `pio test -e native_safeboot`.
//
// Log contract: a rejection is appended to the existing line as
//   [SAFEBOOT];app;image;invalid;rc;<n>;why;<hdr|chip|segs|desc>
// The "valid" line is unchanged.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace safeboot {

// Max segment count of an ESP image (ESP_IMAGE_MAX_SEGMENTS in IDF).
constexpr uint8_t APP_CHECK_MAX_SEGMENTS = 16;
// esp_app_desc_t.magic_word (ESP_APP_DESC_MAGIC_WORD in IDF).
constexpr uint32_t APP_DESC_MAGIC = 0xABCD5432u;
enum class AppCheck : uint8_t {
    Ok = 0,
    ChipMismatch,
    BadSegmentCount,
    BadDesc,  // desc missing or wrong magic word
    BadHeader, // image header could not be read from flash
};

inline const char *appCheckWhy(AppCheck c)
{
    switch (c) {
    case AppCheck::Ok:              return "";
    case AppCheck::ChipMismatch:    return "chip";
    case AppCheck::BadSegmentCount: return "segs";
    case AppCheck::BadDesc:         return "desc";
    case AppCheck::BadHeader:       return "hdr";
    }
    return "";
}

// descValid: false when the desc could not be read at all.
// projectName: accepted for call-site symmetry, deliberately not checked.
inline AppCheck checkAppImage(uint16_t hdrChipId, uint16_t expectedChipId, uint8_t segmentCount,
                              bool descValid, uint32_t descMagic, const char *projectName)
{
    if (hdrChipId != expectedChipId) return AppCheck::ChipMismatch;
    if (segmentCount == 0 || segmentCount > APP_CHECK_MAX_SEGMENTS) return AppCheck::BadSegmentCount;
    (void)projectName;
    if (!descValid || descMagic != APP_DESC_MAGIC) return AppCheck::BadDesc;
    return AppCheck::Ok;
}

} // namespace safeboot
