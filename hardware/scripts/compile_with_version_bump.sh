#!/bin/bash
#
# Compile Teensy Firmware with Automatic Version Bump
#
# This script:
# 1. Bumps the patch version in FirmwareMetadata.h
# 2. Compiles the firmware
# 3. Shows compilation results
#
# Usage: ./compile_with_version_bump.sh <firmware_name>
#
# Examples:
#   ./compile_with_version_bump.sh PilotLight_v2.0.1
#   ./compile_with_version_bump.sh LeverBoiler_v2.0.1
#

set -e  # Exit on error

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Paths
#!/usr/bin/env bash

echo "This script is deprecated. Use: /opt/sentient/scripts/compile_current.sh for single builds."
exit 1
ARDUINO_CLI="/opt/sentient/bin/arduino-cli"
