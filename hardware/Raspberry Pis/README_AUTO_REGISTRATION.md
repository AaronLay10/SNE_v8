# Raspberry Pi Auto-Registration with Sentient Engine

## Overview

Raspberry Pi controllers (video managers, audio players, DMX controllers, etc.) can now automatically register themselves with Sentient Engine, just like Teensy microcontrollers. This eliminates manual database configuration and ensures the system always has current hardware information.

## How It Works

### 1. Registration Flow

```
Raspberry Pi Boots → Connects to MQTT → Publishes Registration → device-monitor Receives → API Creates/Updates Controller → Controller Available in System
```

### 2. MQTT Topics

**Registration Topic:**

```
sentient/system/register
```

**Command Topics (auto-generated):**

```
paragon/{room_id}/commands/{controller_id}/{device_id}/{command_name}
```

**Status Topics:**

```
paragon/{room_id}/status/{controller_id}/heartbeat
```

### 3. Registration Message Format

```json
{
  "controller_id": "intro_video_player",
  "room_id": "clockwork",
  "controller_type": "video_manager",
  "friendly_name": "Clockwork Intro Video Player",
  "description": "Plays intro and loop videos",
  "hardware_type": "Raspberry Pi 4 Model B",
  "hardware_version": "c03115",
  "firmware_version": "1.0.0",
  "ip_address": "192.168.20.45",
  "mac_address": "dc:a6:32:xx:xx:xx",
  "mqtt_client_id": "clockwork_intro_video_player",
  "mqtt_base_topic": "paragon/clockwork",
  "heartbeat_interval_ms": 5000,
  "capability_manifest": {
    "devices": [
      {
        "device_id": "intro_tv",
        "device_type": "video_display",
        "friendly_name": "Clockwork Intro TV",
        "properties": {
          "resolution": "1920x1080",
          "player": "mpv"
        }
      }
    ],
    "actions": [
      {
        "command_name": "play_loop",
        "friendly_name": "Play Loop Video",
        "description": "Play the 35-second clockwork loop video",
        "mqtt_topic": "paragon/clockwork/commands/intro_video_player/intro_tv/play_loop",
        "device_id": "intro_tv",
        "duration_ms": 35000,
        "safety_critical": false
      },
      {
        "command_name": "play_intro",
        "friendly_name": "Play Intro Video",
        "description": "Play the 2.5 minute introduction video",
        "mqtt_topic": "paragon/clockwork/commands/intro_video_player/intro_tv/play_intro",
        "device_id": "intro_tv",
        "duration_ms": 150000,
        "safety_critical": false
      }
    ],
    "mqtt_topics_subscribe": [
      {
        "topic": "paragon/clockwork/commands/intro_video_player/intro_tv/play_loop",
        "message_type": "command",
        "description": "Play the 35-second clockwork loop video"
      },
      {
        "topic": "paragon/clockwork/commands/intro_video_player/intro_tv/play_intro",
        "message_type": "command",
        "description": "Play the 2.5 minute introduction video"
      }
    ],
    "mqtt_topics_publish": [
      {
        "topic": "paragon/clockwork/sensors/intro_video_player/intro_video_player/status",
        "message_type": "heartbeat",
        "publish_interval_ms": 5000,
        "schema": {
          "controller_id": "string",
          "firmware_version": "string",
          "uptime_seconds": "number",
          "current_video": "string"
        }
      }
    ]
  }
}
```

## Using the Registration Library

### Step 1: Import the Library

```python
import sys
from pathlib import Path

# Add lib directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'lib'))
from sentient_registration import SentientRegistration
```

### Step 2: Create Registration Object

```python
registration = SentientRegistration(
    controller_id="intro_video_player",  # Unique ID (snake_case)
    room_id="clockwork",                 # Room this controller belongs to
    controller_type="video_manager",     # Type of controller
    friendly_name="Clockwork Intro Video Player",
    description="Plays intro and loop videos on main display",
    physical_location="Entry Room, above door",
    firmware_version="1.0.0",
    heartbeat_interval_ms=5000
)
```

### Step 3: Register Devices

```python
registration.add_device(
    device_id="intro_tv",               # Device ID (snake_case)
    device_type="video_display",        # Device type
    friendly_name="Clockwork Intro TV",
    properties={
        "resolution": "1920x1080",
        "player": "mpv",
        "output": "HDMI"
    }
)
```

### Step 4: Register Commands

```python
registration.add_command(
    device_id="intro_tv",
    command_name="play_loop",           # Command name (snake_case)
    friendly_name="Play Loop Video",
    description="Play the 35-second clockwork loop video",
    duration_ms=35000,
    safety_critical=False
)

registration.add_command(
    device_id="intro_tv",
    command_name="play_intro",
    friendly_name="Play Intro Video",
    description="Play the 2.5 minute introduction video",
    duration_ms=150000,
    safety_critical=False
)
```

### Step 5: Publish Registration on MQTT Connect

```python
def on_connect(self, client, userdata, flags, rc):
    if rc == 0:
        logger.info("✓ Connected to MQTT broker")

        # Publish registration
        if self.registration.publish_registration(client):
            logger.info("✓ Controller registered with Sentient Engine")

        # Subscribe to command topics
        topics = [
            f"paragon/{self.room_id}/commands/{self.controller_id}/intro_tv/play_loop",
            f"paragon/{self.room_id}/commands/{self.controller_id}/intro_tv/play_intro",
        ]

        for topic in topics:
            client.subscribe(topic, qos=1)
```

### Step 6: Handle Commands

```python
def on_message(self, client, userdata, msg):
    topic = msg.topic

    if topic.endswith("/play_loop"):
        self.play_loop()
    elif topic.endswith("/play_intro"):
        self.play_intro()
```

### Step 7: Send Heartbeats

```python
def start_heartbeat_timer(self):
    def publish_heartbeat():
        if self.mqtt_connected:
            uptime = int(time.time() - self.start_time)

            topic = f"paragon/{self.room_id}/status/{self.controller_id}/heartbeat"
            message = {
                "controller_id": self.controller_id,
                "firmware_version": self.firmware_version,
                "uptime_seconds": uptime,
                "current_video": self.current_video,
                "timestamp": int(time.time() * 1000)
            }

            self.mqtt_client.publish(topic, json.dumps(message), qos=1)

        # Schedule next heartbeat
        self.heartbeat_timer = threading.Timer(5.0, publish_heartbeat)
        self.heartbeat_timer.daemon = True
        self.heartbeat_timer.start()

    publish_heartbeat()
```

## Controller Types

Common controller types for Raspberry Pis:

- `video_manager` - Video playback controllers
- `audio_manager` - Audio playback controllers
- `dmx_controller` - DMX lighting control
- `projection_mapper` - Projection mapping systems
- `touchscreen_interface` - Interactive touchscreens
- `game_logic` - Game state management
- `sensor_aggregator` - Sensor data collection
- `hybrid` - Multi-purpose controllers

## Device Types

Common device types:

- `video_display` - TVs, monitors, projectors
- `audio_player` - Speakers, sound systems
- `dmx_fixture` - DMX-controlled lights/effects
- `projector` - Video projectors
- `touchscreen` - Interactive displays
- `sensor` - Various sensors
- `relay_bank` - Relay control boards

## Database Integration

When a controller publishes registration:

1. **device-monitor** receives message on `sentient/system/register`
2. Checks if controller exists in database
3. If **new**: Creates controller record with status `inactive`
4. If **existing**: Updates hardware info (IP, firmware version, etc.)
5. Operator can then:
   - Enable the controller (set status to `active`)
   - Assign devices to the controller
   - Create device commands that map to MQTT topics

## Benefits

✅ **Zero Manual Configuration** - Controllers self-register on boot
✅ **Always Current** - Hardware info updated on every connect
✅ **Automatic Discovery** - New controllers appear in system immediately
✅ **Topic Standardization** - MQTT topics follow Sentient conventions
✅ **Capability Documentation** - System knows what each controller can do
✅ **Consistent with Teensys** - Same pattern as microcontrollers

## Example: Complete Video Player

See `/opt/sentient/hardware/Raspberry Pis/intro_movie_player/intro-player.py` for a complete working example.

## Testing Registration

Watch device-monitor logs to see registration:

```bash
pm2 logs sentient-device-monitor --lines 50
```

You should see:

```
Received controller registration message
Created new controller from registration
✓ Controller: intro_video_player (clockwork)
```

Check the database:

```bash
sudo -u postgres psql -d sentient -c "SELECT controller_id, friendly_name, status, firmware_version FROM controllers WHERE controller_id = 'intro_video_player';"
```

## Next Steps

After registration, in the Sentient UI:

1. Go to **Controllers** page
2. Find your newly registered controller
3. Set status to `active`
4. Devices and commands are now available for scene timeline steps!

---

**Auto-registration makes hardware management effortless!** 🚀
