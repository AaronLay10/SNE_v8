#!/bin/bash

# Script to update MQTT configuration in all v2 controllers
# Updates broker IP and adds authentication credentials

set -e

BROKER_IP="192, 168, 2, 3"
BROKER_HOST="mqtt.sentientengine.ai"
MQTT_USER="paragon_devices"
MQTT_PASSWORD="wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs="

# Controllers already updated (skip these)
SKIP_CONTROLLERS=(
  "main_lighting_v2"
  "power_control_lower_left"
  "power_control_lower_right"
  "power_control_upper_right"
)

echo "========================================"
echo "V2 Controller MQTT Configuration Update"
echo "========================================"
echo ""
echo "Broker IP: 192.168.2.3"
echo "Broker Host: mqtt.sentientengine.ai"
echo "Username: paragon_devices"
echo ""

# Find all v2 controller directories
for controller_dir in "Controller Code Teensy"/*_v2; do
  if [ ! -d "$controller_dir" ]; then
    continue
  fi
  
  controller_name=$(basename "$controller_dir")
  ino_file="$controller_dir/${controller_name}.ino"
  
  # Skip if already updated
  if [[ " ${SKIP_CONTROLLERS[@]} " =~ " ${controller_name} " ]]; then
    echo "⊘ SKIP: $controller_name (already updated)"
    continue
  fi
  
  # Skip if .ino file doesn't exist
  if [ ! -f "$ino_file" ]; then
    echo "⊘ SKIP: $controller_name (no .ino file)"
    continue
  fi
  
  echo "→ Processing: $controller_name"
  
  # Create backup
  cp "$ino_file" "$ino_file.backup"
  
  # Update broker IP (handle various formats)
  if grep -q "mqtt_broker_ip(192, 168, 20, 3)" "$ino_file"; then
    sed -i '' "s/mqtt_broker_ip(192, 168, 20, 3)/mqtt_broker_ip(${BROKER_IP})/g" "$ino_file"
    echo "  ✓ Updated broker IP from 192.168.20.3 to 192.168.2.3"
  elif grep -q "mqtt_broker_ip(192, 168, 2, 3)" "$ino_file"; then
    echo "  ✓ Broker IP already correct"
  else
    echo "  ⚠ Could not find broker IP declaration"
  fi
  
  # Update mqtt_host if exists
  if grep -q 'mqtt_host.*=.*"sentientengine.ai"' "$ino_file"; then
    sed -i '' 's|mqtt_host.*=.*"sentientengine.ai"|mqtt_host = "mqtt.sentientengine.ai"|g' "$ino_file"
    echo "  ✓ Updated mqtt_host to mqtt.sentientengine.ai"
  elif grep -q 'mqtt_host.*=.*"mqtt.sentientengine.ai"' "$ino_file"; then
    echo "  ✓ mqtt_host already correct"
  fi
  
  # Check if credentials already exist
  if grep -q "mqtt_user" "$ino_file" && grep -q "mqtt_password" "$ino_file"; then
    echo "  ✓ Credentials already present"
  else
    # Add credentials after mqtt_port line
    if grep -q "const int mqtt_port" "$ino_file"; then
      # Find the line number of mqtt_port
      line_num=$(grep -n "const int mqtt_port" "$ino_file" | cut -d: -f1)
      
      # Insert credentials after mqtt_port
      sed -i '' "${line_num}a\\
const char *mqtt_user = \"${MQTT_USER}\";\\
const char *mqtt_password = \"${MQTT_PASSWORD}\";
" "$ino_file"
      echo "  ✓ Added MQTT credentials"
    else
      echo "  ⚠ Could not find mqtt_port line to insert credentials"
    fi
  fi
  
  # Check if build_mqtt_config needs username/password
  if grep -q "cfg.username" "$ino_file" && grep -q "cfg.password" "$ino_file"; then
    echo "  ✓ Credentials already in build_mqtt_config"
  else
    # Try to add username/password to build_mqtt_config function
    if grep -q "cfg.brokerPort = mqtt_port;" "$ino_file"; then
      # Add after brokerPort line
      sed -i '' '/cfg.brokerPort = mqtt_port;/a\
    cfg.username = mqtt_user;\
    cfg.password = mqtt_password;
' "$ino_file"
      echo "  ✓ Added credentials to build_mqtt_config()"
    else
      echo "  ⚠ Could not find cfg.brokerPort to insert credentials in build_mqtt_config"
    fi
  fi
  
  echo "  ✓ $controller_name updated"
  echo ""
done

echo "========================================"
echo "Update Complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "1. Review changes with: git diff hardware/"
echo "2. Compile all controllers: ./compile_all_v2.sh"
echo "3. Upload firmware to each Teensy"
echo ""
