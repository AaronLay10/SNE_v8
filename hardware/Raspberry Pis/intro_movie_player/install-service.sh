#!/bin/bash
# Clean up any running intro-player processes and install systemd service

echo "Checking for running intro-player processes..."

# Find and kill any existing intro-player.py processes
PIDS=$(pgrep -f "intro-player.py")
if [ -n "$PIDS" ]; then
    echo "Found running processes: $PIDS"
    echo "Killing existing intro-player processes..."
    pkill -f "intro-player.py"
    sleep 2
    
    # Force kill if still running
    if pgrep -f "intro-player.py" > /dev/null; then
        echo "Force killing..."
        pkill -9 -f "intro-player.py"
    fi
    echo "✓ Cleaned up old processes"
else
    echo "No existing processes found"
fi

# Kill any orphaned MPV processes from intro player
MPV_PIDS=$(pgrep -f "mpv.*clockwork-loop")
if [ -n "$MPV_PIDS" ]; then
    echo "Cleaning up MPV processes: $MPV_PIDS"
    pkill -f "mpv.*clockwork-loop"
fi

echo ""
echo "Installing systemd service..."

# Copy service file
sudo cp intro-player.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Stop service if it was already running
sudo systemctl stop intro-player 2>/dev/null

# Enable service to start on boot
sudo systemctl enable intro-player

# Start the service
sudo systemctl start intro-player

# Wait a moment for startup
sleep 2

# Show status
sudo systemctl status intro-player --no-pager

echo ""
echo "✓ Service installed and running!"
echo ""
echo "Useful commands:"
echo "  sudo systemctl status intro-player   # Check status"
echo "  sudo systemctl restart intro-player  # Restart service"
echo "  sudo journalctl -u intro-player -f   # Follow logs"
echo "  sudo systemctl stop intro-player     # Stop service"
