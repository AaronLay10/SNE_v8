#!/usr/bin/env bash

# Compile all v2 controllers
# Shows progress and summary at the end

set +e  # Don't exit on errors, we want to continue through all controllers

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Compiling All V2 Controllers${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Clean entire HEX_OUTPUT directory before mass compile
OUTPUT_DIR="${SCRIPT_DIR}/HEX_OUTPUT"
if [ -d "${OUTPUT_DIR}" ]; then
  echo -e "${YELLOW}Cleaning HEX_OUTPUT directory...${NC}"
  rm -rf "${OUTPUT_DIR}"/*
  echo ""
fi

SUCCESS_COUNT=0
FAILED_COUNT=0
SKIPPED_COUNT=0
SUCCESS_LIST=()
FAILED_LIST=()

# Find all v2 controller directories
for controller_dir in "${SCRIPT_DIR}/Controller Code Teensy"/*_v2; do
  if [ ! -d "$controller_dir" ]; then
    continue
  fi
  
  controller_name=$(basename "$controller_dir")
  ino_file="Controller Code Teensy/${controller_name}/${controller_name}.ino"
  
  # Skip if .ino file doesn't exist
  if [ ! -f "$controller_dir/${controller_name}.ino" ]; then
    echo -e "${YELLOW}⊘ SKIP: $controller_name (no .ino file)${NC}"
    ((SKIPPED_COUNT++))
    continue
  fi
  
  echo -e "${YELLOW}=== Compiling $controller_name ===${NC}"
  
  if "${SCRIPT_DIR}/compile_teensy.sh" "$ino_file"; then
    echo -e "${GREEN}✓ SUCCESS: $controller_name${NC}"
    ((SUCCESS_COUNT++))
    SUCCESS_LIST+=("$controller_name")
  else
    echo -e "${RED}✗ FAILED: $controller_name${NC}"
    ((FAILED_COUNT++))
    FAILED_LIST+=("$controller_name")
  fi
  
  echo ""
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Compilation Summary${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}✓ Successful: $SUCCESS_COUNT${NC}"
echo -e "${RED}✗ Failed: $FAILED_COUNT${NC}"
echo -e "${YELLOW}⊘ Skipped: $SKIPPED_COUNT${NC}"
echo ""

if [ ${#SUCCESS_LIST[@]} -gt 0 ]; then
  echo -e "${GREEN}Successfully compiled:${NC}"
  for controller in "${SUCCESS_LIST[@]}"; do
    echo -e "  ${GREEN}✓${NC} $controller"
  done
  echo ""
fi

if [ ${#FAILED_LIST[@]} -gt 0 ]; then
  echo -e "${RED}Failed to compile:${NC}"
  for controller in "${FAILED_LIST[@]}"; do
    echo -e "  ${RED}✗${NC} $controller"
  done
  echo ""
fi

echo -e "${GREEN}========================================${NC}"

# Exit with error code if any failed
if [ $FAILED_COUNT -gt 0 ]; then
  exit 1
else
  exit 0
fi
