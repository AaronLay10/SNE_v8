# Raspberry Pi Auto-Registration - Complete

## ✅ Status: FULLY IMPLEMENTED

All components now use **snake_case** exclusively. Raspberry Pi controllers can self-register with Sentient Engine just like Teensy microcontrollers.

## 🎯 What Was Fixed

### Backend API (snake_case conversion)

- ✅ **controllers.js Joi schema** - All fields converted to snake_case
- ✅ **controllers.js POST route** - Variable destructuring uses snake_case
- ✅ **controllers.js INSERT query** - Parameters use snake_case variables
- ✅ **ControllerRegistration.ts** - Device-monitor sends snake_case to API
- ✅ Added new controller types: `video_manager`, `audio_manager`, `dmx_controller`, etc.

### Python Libraries

- ✅ **sentient_registration.py** - Auto-registration helper library
- ✅ **intro-player.py** - Updated to use registration system
- ✅ **test_registration.py** - Validation and testing script

### Documentation

- ✅ **README_AUTO_REGISTRATION.md** - Complete usage guide
- ✅ **RASPBERRY_PI_REGISTRATION_COMPLETE.md** - This summary

## 🚀 How It Works

### 1. Raspberry Pi Boots & Connects to MQTT

```python
from sentient_registration import SentientRegistration

registration = SentientRegistration(
    controller_id="intro_video_player",
    room_id="clockwork",
    controller_type="video_manager",
    friendly_name="Clockwork Intro Video Player"
)
```

### 2. Define Devices & Commands

```python
# Register device
registration.add_device(
    device_id="intro_tv",
    device_type="video_display",
    friendly_name="Clockwork Intro TV"
)

# Register command
registration.add_command(
    device_id="intro_tv",
    command_name="play_intro",
    friendly_name="Play Intro Video",
    duration_ms=150000
)
```

### 3. Publish Registration on MQTT Connect

```python
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        # Publish registration
        registration.publish_registration(client)

        # Subscribe to command topics
        client.subscribe(
            "paragon/clockwork/commands/intro_video_player/intro_tv/play_intro"
        )
```

### 4. Device-Monitor Receives & Processes

```
MQTT: sentient/system/register
  ↓
device-monitor: ControllerRegistration.handleRegistration()
  ↓
API: POST /api/sentient/controllers (snake_case)
  ↓
Database: INSERT INTO controllers (snake_case columns)
  ↓
Controller appears in system with status='inactive'
```

### 5. Operator Activates Controller

In Sentient UI:

1. Go to **Controllers** page
2. Find `intro_video_player`
3. Set status to `active`
4. Commands now available in Scene Timeline Editor!

## 📁 Files Changed

### Backend API

```
services/api/src/routes/controllers.js
  - Line 15-127: Joi schema (snake_case)
  - Line 294-356: POST route destructuring (snake_case)
  - Line 416-465: INSERT query parameters (snake_case)
```

### Device Monitor

```
services/device-monitor/src/registration/ControllerRegistration.ts
  - Line 195-230: createController() uses snake_case
  - Line 267-277: updateController() uses snake_case
```

### Python Libraries

```
hardware/Raspberry Pis/lib/sentient_registration.py
  - Complete auto-registration helper class
  - Snake_case throughout
  - MQTT topic generation
  - Capability manifest builder

hardware/Raspberry Pis/intro_movie_player/intro-player.py
  - Integrated registration system
  - Auto-registers on MQTT connect
  - Publishes heartbeats
  - Follows topic standards

hardware/Raspberry Pis/test_registration.py
  - Validation script
  - Tests message generation
  - Verifies topic standards
```

## 🧪 Testing

### Run Registration Test

```bash
cd /opt/sentient/hardware/Raspberry\ Pis
python3 test_registration.py
```

**Expected Output:**

```
✓ REGISTRATION TEST PASSED
```

### Watch Device-Monitor Logs

```bash
pm2 logs sentient-device-monitor --lines 50
```

Look for:

```
Received controller registration message
Created new controller from registration
✓ Controller: intro_video_player (clockwork)
```

### Verify Database

```bash
sudo -u postgres psql -d sentient -c \
  "SELECT controller_id, friendly_name, controller_type, status, firmware_version
   FROM controllers
   WHERE controller_id = 'intro_video_player';"
```

## 📊 Registration Message Format

```json
{
  "controller_id": "intro_video_player",
  "room_id": "clockwork",
  "controller_type": "video_manager",
  "friendly_name": "Clockwork Intro Video Player",
  "hardware_type": "Raspberry Pi 4 Model B",
  "firmware_version": "1.0.0",
  "ip_address": "192.168.20.45",
  "heartbeat_interval_ms": 5000,
  "mqtt_client_id": "clockwork_intro_video_player",
  "mqtt_base_topic": "paragon/clockwork",
  "capability_manifest": {
    "devices": [...],
    "actions": [...],
    "mqtt_topics_subscribe": [...],
    "mqtt_topics_publish": [...]
  }
}
```

## 🎮 Controller Types

Supported Raspberry Pi controller types:

- `video_manager` - Video playback (MPV, VLC, custom players)
- `audio_manager` - Audio playback and routing
- `dmx_controller` - DMX lighting control (OLA, QLC+)
- `projection_mapper` - Projection mapping systems
- `touchscreen_interface` - Interactive touch displays
- `game_logic` - Game state management
- `sensor_aggregator` - Sensor data collection hubs
- `microcontroller` - Standard embedded controllers
- `power_controller` - Relay/power control boards
- `hybrid` - Multi-purpose controllers

## 📡 MQTT Topic Standards

All topics follow the standard:

```
paragon/{room_id}/commands/{controller_id}/{device_id}/{command_name}
paragon/{room_id}/sensors/{controller_id}/{device_id}/{sensor_name}
paragon/{room_id}/status/{controller_id}/heartbeat
```

**Rules:**

- ✅ All lowercase categories: `commands`, `sensors`, `status`, `events`
- ✅ Snake_case identifiers from database
- ✅ No display names in topics
- ✅ Device = aggregate physical unit

## 🔄 Next Steps

### For New Video Managers:

1. **Copy the template:**

   ```bash
   cp /opt/sentient/hardware/Raspberry\ Pis/intro_movie_player/intro-player.py \
      /opt/sentient/hardware/Raspberry\ Pis/your_player/your-player.py
   ```

2. **Update configuration:**

   - Change `controller_id`
   - Change `room_id`
   - Define your devices
   - Define your commands

3. **Test registration:**

   ```bash
   python3 test_registration.py
   ```

4. **Deploy and run:**
   - The controller auto-registers on boot
   - Check device-monitor logs
   - Activate in Sentient UI
   - Use in scene timelines!

### For Other Controller Types:

Same pattern works for:

- **Audio Players** - MPD, PulseAudio control
- **DMX Controllers** - OLA/QLC+ integration
- **Projection Mappers** - MadMapper, Resolume
- **Game Logic** - Python game state managers
- **Sensor Hubs** - Aggregate sensor data

## 🎉 Benefits

✅ **Zero Manual Configuration** - No database editing needed
✅ **Consistent with Teensys** - Same pattern as microcontrollers
✅ **Topic Standardization** - MQTT topics follow conventions
✅ **Capability Documentation** - System knows what devices can do
✅ **Auto-Discovery** - New controllers appear immediately
✅ **Always Current** - Hardware info updated on reconnect

## ✨ Summary

Raspberry Pi controllers now have **first-class support** in Sentient Engine:

1. ✅ Auto-registration system implemented
2. ✅ Python helper library created
3. ✅ Example video player updated
4. ✅ Backend API accepts snake_case
5. ✅ Device-monitor processes registrations
6. ✅ Testing tools provided
7. ✅ Documentation complete

**The system is production-ready!** 🚀

---

_Last Updated: November 1, 2025_
_Services Restarted: sentient-api v1.0.1, sentient-device-monitor v0.1.0_
