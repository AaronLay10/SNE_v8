#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
    constexpr const char *VERSION = "2.3.2";
    constexpr const char *BUILD_DATE = "2025-10-29";
    // UNIQUE_ID must equal controller_id and be globally unique per controller
    constexpr const char *UNIQUE_ID = "gauge_6_leds";
    constexpr const char *DESCRIPTION = "STATELESS EXECUTOR - 1 pressure gauge, 7 photoresistors, 219 ceiling LEDs, 7 indicator LEDs. Sentient makes all decisions.";
}

#endif // FIRMWARE_METADATA_H
