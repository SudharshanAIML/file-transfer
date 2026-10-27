# LANShare

**Fast, direct peer-to-peer file transfer over your local network.**  
No cloud. No accounts. No internet required.

---

## Features (v1.0)

| Feature | Status |
|---|---|
| Auto device discovery (UDP broadcast) | ✅ |
| Direct TCP file transfer | ✅ |
| Transfer multiple files / entire folders | ✅ |
| Sender + Receiver on every device | ✅ |
| Live progress: speed, file name, bytes | ✅ |
| Incoming transfer consent dialog | ✅ |
| System tray (minimize to background) | ✅ |
| Path traversal protection | ✅ |
| 512 KiB streaming chunks | ✅ |

---

## Architecture

```
Laptop A ←── Same Wi-Fi ──→ Laptop B
TCP :45454                  TCP :45454
UDP :45454                  UDP :45454

Discovery:  UDP broadcast every 2 s
Transfer:   direct TCP stream, framed messages
```

Every device is both sender and receiver. No relay server needed.

---

## Build

### Requirements

- CMake ≥ 3.22  
- Qt 6.x **or** Qt 5.15 (auto-detected)  
- C++20-capable compiler (GCC 10+, Clang 12+, MSVC 2019+)

### Ubuntu / Debian

```bash
# Qt6 (preferred)
sudo apt install qt6-base-dev

# OR Qt5
sudo apt install qt5-default libqt5network5-dev qtbase5-dev

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

```bash
./build/LANShare
```

Open LANShare on **two or more devices on the same LAN**. They will discover each other automatically within a few seconds.

---

## Usage

1. **Devices tab** — shows all LANShare peers found on the LAN.
2. Click **Send Files →** next to a peer.
3. Browse and select files or folders (use the Quick Access shortcuts).
4. Click **Send Selected →**.
5. The receiver sees a consent dialog — they choose a save folder and click Yes.
6. Watch progress in the **Transfers** tab on both sides.

---

## Protocol

```
[1 byte MsgType] [4 bytes big-endian length] [payload bytes]
```

| Message | Direction | Payload |
|---|---|---|
| `TransferRequest` | → Receiver | JSON: deviceName, file list |
| `TransferAccepted` | → Sender | (empty) |
| `TransferRejected` | → Sender | (empty) |
| `FileHeader` | → Receiver | JSON: name, relPath, size |
| `FileData` | → Receiver | raw bytes (512 KiB chunk) |
| `FileAck` | → Sender | file fully written |
| `TransferComplete` | → Receiver | all files sent |

---

## Roadmap

- [ ] SHA-256 checksum verification  
- [ ] Pause / resume transfers  
- [ ] Session PIN pairing  
- [ ] Android (Qt for Android)  
- [ ] Transfer history  