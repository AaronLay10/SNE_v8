#!/bin/bash
#
# Clean HEX_OUTPUT Directory
# Removes all build artifacts except .hex files (or all if --all flag is used)
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/HEX_OUTPUT"

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}Cleaning HEX_OUTPUT Directory${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

if [ ! -d "${OUTPUT_DIR}" ]; then
    echo -e "${YELLOW}HEX_OUTPUT directory does not exist${NC}"
    exit 0
fi

if [ "$1" == "--all" ]; then
    # Remove everything
    echo -e "${YELLOW}Removing ALL files from HEX_OUTPUT...${NC}"
    rm -rf "${OUTPUT_DIR}"/*
    echo -e "${GREEN}✓ All files removed${NC}"
else
    # Remove only build artifacts, keep .hex files
    echo -e "${YELLOW}Removing build artifacts (.eep, .elf, .lst, .sym)...${NC}"
    find "${OUTPUT_DIR}" -type f \( -name "*.eep" -o -name "*.elf" -o -name "*.lst" -o -name "*.sym" \) -delete
    
    ARTIFACTS_REMOVED=$(find "${OUTPUT_DIR}" -type f \( -name "*.eep" -o -name "*.elf" -o -name "*.lst" -o -name "*.sym" \) | wc -l | xargs)
    HEX_FILES=$(find "${OUTPUT_DIR}" -type f -name "*.hex" | wc -l | xargs)
    
    echo -e "${GREEN}✓ Build artifacts removed${NC}"
    echo -e "${GREEN}✓ Kept ${HEX_FILES} .hex file(s)${NC}"
fi

echo ""
echo -e "${CYAN}Usage:${NC}"
echo "  ./clean_hex_output.sh       # Remove build artifacts only (keep .hex files)"
echo "  ./clean_hex_output.sh --all # Remove everything"
