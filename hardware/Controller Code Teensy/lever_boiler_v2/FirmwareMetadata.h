#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
    constexpr const char *VERSION = "2.3.5";
    constexpr const char *BUILD_DATE = "2025-11-18";
    // UNIQUE_ID must equal controller_id and be globally unique per controller
    constexpr const char *UNIQUE_ID = "lever_boiler";
    constexpr const char *DESCRIPTION = "STATELESS EXECUTOR - Lever Boiler/Stairs maglocks, lever LEDs, photocell sensors, IR gun readers (alternating pins), and Newell Post light with stepper motor and proximity sensors. Sentient makes all decisions.";
}

#endif // FIRMWARE_METADATA_H
