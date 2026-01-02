# Teensy Hardware Compilation Scripts

## Quick Start

### Compile Single Controller

```bash
./compile_teensy.sh "Controller Code Teensy/main_lighting_v2/main_lighting_v2.ino"
```

### Compile All V2 Controllers

```bash
./compile_all_v2.sh
```

### Clean Build Artifacts

```bash
# Remove .eep, .elf, .lst, .sym files (keep .hex)
./clean_hex_output.sh

# Remove everything including .hex files
./clean_hex_output.sh --all
```

## Features

### Automatic Version Bumping

- Each compilation automatically increments the patch version in `FirmwareMetadata.h`
- Format: `MAJOR.MINOR.PATCH` (e.g., `2.3.4` → `2.3.5`)

### Build Artifact Cleanup

- **Individual compile**: Removes old artifacts for that controller before compiling
- **Mass compile**: Cleans entire `HEX_OUTPUT/` directory before starting
- **Manual cleanup**: Use `clean_hex_output.sh` to remove artifacts anytime

### Output Structure

```
HEX_OUTPUT/
  ├── controller_name.ino.hex  # Flash file (kept)
  ├── controller_name.ino.eep  # EEPROM data (cleaned)
  ├── controller_name.ino.elf  # Executable (cleaned)
  ├── controller_name.ino.lst  # Listing file (cleaned)
  └── controller_name.ino.sym  # Symbol table (cleaned)
```

## Flashing

### Using Teensy Loader GUI

1. Open Teensy Loader application
2. File → Open HEX File → Select from `HEX_OUTPUT/`
3. Press button on Teensy to flash

### Using Command Line

```bash
teensy_loader_cli --mcu=TEENSY41 -w HEX_OUTPUT/controller_name.ino.hex
```

## Git Best Practices

**Only commit `.hex` files to Git:**

- Build artifacts (`.eep`, `.elf`, `.lst`, `.sym`) are automatically cleaned
- Run `./clean_hex_output.sh` before committing
- Mass compile runs cleanup automatically

## Troubleshooting

### Arduino CLI Not Found

```bash
brew install arduino-cli
```

### Teensy Platform Not Installed

The script will automatically install it on first run.

### Compilation Errors

- Check library paths in `Custom Libraries/`
- Verify all `.h` files are present in controller directory
- Review error output for missing dependencies
