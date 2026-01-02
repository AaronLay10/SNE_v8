#!/usr/bin/env python3
"""
Sentient Controller Auto-Registration
Allows Raspberry Pi controllers to self-register with Sentient Engine
"""

import json
import socket
import subprocess
import logging
import time
from typing import Dict, List, Optional, Any

logger = logging.getLogger(__name__)

class SentientRegistration:
    """
    Handles auto-registration of Raspberry Pi controllers with Sentient Engine.
    
    Usage:
        registration = SentientRegistration(
            controller_id="intro_video_player",
            room_id="clockwork",
            controller_type="video_manager",
            friendly_name="Clockwork Intro Video Player"
        )
        
        # Add devices
        registration.add_device(
            device_id="intro_tv",
            device_type="video_display",
            friendly_name="Clockwork Intro TV"
        )
        
        # Add commands
        registration.add_command(
            device_id="intro_tv",
            command_name="play_loop",
            friendly_name="Play Loop Video",
            description="Play the 35-second clockwork loop video"
        )
        
        # Publish registration
        registration.publish_registration(mqtt_client)
    """
    
    def __init__(
        self,
        controller_id: str,
        room_id: str,
        controller_type: str = "video_manager",
        friendly_name: Optional[str] = None,
        description: Optional[str] = None,
        physical_location: Optional[str] = None,
        firmware_version: str = "1.0.0",
        heartbeat_interval_ms: int = 5000
    ):
        self.controller_id = controller_id
        self.room_id = room_id
        self.controller_type = controller_type
        self.friendly_name = friendly_name or controller_id.replace("_", " ").title()
        self.description = description
        self.physical_location = physical_location
        self.firmware_version = firmware_version
        self.heartbeat_interval_ms = heartbeat_interval_ms
        
        # Auto-detect hardware info
        self.hardware_type = self._detect_hardware_type()
        self.hardware_version = self._get_hardware_version()
        self.ip_address = self._get_ip_address()
        self.mac_address = self._get_mac_address()
        
        # Capability manifest
        self.devices: List[Dict[str, Any]] = []
        self.commands: Dict[str, List[Dict[str, Any]]] = {}
        self.mqtt_topics_publish: List[Dict[str, Any]] = []
        self.mqtt_topics_subscribe: List[Dict[str, Any]] = []
        
    def _detect_hardware_type(self) -> str:
        """Detect Raspberry Pi model"""
        try:
            with open('/proc/cpuinfo', 'r') as f:
                for line in f:
                    if line.startswith('Model'):
                        model = line.split(':', 1)[1].strip()
                        return model
        except:
            pass
        return "Raspberry Pi"
    
    def _get_hardware_version(self) -> Optional[str]:
        """Get Raspberry Pi revision"""
        try:
            with open('/proc/cpuinfo', 'r') as f:
                for line in f:
                    if line.startswith('Revision'):
                        return line.split(':', 1)[1].strip()
        except:
            pass
        return None
    
    def _get_ip_address(self) -> Optional[str]:
        """Get primary IP address"""
        try:
            # Connect to external host to determine primary interface IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return None
    
    def _get_mac_address(self) -> Optional[str]:
        """Get MAC address of primary network interface"""
        try:
            result = subprocess.run(
                ['cat', '/sys/class/net/eth0/address'],
                capture_output=True,
                text=True,
                timeout=2
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except:
            pass
        return None
    
    def add_device(
        self,
        device_id: str,
        device_type: str,
        friendly_name: Optional[str] = None,
        device_category: Optional[str] = None,
        properties: Optional[Dict[str, Any]] = None
    ):
        """
        Add a device to the capability manifest.
        
        Args:
            device_id: Unique identifier (snake_case)
            device_type: Type of device (video_display, audio_player, dmx_controller, etc.)
            friendly_name: Human-readable name
            device_category: Category for filtering (media_playback, puzzle, lighting, etc.)
            properties: Additional device properties
        """
        device = {
            "device_id": device_id,
            "device_type": device_type,
            "friendly_name": friendly_name or device_id.replace("_", " ").title(),
            "device_category": device_category or "puzzle"  # Default to puzzle if not specified
        }
        
        if properties:
            device["properties"] = properties
        
        self.devices.append(device)
        logger.info(f"Added device: {device_id} ({device_type})")
        
    def add_command(
        self,
        device_id: str,
        command_name: str,
        friendly_name: Optional[str] = None,
        description: Optional[str] = None,
        parameters: Optional[List[Dict[str, Any]]] = None,
        duration_ms: Optional[int] = None,
        safety_critical: bool = False
    ):
        """
        Add a command that this device can execute.
        
        Args:
            device_id: Device this command belongs to
            command_name: Command identifier (snake_case)
            friendly_name: Human-readable command name
            description: What the command does
            parameters: List of command parameters
            duration_ms: Estimated execution duration
            safety_critical: Whether command requires safety verification
        """
        if device_id not in self.commands:
            self.commands[device_id] = []
        
        # Build MQTT topic for this command
        topic = f"paragon/{self.room_id}/commands/{self.controller_id}/{device_id}/{command_name}"
        
        command = {
            "action_id": command_name,  # Use action_id for API schema compatibility
            "friendly_name": friendly_name or command_name.replace("_", " ").title(),
            "mqtt_topic": topic
        }
        
        if description:
            command["description"] = description
        if parameters:
            command["parameters"] = parameters
        if duration_ms:
            command["duration_ms"] = duration_ms
        if safety_critical:
            command["safety_critical"] = safety_critical
        
        self.commands[device_id].append(command)
        
        # Add subscribe topic to manifest
        self.add_subscribe_topic(
            topic=topic,
            message_type="command",
            description=description or f"Execute {command_name}",
            parameters=parameters,
            safety_critical=safety_critical
        )
        
        logger.info(f"Added command: {device_id}.{command_name}")
        
    def add_sensor(
        self,
        device_id: str,
        sensor_name: str,
        data_type: str,
        publish_interval_ms: Optional[int] = None,
        schema: Optional[Dict[str, str]] = None
    ):
        """
        Add a sensor that publishes data.
        
        Args:
            device_id: Device this sensor belongs to
            sensor_name: Sensor identifier (snake_case)
            data_type: Type of data (temperature, humidity, ir_code, etc.)
            publish_interval_ms: How often sensor publishes
            schema: JSON schema of sensor data
        """
        topic = f"paragon/{self.room_id}/sensors/{self.controller_id}/{device_id}/{sensor_name}"
        
        sensor = {
            "topic": topic,
            "message_type": data_type
        }
        
        if publish_interval_ms:
            sensor["publish_interval_ms"] = publish_interval_ms
        if schema:
            sensor["schema"] = schema
        
        self.mqtt_topics_publish.append(sensor)
        logger.info(f"Added sensor: {device_id}.{sensor_name} ({data_type})")
        
    def add_subscribe_topic(
        self,
        topic: str,
        message_type: str = "command",
        description: Optional[str] = None,
        parameters: Optional[List[Dict[str, Any]]] = None,
        safety_critical: bool = False
    ):
        """Add a topic this controller subscribes to"""
        sub = {
            "topic": topic,
            "message_type": message_type
        }
        
        if description:
            sub["description"] = description
        if parameters:
            sub["parameters"] = parameters
        if safety_critical:
            sub["safety_critical"] = safety_critical
        
        self.mqtt_topics_subscribe.append(sub)
        
    def build_registration_message(self) -> Dict[str, Any]:
        """Build the complete registration message"""
        message = {
            "controller_id": self.controller_id,
            "room_id": self.room_id,
            "controller_type": self.controller_type,
            "friendly_name": self.friendly_name,
            "hardware_type": self.hardware_type,
            "firmware_version": self.firmware_version,
            "heartbeat_interval_ms": self.heartbeat_interval_ms,
            "mqtt_client_id": f"{self.room_id}_{self.controller_id}",
            "mqtt_base_topic": f"paragon/{self.room_id}"
        }
        
        if self.description:
            message["description"] = self.description
        if self.physical_location:
            message["physical_location"] = self.physical_location
        if self.hardware_version:
            message["hardware_version"] = self.hardware_version
        if self.ip_address:
            message["ip_address"] = self.ip_address
        if self.mac_address:
            message["mac_address"] = self.mac_address
        
        # Add capability manifest
        capability_manifest = {}
        
        if self.devices:
            capability_manifest["devices"] = self.devices
        
        if self.mqtt_topics_publish:
            capability_manifest["mqtt_topics_publish"] = self.mqtt_topics_publish
        
        if self.mqtt_topics_subscribe:
            capability_manifest["mqtt_topics_subscribe"] = self.mqtt_topics_subscribe
        
        # Build actions from commands
        if self.commands:
            actions = []
            for device_id, device_commands in self.commands.items():
                actions.extend(device_commands)
            capability_manifest["actions"] = actions
        
        if capability_manifest:
            message["capability_manifest"] = capability_manifest
        
        return message
    
    def publish_registration(self, mqtt_client, qos: int = 1) -> bool:
        """
        Publish split registration format (v2.0.7+).
        Sends controller first, then each device separately.
        
        Args:
            mqtt_client: Connected paho MQTT client
            qos: MQTT QoS level
            
        Returns:
            True if all messages published successfully
        """
        try:
            # Build full registration to extract data
            full_message = self.build_registration_message()
            
            # 1. Publish controller registration
            controller_topic = "sentient/system/register/controller"
            controller_message = {
                "room_id": self.room_id,
                "controller_id": self.controller_id,
                "friendly_name": self.friendly_name,
                "description": self.description,
                "hardware_type": self.hardware_type,
                "firmware_version": self.firmware_version,
                "ip_address": self.ip_address,
                "controller_type": self.controller_type,
                "device_count": len(self.devices),  # Tell handler how many devices to expect
                "capability_manifest": full_message.get("capability_manifest", {})
            }
            
            result = mqtt_client.publish(controller_topic, json.dumps(controller_message), qos=qos)
            if result.rc != 0:
                logger.error(f"Failed to publish controller registration (rc={result.rc})")
                return False
            
            logger.info(f"✓ Published controller registration for {self.controller_id}")
            
            # 2. Publish device registrations
            devices_published = 0
            for device_index, device in enumerate(self.devices):
                device_topic = "sentient/system/register/device"

                # Get commands for this device
                device_commands = self.commands.get(device["device_id"], [])

                # Transform commands to mqtt_topics format expected by device-monitor
                # Format: [{"topic": "commands/play_intro", "topic_type": "command"}, ...]
                mqtt_topics = []
                for cmd in device_commands:
                    command_name = cmd.get("action_id")
                    if command_name:
                        mqtt_topics.append({
                            "topic": f"commands/{command_name}",
                            "topic_type": "command"
                        })

                device_message = {
                    "controller_id": self.controller_id,
                    "device_index": device_index,  # Required for tracking
                    "device_id": device["device_id"],
                    "friendly_name": device["friendly_name"],
                    "device_type": device["device_type"],
                    "device_category": device.get("device_category", "puzzle"),
                    "properties": device.get("properties", {}),
                    "mqtt_topics": mqtt_topics  # Commands in device-monitor expected format
                }
                
                result = mqtt_client.publish(device_topic, json.dumps(device_message), qos=qos)
                if result.rc == 0:
                    devices_published += 1
                    logger.info(f"✓ Published device registration for {device['device_id']}")
                else:
                    logger.error(f"Failed to publish device registration for {device['device_id']} (rc={result.rc})")
            
            logger.info(
                f"✓ Published split registration for {self.controller_id} "
                f"(1 controller, {devices_published}/{len(self.devices)} devices)"
            )
            
            return devices_published == len(self.devices)
                
        except Exception as e:
            logger.error(f"Error publishing registration: {e}")
            return False
        logger.info(
            f"✓ Published split registration for {self.controller_id} "
            f"(1 controller, {devices_published}/{len(self.devices)} devices)"
        )
        
        return devices_published == len(self.devices)
    
    def publish_heartbeat(self, mqtt_client, uptime_seconds: int, qos: int = 1) -> bool:
        """
        Publish heartbeat message.
        
        Args:
            mqtt_client: Connected paho MQTT client
            uptime_seconds: System uptime in seconds
            qos: MQTT QoS level
            
        Returns:
            True if published successfully
        """
        try:
            topic = f"paragon/{self.room_id}/status/{self.controller_id}/heartbeat"
            
            message = {
                "controller_id": self.controller_id,
                "firmware_version": self.firmware_version,
                "uptime_seconds": uptime_seconds,
                "timestamp": int(time.time() * 1000)
            }
            
            result = mqtt_client.publish(topic, json.dumps(message), qos=qos)
            return result.rc == 0
            
        except Exception as e:
            logger.error(f"Error publishing heartbeat: {e}")
            return False


if __name__ == "__main__":
    # Example usage
    import time
    
    registration = SentientRegistration(
        controller_id="intro_video_player",
        room_id="clockwork",
        controller_type="video_manager",
        friendly_name="Clockwork Intro Video Player",
        description="Plays intro and loop videos on main display",
        firmware_version="1.0.0"
    )
    
    # Add device
    registration.add_device(
        device_id="intro_tv",
        device_type="video_display",
        friendly_name="Clockwork Intro TV",
        properties={
            "resolution": "1920x1080",
            "player": "mpv"
        }
    )
    
    # Add commands
    registration.add_command(
        device_id="intro_tv",
        command_name="play_loop",
        friendly_name="Play Loop Video",
        description="Play the 35-second clockwork loop video",
        duration_ms=35000
    )
    
    registration.add_command(
        device_id="intro_tv",
        command_name="play_intro",
        friendly_name="Play Intro Video",
        description="Play the 2.5 minute introduction video",
        duration_ms=150000
    )
    
    # Print registration message
    print(json.dumps(registration.build_registration_message(), indent=2))
