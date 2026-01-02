#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
    constexpr const char *VERSION = "2.3.2";
    constexpr const char *BUILD_DATE = "2025-11-06";
    // UNIQUE_ID must equal controller_id and be globally unique per controller
    constexpr const char *UNIQUE_ID = "pilot_light";
    constexpr const char *DESCRIPTION = "DEBUG: Testing connection WITHOUT Will message to isolate rc=-2 error.";
}

#endif // FIRMWARE_METADATA_H
