# Getting Started from Scratch

This guide walks you through setting up Roon Knob step-by-step, assuming no prior experience with Docker or embedded devices.

## What You're Setting Up

Roon Knob has two parts that work together:

1. **The Knob** - A small physical device ([Waveshare ESP32-S3 Knob](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm)) that sits on your desk. You'll install custom firmware on it.

2. **The Bridge** - A small program that runs on your network (on a Raspberry Pi, NAS, or any always-on computer). It talks to Roon and tells the knob what's playing.

```
┌──────────┐      WiFi       ┌──────────┐     Roon API    ┌──────────┐
│   Knob   │ ◄────────────► │  Bridge  │ ◄──────────────► │   Roon   │
│ (device) │                 │ (Docker) │                  │  (Core)  │
└──────────┘                 └──────────┘                  └──────────┘
```

---

## Part 1: Flash the Firmware onto the Knob

"Flashing" means copying software onto the device. Unlike a Raspberry Pi (which uses an SD card), the ESP32 chips store their software internally. You flash once over USB, then future updates happen automatically over WiFi.

### Understanding the Hardware

The Waveshare knob contains **two separate chips**:

| Chip | Firmware | What it does |
|------|----------|--------------|
| **ESP32-S3** | `roon_knob.bin` | Main chip: display, WiFi, touch, Roon control |
| **ESP32** | `esp32_bt.bin` | Bluetooth chip: BLE HID controls + AVRCP metadata |

**You must flash at least the ESP32-S3** (the main firmware). The ESP32 firmware is only needed if you want to use Bluetooth mode (controlling your phone/DAP when away from Roon).

### How USB Works on This Device

The knob has a clever trick: **flipping the USB-C cable switches which chip you're programming**. The connector works in either orientation, but each orientation connects to a different chip.

- One orientation → ESP32-S3 (main chip)
- Flip the cable → ESP32 (Bluetooth chip)

You'll figure out which is which by the port name that appears (explained below).

### What You Need

- The Waveshare ESP32-S3 Knob
- A USB-C cable (data-capable, not charge-only)
- A computer (Windows, Mac, or Linux)

### Step 1: Install Python and esptool

**esptool** is a program that sends firmware to ESP32 devices. It's written in Python.

#### On Mac:

```bash
# Check if Python is installed
python3 --version

# If not installed, install it via Homebrew:
brew install python

# Install esptool
pip3 install esptool
```

#### On Windows:

1. Download Python from [python.org](https://www.python.org/downloads/)
2. **Important**: During installation, check "Add Python to PATH"
3. Open Command Prompt and run:
   ```cmd
   pip install esptool
   ```

#### On Linux:

```bash
sudo apt update
sudo apt install python3 python3-pip
pip3 install esptool
```

### Step 2: Download the Firmware

Go to the [latest release](https://github.com/muness/roon-knob/releases/latest) and download:

- **`roon_knob.bin`** - Required (main firmware for ESP32-S3)
- **`esp32_bt.bin`** - Optional (Bluetooth chip firmware, needed for Bluetooth mode)

Save them somewhere easy to find (like your Downloads folder).

### Step 3: Connect the Knob

1. Plug the USB-C cable into the knob
2. Plug the other end into your computer
3. The knob might turn on and show something on screen—that's fine

### Step 4: Find the USB Port and Identify the Chip

When you plug in the knob, your computer assigns it a "port" name. The port name helps identify which chip you're connected to.

#### On Mac:

```bash
ls /dev/tty.usb*
```

You'll see something like:
- `/dev/tty.usbmodem101` - This is the **ESP32-S3** (main chip)
- `/dev/tty.usbserial-1410` - This is the **ESP32** (Bluetooth chip)

If nothing appears, try flipping the USB-C cable or use a different cable (some only charge).

#### On Windows:

1. Open Device Manager (search for it in the Start menu)
2. Expand "Ports (COM & LPT)"
3. Look for:
   - "USB Serial Device (COMx)" - likely **ESP32-S3**
   - "Silicon Labs CP210x (COMx)" - likely **ESP32**
4. Note the COM number (e.g., `COM3`)

#### On Linux:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

You'll see something like:
- `/dev/ttyACM0` - likely **ESP32-S3**
- `/dev/ttyUSB0` - likely **ESP32**

#### Not Sure Which Chip?

Just try flashing. If esptool reports "Chip is ESP32-S3", you're on the main chip. If it says "Chip is ESP32", you're on the Bluetooth chip. Flip the USB-C cable to switch.

### Step 5: Flash the Main Firmware (ESP32-S3)

Open a terminal/command prompt and navigate to where you downloaded the firmware:

```bash
cd ~/Downloads   # or wherever you saved it
```

Make sure you're connected to the **ESP32-S3** (see Step 4), then run:

#### On Mac/Linux:

```bash
esptool.py --chip esp32s3 -p YOUR_PORT -b 460800 \
  --before default-reset --after hard-reset \
  write_flash 0x10000 roon_knob.bin
```

Example with a real port:
```bash
esptool.py --chip esp32s3 -p /dev/tty.usbmodem101 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash 0x10000 roon_knob.bin
```

#### On Windows:

```cmd
esptool.py --chip esp32s3 -p COM3 -b 460800 --before default-reset --after hard-reset write_flash 0x10000 roon_knob.bin
```

### What Success Looks Like

You should see output like:
```
esptool.py v4.x.x
Serial port /dev/tty.usbmodem101
Connecting...
Chip is ESP32-S3
...
Writing at 0x00010000... (100 %)
Wrote 1234567 bytes at 0x00010000 in 12.3 seconds
Hard resetting via RTS pin...
```

The knob will restart and show "WiFi: Setup Mode" on its screen. That's perfect!

### Step 6: Flash the Bluetooth Firmware (ESP32) - Optional

**Skip this step** if you only plan to use the knob with Roon. The Bluetooth firmware is needed for Bluetooth mode, which lets you control your phone/DAP when you're away from your Roon setup.

1. **Flip the USB-C cable** to connect to the ESP32 chip
2. Verify you see a different port name (e.g., `/dev/tty.usbserial-*` instead of `/dev/tty.usbmodem*`)
3. Flash the Bluetooth firmware:

#### On Mac/Linux:

```bash
esptool.py --chip esp32 -p YOUR_PORT -b 460800 \
  --before default-reset --after hard-reset \
  write_flash 0x10000 esp32_bt.bin
```

#### On Windows:

```cmd
esptool.py --chip esp32 -p COM4 -b 460800 --before default-reset --after hard-reset write_flash 0x10000 esp32_bt.bin
```

Note: The chip type is `esp32` (not `esp32s3`) for this firmware.

### Troubleshooting

| Problem | Solution |
|---------|----------|
| "Port not found" | Try a different USB cable. Some cables only charge, they don't transmit data. |
| "Permission denied" (Linux) | Add yourself to the dialout group: `sudo usermod -a -G dialout $USER` then log out and back in |
| Nothing happens when plugging in | Try a different USB port on your computer |
| esptool command not found | Make sure Python's Scripts folder is in your PATH. Try `python -m esptool` instead of `esptool.py` |

---

## Part 2: Run the Bridge

The bridge is a small program that connects Roon to your knob. It runs in Docker, which is like a lightweight container that packages everything the program needs.

### What is Docker?

Think of Docker as a shipping container for software. Instead of installing programs directly on your computer (and dealing with dependencies, versions, etc.), Docker packages everything into a container that "just works."

### Step 1: Install Docker

#### On Raspberry Pi:

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
```

**Important**: Log out and log back in after running these commands.

#### On Mac:

Download and install [Docker Desktop for Mac](https://www.docker.com/products/docker-desktop/).

#### On Windows:

Download and install [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/).

#### On a NAS (Synology, QNAP, etc.):

Most modern NAS devices have Docker support built in. Check your NAS's package center for "Docker" or "Container Station."

### Step 2: Create the Configuration File

Docker Compose uses a configuration file to know what to run. You need to create this file.

1. Create a folder for your Roon Knob configuration:
   ```bash
   mkdir -p ~/roon-knob
   cd ~/roon-knob
   ```

2. Create a file called `docker-compose.yml` (you can use any text editor):

   **On Mac/Linux:**
   ```bash
   nano docker-compose.yml
   ```

   **On Windows (PowerShell):**
   ```powershell
   notepad docker-compose.yml
   ```

3. Paste the following content into the file:

   ```yaml
   services:
     roon-knob-bridge:
       image: muness/roon-extension-knob:latest
       restart: unless-stopped
       network_mode: host
       volumes:
         - roon-knob-bridge-data:/home/node/app/data

   volumes:
     roon-knob-bridge-data:
   ```

4. Save and close the file
   - In nano: Press `Ctrl+O` to save, then `Ctrl+X` to exit
   - In Notepad: File → Save

### Step 3: Start the Bridge

In the same folder where you created `docker-compose.yml`, run:

```bash
docker compose up -d
```

This downloads the bridge image (first time only) and starts it running in the background.

### What Success Looks Like

```
[+] Running 1/1
 ✔ Container roon-knob-roon-knob-bridge-1  Started
```

To check if it's running:
```bash
docker compose ps
```

To see the logs:
```bash
docker compose logs -f
```

(Press `Ctrl+C` to stop watching logs)

---

## Part 3: Connect Everything

### Step 1: Authorize the Bridge in Roon

1. Open the Roon app on your phone, tablet, or computer
2. Go to **Settings** (gear icon)
3. Go to **Extensions**
4. Find **"Roon Knob Bridge"** and click **Enable**

### Step 2: Connect the Knob to WiFi

1. The knob should show "WiFi: Setup Mode" on its screen
2. On your phone or computer, look for a WiFi network called **"roon-knob-setup"**
3. Connect to that network
4. A setup page should appear automatically (if not, open a browser and go to `192.168.4.1`)
5. Enter your home WiFi name and password
6. The knob will restart and connect to your WiFi

### Step 3: Done!

The knob will automatically find the bridge on your network using mDNS. After a few seconds, you should see "Extension: Connected" and then your now-playing information.

---

## Troubleshooting

### Knob shows "Extension: Searching..."

The knob can't find the bridge. Check:
- Is the bridge running? (`docker compose ps`)
- Is the bridge authorized in Roon?
- Are the knob and bridge on the same network?
- Some networks block mDNS. Try entering the bridge URL manually (long-press the zone name on the knob to access Settings)

### Bridge won't start

Check the logs for errors:
```bash
docker compose logs
```

### Knob won't connect to WiFi

- Make sure you're entering the correct password
- Try moving the knob closer to your router during initial setup
- If the knob gets stuck, you can reset it by re-flashing the firmware

### Nothing shows in Roon Extensions

- Make sure Docker is running
- Make sure you ran `docker compose up -d` in the correct folder
- Try restarting: `docker compose restart`

---

## Updating

### Updating the Knob Firmware

After the initial setup, firmware updates happen automatically. When a new version is available, the knob will download and install it over WiFi.

### Updating the Bridge

```bash
cd ~/roon-knob
docker compose pull
docker compose up -d
```

---

## Getting Help

- [GitHub Issues](https://github.com/muness/roon-knob/issues) - Report bugs or ask questions
- [Roon Community Discussion](https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363) - Chat with other users
