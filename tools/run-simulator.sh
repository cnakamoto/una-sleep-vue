#!/bin/bash
# Build (if needed) and run the SleepVue TouchGFX simulator in a Linux
# container, with the GUI window served over noVNC.
#
#   tools/run-simulator.sh [--rebuild]
#
# Then open http://localhost:6080/vnc.html in a browser.
# Watch buttons are keyboard keys: 1=L1 2=L2 3=R1 4=R2; F1 saves a
# screenshot (screenshots/ next to the app), F10 toggles continuous.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="SleepVue/Software/Apps/TouchGFX-GUI"
IMAGE="una-sim"
DOCKER="${DOCKER:-$HOME/.orbstack/bin/docker}"

if [ "${1:-}" = "--rebuild" ]; then
    "$DOCKER" build --platform linux/amd64 -f "$REPO_ROOT/tools/Dockerfile.sim" -t "$IMAGE" "$REPO_ROOT"
fi

if ! "$DOCKER" image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "Building $IMAGE image (one-time)..."
    "$DOCKER" build --platform linux/amd64 -f "$REPO_ROOT/tools/Dockerfile.sim" -t "$IMAGE" "$REPO_ROOT"
fi

exec "$DOCKER" run --rm -it --platform linux/amd64 \
    -v "$REPO_ROOT:/work" -w "/work/$APP_DIR" -p 6080:6080 \
    -e TZ=America/New_York \
    "$IMAGE" bash -exc '
        set -e
        # Build the simulator if the binary is missing or sources are newer.
        if [ ! -x build/bin/simulator.out ] || [ -n "$(find gui simulator -name "*.cpp" -newer build/bin/simulator.out 2>/dev/null)" ]; then
            echo "== building simulator =="
            UNA_SDK=/work/una-sdk make -f simulator/gcc/Makefile -j"$(nproc)"
        fi

        # Seed the simulated watch flash (service reads slp_last.bin /
        # slp_idx.bin from /Output inside the container).
        mkdir -p /Output
        cp -f /work/tools/sim-fs/* /Output/ 2>/dev/null || true

        echo "== starting Xvfb + x11vnc + noVNC =="
        Xvfb :0 -screen 0 800x600x24 &
        sleep 1
        x11vnc -display :0 -nopw -listen 0.0.0.0 -forever -shared -bg -quiet
        websockify --web /usr/share/novnc 6080 localhost:5900 &
        sleep 1
        echo "== noVNC: http://localhost:6080/vnc.html (keys 1-4 = L1 L2 R1 R2, F1 = screenshot) =="
        DISPLAY=:0 exec ./build/bin/simulator.out
    '
