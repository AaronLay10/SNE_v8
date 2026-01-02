#!/bin/bash
#
# Compile Teensy 4.1 Firmware
# Usage: ./compile_teensy.sh <path_to_ino_file>
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Check if arduino-cli is installed
if ! command -v arduino-cli &> /dev/null; then
    echo -e "${RED}Error: arduino-cli not found${NC}"
    echo "Install with: brew install arduino-cli"
    exit 1
fi

# Check if input file provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <path_to_ino_file>"
    echo "Example: $0 'Controller Code Teensy/main_lighting_v2/main_lighting_v2.ino'"
    exit 1
fi

INO_FILE="$1"

# Strip 'hardware/' prefix if present (from VS Code tasks)
if [[ "$INO_FILE" == hardware/* ]]; then
    INO_FILE="${INO_FILE#hardware/}"
fi

INO_PATH="${PROJECT_ROOT}/hardware/${INO_FILE}"

# Check if file exists
if [ ! -f "${INO_PATH}" ]; then
    echo -e "${RED}Error: File not found: ${INO_PATH}${NC}"
    exit 1
fi

# Extract controller name from path
CONTROLLER_DIR=$(dirname "${INO_PATH}")
CONTROLLER_NAME=$(basename "${CONTROLLER_DIR}")
OUTPUT_DIR="${PROJECT_ROOT}/hardware/HEX_OUTPUT"

# Version bump logic
METADATA_FILE="${CONTROLLER_DIR}/FirmwareMetadata.h"
if [ -f "${METADATA_FILE}" ]; then
    # Read current version
    CURRENT_VERSION=$(grep 'constexpr const char \*VERSION' "${METADATA_FILE}" | sed 's/.*"\(.*\)".*/\1/')
    
    # Parse version components
    IFS='.' read -ra VERSION_PARTS <<< "${CURRENT_VERSION}"
    MAJOR="${VERSION_PARTS[0]}"
    MINOR="${VERSION_PARTS[1]}"
    PATCH="${VERSION_PARTS[2]}"
    
    # Increment patch version
    PATCH=$((PATCH + 1))
    NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
    
    # Update FirmwareMetadata.h
    sed -i.bak "s/constexpr const char \*VERSION = \".*\";/constexpr const char *VERSION = \"${NEW_VERSION}\";/" "${METADATA_FILE}"
    rm "${METADATA_FILE}.bak"
    
    echo -e "${YELLOW}Version: ${CURRENT_VERSION} → ${NEW_VERSION}${NC}"
else
    NEW_VERSION="unknown"
fi

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}Compiling Teensy 4.1 Firmware${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Controller:${NC} ${CONTROLLER_NAME}"
echo -e "${BLUE}Version:${NC} ${NEW_VERSION}"
echo -e "${BLUE}Output:${NC} ${OUTPUT_DIR}"
echo ""

# Initialize arduino-cli config if needed
if [ ! -f ~/Library/Arduino15/arduino-cli.yaml ]; then
    echo -e "${YELLOW}Initializing arduino-cli...${NC}"
    arduino-cli config init
fi

# Add Teensy board URL if not already added
if ! arduino-cli config dump | grep -q "https://www.pjrc.com/teensy/package_teensy_index.json"; then
    echo -e "${YELLOW}Adding Teensy board manager URL...${NC}"
    arduino-cli config add board_manager.additional_urls https://www.pjrc.com/teensy/package_teensy_index.json
fi

# Update board index
echo -e "${YELLOW}Updating board index...${NC}"
arduino-cli core update-index || echo -e "${YELLOW}Warning: Some indexes could not be updated (continuing anyway)${NC}"

# Install Teensy platform if not installed
if ! arduino-cli core list | grep -q "teensy:avr"; then
    echo -e "${YELLOW}Installing Teensy platform...${NC}"
    arduino-cli core install teensy:avr
fi

# Create output directory and clean old build artifacts for this controller
mkdir -p "${OUTPUT_DIR}"

# Clean old artifacts for this specific controller
echo -e "${YELLOW}Cleaning old build artifacts for ${CONTROLLER_NAME}...${NC}"
rm -f "${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.hex"
rm -f "${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.eep"
rm -f "${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.elf"
rm -f "${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.lst"
rm -f "${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.sym"

# Compile
echo -e "${YELLOW}Compiling...${NC}"
arduino-cli compile \
    --fqbn teensy:avr:teensy41 \
    --libraries "${PROJECT_ROOT}/hardware/Custom Libraries" \
    --output-dir "${OUTPUT_DIR}" \
    "${INO_PATH}"

# Check if compilation succeeded
if [ $? -eq 0 ]; then
    echo ""
    HEX_FILE="${OUTPUT_DIR}/${CONTROLLER_NAME}.ino.hex"
    if [ -f "${HEX_FILE}" ]; then
        HEX_SIZE=$(du -h "${HEX_FILE}" | cut -f1)
        echo -e "${GREEN}✓ SUCCESS${NC} - HEX: ${HEX_SIZE}"
        echo -e "${BLUE}File:${NC} ${HEX_FILE}"
    else
        echo -e "${RED}✗ FAILED${NC} - No HEX file generated"
        exit 1
    fi
    echo ""
    echo "To upload, use Teensy Loader or:"
    echo "  teensy_loader_cli --mcu=TEENSY41 -w ${HEX_FILE}"
else
    echo -e "${RED}✗ FAILED${NC} - Compilation error"
    exit 1
fi
