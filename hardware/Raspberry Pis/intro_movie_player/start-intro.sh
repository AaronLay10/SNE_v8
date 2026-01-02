#!/bin/bash
# Launcher script with proper environment

# Set display for video output
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/gdm/Xauthority

# Optional: Force HDMI output
tvservice -p

# Change to script directory
cd /home/pi/scripts

# Run the player
python3 intro-player.py
