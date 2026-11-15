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
// Both the boot check and the OTA verify step (ElegantOTA.cpp, after Update.end() and before the session is
// declared verified) go through checkAppImageValid() in main.cpp; a refusal there also points the boot
// pointer back at the Safeboot partition.
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

// Raw-bytes variant, used by Safeboot for both the boot check and the OTA verify step: `head` is the first
// APP_CHECK_HEAD_LEN bytes of the partition (image header 24 + first segment header 8 + the 4 bytes at 0x20
// where esp_app_desc_t.magic_word sits). The magic word is compared here, on the bytes just read from
// flash, so no IDF helper stands between the flash and the verdict. Reason order as checkAppImage();
// a short or non-E9 head is BadHeader.
constexpr size_t APP_CHECK_HEAD_LEN = 36;

inline AppCheck checkAppImageHead(const uint8_t *head, size_t len, uint16_t expectedChipId)
{
    if (!head || len < APP_CHECK_HEAD_LEN || head[0] != 0xE9) return AppCheck::BadHeader;
    const uint16_t chip = (uint16_t)(head[12] | (head[13] << 8)); // esp_image_header_t.chip_id
    const uint32_t magic = (uint32_t)head[32] | ((uint32_t)head[33] << 8) | ((uint32_t)head[34] << 16) |
                           ((uint32_t)head[35] << 24);
    return checkAppImage(chip, expectedChipId, head[1], true, magic, nullptr);
}

} // namespace safeboot
