#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
    constexpr const char *VERSION = "2.3.2";
    constexpr const char *BUILD_DATE = "2025-10-30";
    // UNIQUE_ID must equal controller_id and be globally unique per controller
    constexpr const char *UNIQUE_ID = "power_control_lower_right";
    constexpr const char *DESCRIPTION = "Power distribution controller for lower right zone - 24 relay outputs. Reports actual physical relay states on startup for state accuracy after power outages.";
}

#endif // FIRMWARE_METADATA_H
