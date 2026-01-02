#!/bin/bash

# Sentient Controller Firmware - Batch Compiler
# Compiles all Teensy 4.1 controller firmware and generates HEX files

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTROLLER_DIR="$SCRIPT_DIR/Controller Code Teensy"
OUTPUT_DIR="$SCRIPT_DIR/HEX_OUTPUT"
LOG_FILE="$SCRIPT_DIR/compile_all_$(date +%Y%m%d_%H%M%S).log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║  Sentient Controller Firmware - Batch Compiler                ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Output Directory:${NC} $OUTPUT_DIR"
echo -e "${BLUE}Log File:${NC} $LOG_FILE"
echo ""

# Initialize counters
TOTAL=0
SUCCESS=0
FAILED=0
SKIPPED=0

# Array to store failed controllers
declare -a FAILED_CONTROLLERS

# Find all .ino files
echo -e "${YELLOW}Scanning for controller firmware...${NC}"
echo ""

for controller_dir in "$CONTROLLER_DIR"/*/ ; do
    if [ ! -d "$controller_dir" ]; then
        continue
    fi
    
    controller_name=$(basename "$controller_dir")
    
    # Skip if directory starts with . or _
    if [[ "$controller_name" == .* ]] || [[ "$controller_name" == _* ]]; then
        continue
    fi
    
    # Look for .ino file
    ino_file=$(find "$controller_dir" -maxdepth 1 -name "*.ino" | head -n 1)
    
    if [ -z "$ino_file" ]; then
        echo -e "${YELLOW}⚠ Skipping${NC} $controller_name (no .ino file found)"
        ((SKIPPED++))
        continue
    fi
    
    ((TOTAL++))
    
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}[$TOTAL]${NC} Compiling: ${GREEN}$controller_name${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    # Version bump logic
    METADATA_FILE="$controller_dir/FirmwareMetadata.h"
    if [ -f "$METADATA_FILE" ]; then
        # Read current version
        CURRENT_VERSION=$(grep 'constexpr const char \*VERSION' "$METADATA_FILE" | sed 's/.*"\(.*\)".*/\1/')
        
        # Parse version components
        IFS='.' read -ra VERSION_PARTS <<< "$CURRENT_VERSION"
        MAJOR="${VERSION_PARTS[0]}"
        MINOR="${VERSION_PARTS[1]}"
        PATCH="${VERSION_PARTS[2]}"
        
        # Increment patch version
        PATCH=$((PATCH + 1))
        NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
        
        # Update FirmwareMetadata.h
        sed -i.bak "s/constexpr const char \*VERSION = \".*\";/constexpr const char *VERSION = \"${NEW_VERSION}\";/" "$METADATA_FILE"
        rm "$METADATA_FILE.bak"
        
        echo -e "  ${YELLOW}Version: ${CURRENT_VERSION} → ${NEW_VERSION}${NC}"
    fi
    
    # Compile
    echo -e "${YELLOW}Compiling...${NC}"
    
    if arduino-cli compile \
        --fqbn teensy:avr:teensy41 \
        --output-dir "$OUTPUT_DIR" \
        --libraries "$SCRIPT_DIR/Custom Libraries" \
        "$ino_file" >> "$LOG_FILE" 2>&1; then
        
        # Find generated HEX file
        hex_file=$(find "$OUTPUT_DIR" -name "${controller_name}.ino.hex" | head -n 1)
        
        if [ -f "$hex_file" ]; then
            hex_size=$(du -h "$hex_file" | cut -f1)
            echo -e "${GREEN}✓ SUCCESS${NC} - HEX: $hex_size"
            ((SUCCESS++))
        else
            echo -e "${RED}✗ FAILED${NC} - No HEX file generated"
            FAILED_CONTROLLERS+=("$controller_name")
            ((FAILED++))
        fi
    else
        echo -e "${RED}✗ FAILED${NC} - Compilation error (see log)"
        FAILED_CONTROLLERS+=("$controller_name")
        ((FAILED++))
    fi
    
    echo ""
done

# Summary
echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║  Compilation Summary                                           ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Total Controllers:${NC} $TOTAL"
echo -e "${GREEN}Successful:${NC} $SUCCESS"
echo -e "${RED}Failed:${NC} $FAILED"
echo -e "${YELLOW}Skipped:${NC} $SKIPPED"
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed Controllers:${NC}"
    for controller in "${FAILED_CONTROLLERS[@]}"; do
        echo -e "  ${RED}✗${NC} $controller"
    done
    echo ""
fi

echo -e "${BLUE}Output Directory:${NC} $OUTPUT_DIR"
echo -e "${BLUE}Log File:${NC} $LOG_FILE"
echo ""

if [ $SUCCESS -eq $TOTAL ] && [ $TOTAL -gt 0 ]; then
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║  ALL CONTROLLERS COMPILED SUCCESSFULLY! 🎉                    ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
    exit 0
elif [ $SUCCESS -gt 0 ]; then
    echo -e "${YELLOW}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${YELLOW}║  PARTIAL SUCCESS - Some controllers failed                    ║${NC}"
    echo -e "${YELLOW}╚════════════════════════════════════════════════════════════════╝${NC}"
    exit 1
else
    echo -e "${RED}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║  ALL COMPILATIONS FAILED                                       ║${NC}"
    echo -e "${RED}╚════════════════════════════════════════════════════════════════╝${NC}"
    exit 2
fi
