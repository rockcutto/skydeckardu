# SkyDeck

![SkyDeck Mounted on Steam Deck](display.png)

**SkyDeck** turns your Steam Deck into a fully-integrated drone controller: combining RC link, MAVLink ground station, and low-latency FPV in one portable unit.

---

## 📦 Hardware

*Current Bill of Materials (subject to change after full testing):*
- **Steam Deck**  
- **Happymodel ES24 TX** (ExpressLRS module)  
- **ESP32-S2 Mini** (joystick→CRSF adapter)  
- **Walksnail VRX** video receiver  
- **HDMI USB capture card**  
- **3D-printed backpack & mounts**  
  → CAD files on Onshape:  
  https://cad.onshape.com/documents/0a85f5b80c6099a2fc1cf05d/w/0408ca52d32ec3c9c9f8f564/e/62ef1ad992c53a1e1d5da3ef

---

## 🎮 RC & MAVLink

1. **Python sender** (`host/skydeck_joystick_sender.py`):  
   - Reads Steam Deck controller via `inputs` (evdev).  
   - Normalizes 8 channels (LY, LX, RY, RX, LT, RT, LB, RB) to 0–800.  
   - Streams `000800400…:` over USB-CDC at 100 Hz.
2. **ESP32-S2 firmware** (`esp32/`):  
   - Parses incoming 3-digit channels + `:`.  
   - Packs into CRSF frames.  
   - Sends UART2→ExpressLRS module (RC + MAVLink on one link).

---

## 📹 FPV Video

- **Walksnail VRX** provides low-latency HDMI output.  
- **USB capture card** feeds HDMI into Steam Deck’s Desktop Mode.  
- View video in a window alongside your GCS.

---

## 🖥️ Software & GUI

1. **Ground Control Station**  
   - Mission Planner under Mono on Steam Deck.  
   - Install via AUR:  
     ```bash
     yay -S ardupilot-mission-planner
     ```
2. **Host tooling** (`host/` folder):  
   - `install.sh` → creates `skydeck_env` venv & installs dependencies.  
   - `deck.sh` → auto-activates venv, runs the sender script.

---

## 🚀 Quick Start

1. **Flash ESP32-S2**  
   ```bash
   cd esp32
   idf.py -p /dev/ttyUSB0 flash monitor
Setup host

```bash
    ./install.sh
    chmod +x deck.sh
```
