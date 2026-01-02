#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

namespace firmware
{
    constexpr const char *VERSION = "2.3.2";
    constexpr const char *BUILD_DATE = "2025-11-18";
    constexpr const char *UNIQUE_ID = "clock";
    constexpr const char *DESCRIPTION = "⚠️ LIBRARY UPGRADED TO v2.3.0 - GAME LOGIC EXTRACTION REQUIRED. 3 DM542 stepper motors (hour/minute/gears), 8 resistor readers, 3 rotary encoders, 2 maglocks, 2 actuators, 4 WS2812B strips (400 LEDs), fog/blacklight/laser. Contains state machine (IDLE/PILASTER/LEVER/CRANK/OPERATOR/FINALE) that must be moved to Sentient backend.";
}

#endif // FIRMWARE_METADATA_H
