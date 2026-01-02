#!/bin/bash
# Raspberry Pi Touchscreen Kiosk Mode Launcher
# Place this script in /home/pi/start-touchscreen.sh
# Make executable: chmod +x /home/pi/start-touchscreen.sh
# Add to autostart: echo "@/home/pi/start-touchscreen.sh" >> ~/.config/lxsession/LXDE-pi/autostart

# Wait for network and X11 to be ready
sleep 10

# Disable screen blanking and screensaver
export DISPLAY=:0
xset s off
xset -dpms
xset s noblank

# Hide mouse cursor after 1 second of inactivity
unclutter -idle 1 &

# Start Chromium in kiosk mode pointing to the touchscreen interface
chromium-browser \
  --kiosk \
  --no-sandbox \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --disable-component-extensions-with-background-pages \
  --disable-features=TranslateUI \
  --disable-ipc-flooding-protection \
  --ignore-certificate-errors \
  --ignore-ssl-errors \
  --ignore-certificate-errors-spki-list \
  --disable-web-security \
  --allow-running-insecure-content \
  --window-size=1280,800 \
  --window-position=0,0 \
  https://sentientengine.ai/kiosk