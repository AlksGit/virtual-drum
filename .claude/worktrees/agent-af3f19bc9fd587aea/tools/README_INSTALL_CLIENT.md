# NoScan — Raspberry Pi Installation

This folder contains **all the software required** to run the NoScan
demonstration on a Raspberry Pi 5. No access to the source code repository is
needed: everything is included here.

---

## 1. Package contents

```
Septodont-NoScan-software-deliverables/
├── README_INSTALL_CLIENT.md    ← this file (read it first)
├── VERSION.txt                 ← delivered version (build date + code reference)
├── App/                        ← the software
│   ├── NOSCAN_app_detection_algo.py    detection algorithm
│   ├── detection_config.json           detection parameters
│   ├── noscan-tooth.jpg                image used by the interface
│   ├── requirements.txt                dependencies (Python 3.11)
│   ├── requirements-linux.txt          dependencies (Python 3.13)
│   └── web/
│       ├── noscan_server.py            web server
│       ├── requirements-server.txt     server dependencies
│       ├── templates/                  user interface (2D and 3D views)
│       ├── static/                     images, 3D model, libraries
│       └── scripts/                    installation and startup scripts
└── docs/
    ├── INSTALL_RPI.md          detailed technical documentation
    ├── user_manual.md          user manual (English)
    └── mode_emploi.md          mode d'emploi utilisateur (French)
```

---

## 2. Requirements

- **Raspberry Pi 5** with a microSD card (16 GB minimum).
- **Raspberry Pi OS 64-bit**, freshly installed, with:
  - the **hostname** set to **`noscan`** (set it in Raspberry Pi Imager);
  - a user account created and the first boot completed.
- An **internet connection** (Wi-Fi or Ethernet) during installation only:
  system packages and Python libraries are downloaded.
- The **NoScan sensor matrix** connected over USB (it can also be plugged in
  after the installation).

> The `noscan` hostname matters: it is what makes the interface reachable at
> `http://noscan.local`.

---

## 3. Installation

### Step 1 — Copy the folder onto the Raspberry Pi

Transfer this package to the Pi (USB stick, `scp`, network share…) and unpack
it at the following location:

```
~/NoScan/magnet_file_detection
```

From the Pi, once the `.zip` archive has been copied into the home directory:

```bash
mkdir -p ~/NoScan
unzip ~/Septodont-NoScan-software-deliverables-*.zip -d ~/NoScan
mv ~/NoScan/Septodont-NoScan-software-deliverables ~/NoScan/magnet_file_detection
```

> If the `unzip` command is not available (Raspberry Pi OS Lite image), install
> it first: `sudo apt update && sudo apt install -y unzip`

Check that the following path exists before continuing:

```bash
ls ~/NoScan/magnet_file_detection/App/web/scripts/provision_pi.sh
```

> **This location matters.** The installation scripts resolve their own
> position automatically, but the startup service is configured for this path.

### Step 2 — Run the automated installation

```bash
bash ~/NoScan/magnet_file_detection/App/web/scripts/provision_pi.sh
```

The script performs the full installation and **verifies every step**:

1. required system packages;
2. hostname, serial port activation, access rights;
3. Python environment and library installation;
4. `NoScan-AP` Wi-Fi access point;
5. automatic startup of the web server;
6. kiosk mode (full-screen display on the HDMI output).

Allow **15 to 30 minutes** depending on the internet connection. On error, the
script stops and states both the cause and the fix; it can safely be re-run as
many times as needed.

A full log is written to `~/noscan-provision.log`.

### Step 3 — Reboot

At the end, the script offers to reboot: **accept it**. The reboot is required
to apply the hostname, the serial port and the automatic startup.

---

## 4. Usage

After the reboot, the Raspberry Pi starts **automatically**:

- the screen connected over HDMI shows the NoScan interface in full screen;
- the Pi broadcasts its own Wi-Fi network.

To use a tablet or a laptop:

| | |
|---|---|
| **Wi-Fi network** | `NoScan-AP` |
| **Password** | `2026No_Scan` |
| **Address** | `http://noscan.local` |

The complete user manual (calibration, use with a file, 2D and 3D views) is in
**`docs/user_manual.md`** — version française dans **`docs/mode_emploi.md`**.

---

## 5. Maintenance

**Reconnect the Pi to the internet** (the access point is active by default,
which prevents internet access):

```bash
bash ~/NoScan/magnet_file_detection/App/web/scripts/stop_ap.sh
sudo reboot
```

To re-enable the access point:

```bash
bash ~/NoScan/magnet_file_detection/App/web/scripts/start_ap.sh
```

**Check that the server is running**:

```bash
systemctl status noscan-web.service
journalctl -u noscan-web.service -n 50
```

**Restart the server**:

```bash
sudo systemctl restart noscan-web.service
```

---

## 6. Technical documentation

`docs/INSTALL_RPI.md` describes the installation **step by step, manually**, as
well as the advanced configuration (access point, system service, kiosk mode,
SD card cloning). It is useful to understand or troubleshoot the installation.

> **Note**: that document is the original project documentation, and it is
> written in French. Its section **4.2 ("Cloner le repo")** does not apply to
> this package — it is replaced by **step 1** of this README, since the code is
> already provided in this folder. All other sections remain valid.
