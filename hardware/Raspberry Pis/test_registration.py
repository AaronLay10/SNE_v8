#!/usr/bin/env python3
"""
Test Raspberry Pi Auto-Registration
Run this to verify controller registration is working
"""

import sys
import json
from pathlib import Path

# Add lib directory to path
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
from sentient_registration import SentientRegistration

def test_registration():
    """Test registration message generation"""
    
    print("=" * 60)
    print("TESTING RASPBERRY PI AUTO-REGISTRATION")
    print("=" * 60)
    
    # Create registration object
    registration = SentientRegistration(
        controller_id="intro_video_player",
        room_id="clockwork",
        controller_type="video_manager",
        friendly_name="Clockwork Intro Video Player",
        description="Plays intro and loop videos on main Clockwork display",
        physical_location="Entry Room, above door",
        firmware_version="1.0.0",
        heartbeat_interval_ms=5000
    )
    
    print("\n✓ Registration object created")
    print(f"  Controller: {registration.controller_id}")
    print(f"  Room: {registration.room_id}")
    print(f"  Type: {registration.controller_type}")
    print(f"  Hardware: {registration.hardware_type}")
    print(f"  IP: {registration.ip_address}")
    print(f"  MAC: {registration.mac_address}")
    
    # Add device
    registration.add_device(
        device_id="intro_tv",
        device_type="video_display",
        friendly_name="Clockwork Intro TV",
        properties={
            "resolution": "1920x1080",
            "player": "mpv",
            "output": "HDMI"
        }
    )
    
    # Add commands
    registration.add_command(
        device_id="intro_tv",
        command_name="play_loop",
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
    
    registration.add_command(
        device_id="intro_tv",
        command_name="stop",
        friendly_name="Stop Playback",
        description="Stop current video and return to loop",
        duration_ms=0,
        safety_critical=False
    )
    
    # Add sensor
    registration.add_sensor(
        device_id=registration.controller_id,
        sensor_name="status",
        data_type="heartbeat",
        publish_interval_ms=5000
    )
    
    print("\n✓ Capability manifest built:")
    print(f"  Devices: {len(registration.devices)}")
    print(f"  Commands: {sum(len(cmds) for cmds in registration.commands.values())}")
    print(f"  Publish topics: {len(registration.mqtt_topics_publish)}")
    print(f"  Subscribe topics: {len(registration.mqtt_topics_subscribe)}")
    
    # Build registration message
    message = registration.build_registration_message()
    
    print("\n✓ Registration message generated")
    print("\nREGISTRATION MESSAGE (JSON):")
    print("-" * 60)
    print(json.dumps(message, indent=2))
    print("-" * 60)
    
    # Validate message structure
    required_fields = ['controller_id', 'room_id', 'controller_type', 'friendly_name']
    missing_fields = [f for f in required_fields if f not in message]
    
    if missing_fields:
        print(f"\n✗ ERROR: Missing required fields: {missing_fields}")
        return False
    
    print("\n✓ All required fields present")
    
    # Validate capability manifest
    if 'capability_manifest' in message:
        manifest = message['capability_manifest']
        print("\n✓ Capability manifest included:")
        if 'devices' in manifest:
            print(f"  - {len(manifest['devices'])} device(s)")
        if 'actions' in manifest:
            print(f"  - {len(manifest['actions'])} action(s)")
        if 'mqtt_topics_subscribe' in manifest:
            print(f"  - {len(manifest['mqtt_topics_subscribe'])} subscribe topic(s)")
        if 'mqtt_topics_publish' in manifest:
            print(f"  - {len(manifest['mqtt_topics_publish'])} publish topic(s)")
    
    # Validate MQTT topics follow standards
    print("\n✓ MQTT Topics:")
    for topic_info in registration.mqtt_topics_subscribe:
        topic = topic_info['topic']
        print(f"  SUB: {topic}")
        
        # Check topic structure
        parts = topic.split('/')
        if len(parts) >= 6:
            client, room, category, controller, device, command = parts[:6]
            if category not in ['commands', 'sensors', 'status', 'events']:
                print(f"    ⚠ WARNING: Non-standard category '{category}'")
            if category != category.lower():
                print(f"    ✗ ERROR: Category must be lowercase!")
                return False
    
    for topic_info in registration.mqtt_topics_publish:
        topic = topic_info['topic']
        print(f"  PUB: {topic}")
    
    print("\n" + "=" * 60)
    print("✓ REGISTRATION TEST PASSED")
    print("=" * 60)
    print("\nTo publish this registration:")
    print("1. Connect to MQTT broker")
    print("2. Call: registration.publish_registration(mqtt_client)")
    print("3. Check device-monitor logs: pm2 logs sentient-device-monitor")
    print("4. Check database: sudo -u postgres psql -d sentient")
    print("   SELECT controller_id, friendly_name, status FROM controllers;")
    print("\n")
    
    return True

if __name__ == "__main__":
    try:
        success = test_registration()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"\n✗ TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
