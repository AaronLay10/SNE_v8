#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
  constexpr const char *VERSION = "2.3.8";
  constexpr const char *BUILD_DATE = "2025-11-18";
  // UNIQUE_ID must equal controller_id and be globally unique per controller
  constexpr const char *UNIQUE_ID = "boiler_room_subpanel";
  constexpr const char *DESCRIPTION = "STATELESS EXECUTOR - TV lift, fog machines, IR sensor, barrel maglock, 3 study door maglocks, 60 gauge LEDs. Sentient makes all decisions.";
}

#endif // FIRMWARE_METADATA_H
