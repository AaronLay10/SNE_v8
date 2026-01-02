#!/usr/bin/env bash
#
# Batch compile all v2 Teensy controllers
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Output files
SUCCESS_LOG="compile_successes.txt"
FAILURE_LOG="compile_failures.txt"

# Clear logs
> "$SUCCESS_LOG"
> "$FAILURE_LOG"

echo "Starting batch compilation of v2 controllers..."
echo "================================================"
echo ""

# Find all v2 controllers
count=0
success=0
failed=0

for dir in "Controller Code Teensy"/*_v2; do
  if [ -d "$dir" ]; then
    controller_name=$(basename "$dir")
    ino_path="Controller Code Teensy/${controller_name}/${controller_name}.ino"
    
    if [ -f "$ino_path" ]; then
      count=$((count + 1))
      echo "[$count] Compiling $controller_name..."
      
      # Compile with error handling
      if ./compile_teensy.sh "$ino_path" > "/tmp/${controller_name}_build.log" 2>&1; then
        echo "✓ SUCCESS: $controller_name"
        echo "$controller_name" >> "$SUCCESS_LOG"
        success=$((success + 1))
      else
        echo "✗ FAILED: $controller_name"
        echo "$controller_name" >> "$FAILURE_LOG"
        failed=$((failed + 1))
      fi
      echo ""
    fi
  fi
done

echo "================================================"
echo "Compilation complete!"
echo "Total: $count controllers"
echo "Success: $success"
echo "Failed: $failed"
echo ""
echo "Successful builds listed in: $SUCCESS_LOG"
echo "Failed builds listed in: $FAILURE_LOG"
