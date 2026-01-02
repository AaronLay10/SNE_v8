#!/usr/bin/env python3
"""
Clockwork Intro Movie Player
- clockwork-loop.mp4: 35 second seamless loop
- intro.mp4: 2.5 minute introduction video
"""

import os
import json
import subprocess
import signal
import sys
import time
import logging
from pathlib import Path
import threading
import socket
import paho.mqtt.client as mqtt

# Add lib directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'lib'))
from sentient_registration import SentientRegistration

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('/home/pi/logs/intro_player.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

class ClockworkIntroPlayer:
    def __init__(self):
        logger.info("Starting Clockwork Intro Player...")
        
        # Controller identification
        self.controller_id = "intro_video_player"
        self.room_id = "clockwork"
        self.firmware_version = "1.0.0"
        self.start_time = time.time()
        
        # Video configuration with actual filenames
        self.video_dir = Path("/home/pi/videos")
        self.videos = {
            "loop": {
                "file": self.video_dir / "clockwork-loop.mp4",
                "duration": 35,  # Actual duration
                "loop": True,
                "description": "Clockwork 35-second loop"
            },
            "intro": {
                "file": self.video_dir / "intro.mp4", 
                "duration": 150,  # 2.5 minutes
                "loop": False,
                "description": "Clockwork introduction video"
            }
        }
        
        # Check videos exist
        if not self.check_videos():
            logger.error("Videos missing! Please copy:")
            logger.error("  - clockwork-loop.mp4")
            logger.error("  - intro.mp4")
            logger.error("To /home/pi/videos/")
        
        # Player state
        self.current_process = None
        self.current_video = None
        self.return_to_loop_timer = None
        self.heartbeat_timer = None
        self.ipc_socket = "/tmp/mpv-socket"
        self.mpv_socket = None
        
        # MQTT setup - YOUR BROKERS
        self.mqtt_brokers = [
            "sentientengine.ai",    # Your domain
            "192.168.20.3",         # Your IP
            "localhost"             # Fallback if running locally
        ]
        self.mqtt_connected = False
        self.mqtt_broker = None
        
        # Setup auto-registration
        self.setup_registration()
        self.setup_mqtt()
        
        # Handle shutdown
        signal.signal(signal.SIGINT, self.shutdown)
        signal.signal(signal.SIGTERM, self.shutdown)
        
    def check_videos(self):
        """Check that video files exist"""
        all_found = True
        
        for name, info in self.videos.items():
            video_path = info["file"]
            if video_path.exists():
                size_mb = video_path.stat().st_size / (1024*1024)
                logger.info(f"✓ Found {video_path.name} ({size_mb:.1f} MB)")
            else:
                logger.error(f"✗ Missing: {video_path.name}")
                all_found = False
                
        return all_found
    
    def setup_registration(self):
        """Setup auto-registration with Sentient Engine"""
        logger.info("Setting up controller registration...")
        
        self.registration = SentientRegistration(
            controller_id=self.controller_id,
            room_id=self.room_id,
            controller_type="video_manager",
            friendly_name="Clockwork Intro Video Player",
            description="Plays intro and loop videos on main Clockwork display",
            physical_location="Clockwork Entry Room",
            firmware_version=self.firmware_version,
            heartbeat_interval_ms=5000
        )
        
        # Register the intro TV device
        self.registration.add_device(
            device_id="intro_tv",
            device_type="video_display",
            friendly_name="Clockwork Intro TV",
            device_category="media_playback",  # Required for timeline editor filtering
            properties={
                "resolution": "1920x1080",
                "player": "mpv",
                "output": "HDMI"
            }
        )
        
        # Register commands
        self.registration.add_command(
            device_id="intro_tv",
            command_name="play_loop",
            friendly_name="Play Loop Video",
            description="Play the 35-second clockwork loop video",
            duration_ms=35000,
            safety_critical=False
        )
        
        self.registration.add_command(
            device_id="intro_tv",
            command_name="play_intro",
            friendly_name="Play Intro Video",
            description="Play the 2.5 minute introduction video",
            duration_ms=150000,
            safety_critical=False
        )
        
        self.registration.add_command(
            device_id="intro_tv",
            command_name="stop",
            friendly_name="Stop Playback",
            description="Stop current video and return to loop",
            duration_ms=0,
            safety_critical=False
        )
        
        # Register status topic (heartbeat)
        self.registration.add_sensor(
            device_id=self.controller_id,
            sensor_name="status",
            data_type="heartbeat",
            publish_interval_ms=5000,
            schema={
                "controller_id": "string",
                "firmware_version": "string",
                "uptime_seconds": "number",
                "current_video": "string"
            }
        )
        
        logger.info(f"✓ Registration configured for {self.controller_id}")
        
    def setup_mqtt(self):
        """Setup MQTT connection"""
        # Use unique client ID to prevent connection conflicts
        import uuid
        unique_client_id = f"clockwork_intro_{uuid.uuid4().hex[:8]}"
        
        # Fix for paho-mqtt 2.0+
        try:
            self.mqtt_client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION1,
                unique_client_id
            )
        except:
            self.mqtt_client = mqtt.Client(unique_client_id)
            
        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_message = self.on_message
        self.mqtt_client.on_disconnect = self.on_disconnect
        
        # Try each broker
        for broker in self.mqtt_brokers:
            try:
                logger.info(f"Connecting to {broker}...")
                self.mqtt_client.connect(broker, 1883, 60)
                self.mqtt_broker = broker
                self.mqtt_connected = True
                logger.info(f"✓ Connected to MQTT broker: {broker}")
                break
            except Exception as e:
                logger.warning(f"Could not connect to {broker}: {e}")
                
        if not self.mqtt_connected:
            logger.warning("Running without MQTT control - will just loop video")
            
    def on_connect(self, client, userdata, flags, rc):
        """MQTT connected"""
        if rc == 0:
            logger.info("✓ Connected to MQTT broker")
            
            # Wait for connection to fully stabilize before publishing
            import time
            time.sleep(2.0)
            
            # Publish registration
            logger.info("Publishing controller registration...")
            if self.registration.publish_registration(client):
                logger.info("✓ Controller registered with Sentient Engine")
            else:
                logger.warning("Failed to publish registration")
            
            # Subscribe to device-level command topic (Sentient v4 standard)
            device_topic = f"paragon/{self.room_id}/commands/{self.controller_id}/intro_tv/#"
            client.subscribe(device_topic, qos=1)
            logger.info(f"  Subscribed to: {device_topic}")
                
            logger.info("✓ Ready for MQTT commands")
            
            # Start heartbeat
            self.start_heartbeat_timer()
            
    def on_disconnect(self, client, userdata, rc):
        """MQTT disconnected"""
        if rc != 0:
            logger.warning(f"Lost MQTT connection (rc={rc}), will try to reconnect...")
            self.mqtt_connected = False
    
    def publish_acknowledgement(self, device_id, command, success=True):
        """Publish acknowledgement matching Teensy controller pattern"""
        if self.mqtt_connected and self.mqtt_client:
            topic = f"paragon/{self.room_id}/acknowledgement/{self.controller_id}/{device_id}/{command}"
            payload = {
                "controller_id": self.controller_id,
                "device_id": device_id,
                "command": command,
                "success": success,
                "timestamp_ms": int(time.time() * 1000)
            }
            logger.info(f"📤 Publishing ACK to: {topic}")
            logger.info(f"   Payload: {json.dumps(payload)}")
            result = self.mqtt_client.publish(topic, json.dumps(payload), qos=1)
            logger.info(f"✓ ACK: {device_id}/{command} (success={success}, rc={result.rc})")
            
    def on_message(self, client, userdata, msg):
        """Handle MQTT commands - Sentient v4 standard"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8') if msg.payload else ''
            
            # Parse command from topic: paragon/room/commands/controller/device/command
            parts = topic.split('/')
            if len(parts) >= 6 and parts[2] == 'commands':
                device_id = parts[4]
                command = parts[5]
                
                logger.info(f"MQTT Command: {device_id}/{command}")
                
                success = True
                try:
                    if command == "play_loop":
                        self.play_loop()
                    elif command == "play_intro":
                        self.play_intro()
                    elif command == "stop":
                        self.stop_video()
                        self.play_loop()
                    else:
                        logger.warning(f"Unknown command: {command}")
                        success = False
                except Exception as e:
                    logger.error(f"Command execution failed: {e}")
                    success = False
                
                # Publish acknowledgement
                self.publish_acknowledgement(device_id, command, success)
            else:
                logger.warning(f"Unhandled topic format: {topic}")
                
        except Exception as e:
            logger.error(f"Error handling message: {e}")
            
    def publish_video_completed(self, video_name):
        """Publish video completion event to state topic"""
        if not self.mqtt_connected or not self.mqtt_client:
            return
        
        # Publish to device state topic
        topic = f"paragon/{self.room_id}/status/{self.controller_id}/intro_tv/state"
        message = {
            "device_id": "intro_tv",
            "controller_id": self.controller_id,
            "video_completed": video_name,
            "timestamp_ms": int(time.time() * 1000)
        }
        
        self.mqtt_client.publish(topic, json.dumps(message), qos=1)
        logger.info(f"✓ Published completion: {video_name}")
    
    def play_loop(self):
        """Play the clockwork loop video"""
        self.play_video("loop")
        
    def play_intro(self):
        """Play the intro video"""
        self.play_video("intro")
        
    def start_mpv_player(self):
        """Start mpv with BOTH videos pre-loaded in playlist for instant switching"""
        # Remove old socket if exists
        if os.path.exists(self.ipc_socket):
            os.remove(self.ipc_socket)
            
        # Pre-load both videos in playlist
        # Index 0: loop video (starts playing)
        # Index 1: intro video (pre-loaded, ready to switch)
        loop_path = self.videos["loop"]["file"]
        intro_path = self.videos["intro"]["file"]
        
        cmd = [
            'mpv',
            str(loop_path),   # Playlist index 0
            str(intro_path),  # Playlist index 1 (pre-loaded!)
            '--fs',  # Fullscreen
            '--vo=drm',  # Direct Rendering
            '--hwdec=auto',  # Hardware decoding
            '--really-quiet',  # Minimize output
            '--no-terminal',  # No terminal control
            '--cache=yes',  # Enable cache
            '--demuxer-max-bytes=50M',  # Larger preload for intro
            '--audio-device=auto',
            '--audio-fallback-to-null=yes',
            '--no-audio-display',
            '--loop-file=inf',  # Loop current file (not playlist)
            '--input-ipc-server=' + self.ipc_socket,  # Enable IPC control
            '--keep-open=yes',  # Don't exit when file ends
        ]
        
        # Set environment
        env = os.environ.copy()
        if 'DISPLAY' not in env:
            env['DISPLAY'] = ':0'
        if 'XDG_RUNTIME_DIR' not in env:
            env['XDG_RUNTIME_DIR'] = '/run/user/1000'
        
        try:
            self.current_process = subprocess.Popen(
                cmd, 
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            logger.info(f"✓ MPV player started with PID: {self.current_process.pid}")
            logger.info(f"✓ IPC socket: {self.ipc_socket}")
            
            # Wait for socket to be created
            for _ in range(20):  # Wait up to 2 seconds
                if os.path.exists(self.ipc_socket):
                    time.sleep(0.2)  # Extra time for mpv to be ready
                    logger.info("✓ IPC socket ready")
                    self.current_video = "loop"
                    return True
                time.sleep(0.1)
            
            logger.error("IPC socket not created!")
            return False
            
        except Exception as e:
            logger.error(f"Failed to start mpv: {e}")
            return False
    
    def send_mpv_command(self, command):
        """Send command to mpv via IPC socket. Returns response or None on failure."""
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect(self.ipc_socket)
            sock.send((json.dumps(command) + '\n').encode('utf-8'))

            # Read response (may have multiple lines)
            response = b''
            sock.settimeout(0.5)
            try:
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
            except socket.timeout:
                pass  # Normal - we got all data

            sock.close()

            # Parse first JSON line (ignore event notifications)
            if response:
                for line in response.decode('utf-8').strip().split('\n'):
                    try:
                        return json.loads(line)
                    except:
                        continue
            return {"error": "no_response"}
        except Exception as e:
            logger.warning(f"IPC command failed: {e}")
            return None

    def is_mpv_responsive(self):
        """Check if mpv is running and responding to IPC commands"""
        if not self.current_process or self.current_process.poll() is not None:
            return False
        if not os.path.exists(self.ipc_socket):
            return False
        # Try a simple command
        result = self.send_mpv_command({"command": ["get_property", "pid"]})
        return result is not None and "error" not in result
    
    def play_video(self, video_name):
        """Play specified video using seamless IPC control"""
        if video_name not in self.videos:
            logger.error(f"Unknown video: {video_name}")
            return

        video_info = self.videos[video_name]
        video_path = video_info["file"]

        if not video_path.exists():
            logger.error(f"Video not found: {video_path}")
            return

        # Cancel return timer if exists
        if self.return_to_loop_timer:
            self.return_to_loop_timer.cancel()
            self.return_to_loop_timer = None

        # Check if mpv is running AND responsive
        responsive = self.is_mpv_responsive()
        logger.info(f"MPV responsive check: {responsive}")

        if not responsive:
            logger.warning("MPV not responsive, restarting...")
            self.stop_video()
            self.start_mpv_player()
            time.sleep(0.5)

        logger.info(f"▶ Switching to: {video_path.name}")
        logger.info(f"  Full path: {video_path}")
        logger.info(f"  File exists: {video_path.exists()}")
        logger.info(f"  MPV process alive: {self.current_process and self.current_process.poll() is None}")
        
        # Switch to the pre-loaded video using playlist index
        # Index 0 = loop, Index 1 = intro
        playlist_index = 0 if video_name == "loop" else 1
        
        logger.info(f"  Switching to playlist index {playlist_index}")
        
        # Disable loop before switching (will re-enable for loop video)
        result = self.send_mpv_command({"command": ["set_property", "loop-file", "no"]})
        logger.info(f"  Loop disabled: {result}")
        
        # Switch to the target video in playlist
        result = self.send_mpv_command({"command": ["playlist-play-index", playlist_index]})
        logger.info(f"  Playlist switch: {result}")
        
        # Wait a moment for switch
        time.sleep(0.3)
        
        # Verify the switch happened
        filename_check = self.send_mpv_command({"command": ["get_property", "filename"]})
        logger.info(f"  Now playing: {filename_check}")
        
        # Set loop mode based on video type
        if video_info["loop"]:
            result = self.send_mpv_command({"command": ["set_property", "loop-file", "inf"]})
            logger.info(f"  Loop mode enabled: {result}")
            logger.info(f"✓ Playing: {video_path.name} (looping)")
        else:
            logger.info(f"✓ Playing: {video_path.name} ({video_info['duration']}s)")

        self.current_video = video_name

        # Schedule return to loop after intro
        if video_name == "intro":
            def on_intro_complete():
                # Publish completion event before returning to loop
                self.publish_video_completed("intro")
                self.play_loop()
            
            self.return_to_loop_timer = threading.Timer(
                video_info["duration"] + 1,
                on_intro_complete
            )
            self.return_to_loop_timer.start()
            logger.info(f"⏱ Will return to loop in {video_info['duration']} seconds")
            
    def stop_video(self):
        """Stop mpv player"""
        if self.current_process:
            try:
                self.send_mpv_command({"command": ["quit"]})
                time.sleep(0.2)
                self.current_process.kill()
                self.current_process.wait(timeout=0.5)
            except:
                pass
            self.current_process = None
            self.current_video = None
        
        # Clean up socket
        if os.path.exists(self.ipc_socket):
            try:
                os.remove(self.ipc_socket)
            except:
                pass
    
    def start_heartbeat_timer(self):
        """Start periodic heartbeat publishing - Sentient v4 standard"""
        def publish_heartbeat():
            if self.mqtt_connected and self.mqtt_client:
                uptime = int(time.time() - self.start_time)
                
                # Heartbeat matching Teensy controller format
                topic = f"paragon/{self.room_id}/status/{self.controller_id}/heartbeat"
                message = {
                    "controller_id": self.controller_id,
                    "firmware_version": self.firmware_version,
                    "uptime_ms": uptime * 1000,
                    "timestamp_ms": int(time.time() * 1000),
                    # Additional Pi-specific fields
                    "current_video": self.current_video,
                    "player_running": self.current_process is not None
                }
                
                self.mqtt_client.publish(topic, json.dumps(message), qos=1)
            
            # Schedule next heartbeat
            if self.mqtt_connected:
                self.heartbeat_timer = threading.Timer(5.0, publish_heartbeat)
                self.heartbeat_timer.daemon = True
                self.heartbeat_timer.start()
        
        # Start first heartbeat
        publish_heartbeat()
            
    def send_status(self):
        """Send current status via MQTT (legacy method, heartbeat preferred)"""
        if self.mqtt_connected and self.mqtt_client:
            status = {
                "controller_id": self.controller_id,
                "playing": self.current_video,
                "running": self.current_process is not None,
                "uptime_seconds": int(time.time() - self.start_time),
                "timestamp": int(time.time() * 1000)
            }
            self.mqtt_client.publish(
                f"paragon/{self.room_id}/status/{self.controller_id}/status",
                json.dumps(status),
                qos=1
            )
            
    def run(self):
        """Main loop"""
        print("""
        ╔═══════════════════════════════════════╗
        ║      CLOCKWORK INTRO PLAYER          ║
        ╟───────────────────────────────────────╢
        ║  Loop: clockwork-loop.mp4 (35s)      ║
        ║  Intro: intro.mp4 (2.5 min)          ║
        ╟───────────────────────────────────────╢
        ║  MQTT: sentientengine.ai             ║
        ║        192.168.20.3                   ║
        ╚═══════════════════════════════════════╝
        """)
        
        # Start mpv player with IPC control
        if not self.start_mpv_player():
            logger.error("Failed to start video player!")
            sys.exit(1)
        
        # Send initial status
        self.send_status()
        
        # Run MQTT loop
        if self.mqtt_connected:
            try:
                self.mqtt_client.loop_forever()
            except KeyboardInterrupt:
                pass
        else:
            # Just keep running
            logger.info("No MQTT - playing loop video only")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                pass
                
    def shutdown(self, signum, frame):
        """Clean shutdown"""
        logger.info("Shutting down...")
        
        if self.return_to_loop_timer:
            self.return_to_loop_timer.cancel()
        
        if self.heartbeat_timer:
            self.heartbeat_timer.cancel()
            
        self.stop_video()
        
        if self.mqtt_connected and self.mqtt_client:
            self.mqtt_client.disconnect()
            
        sys.exit(0)

if __name__ == "__main__":
    # Create directories if they don't exist
    Path("/home/pi/videos").mkdir(exist_ok=True)
    Path("/home/pi/logs").mkdir(exist_ok=True)
    
    player = ClockworkIntroPlayer()
    player.run()
