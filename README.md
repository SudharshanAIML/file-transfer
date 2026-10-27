# LANShare

**Zero-install, browser-based local network file sharing.**  
Run one command → browser opens → pick folders → anyone on your Wi-Fi can download.

---

## How it works

```
You run:  python3 lanshare.py
          ↓
Browser opens at http://localhost:7070

You pick a folder  →  all files appear in the web UI
Someone else on the same Wi-Fi visits http://192.168.x.x:7070
They see every file and click Download  →  file saves to their laptop
```

No installation. No accounts. No internet required.

---

## Quick Start

```bash
python3 lanshare.py
```

The browser opens automatically. To share files:

1. Click **"Click to choose a folder"**
2. Select a folder (e.g. Videos, Documents, Downloads)
3. All files inside appear in the list instantly
4. Tell others to open the **Network URL** shown in the terminal

---

## Options

```bash
python3 lanshare.py --port 8080       # use a different port
python3 lanshare.py --no-browser      # don't auto-open browser
```

---

## Features

| Feature | Details |
|---|---|
| 📂 Folder sharing | Pick any folder — all files listed recursively |
| 🔍 Search | Filter files by name instantly |
| ⬇️ One-click download | Streams directly to visitor's browser |
| 🖼️ File icons | Auto-detected by extension (video, audio, image, etc.) |
| 🌐 Any device | Works on any browser — laptop, phone, tablet |
| 🗑️ Remove shares | Click × on a folder to stop sharing it |
| 🔄 Auto-refresh | Viewer page updates every 5 seconds |
| 🔒 Path sanitization | `..` path traversal blocked server-side |

---

## Requirements

- Python 3.8+  *(pre-installed on all Linux/macOS systems)*
- No pip packages needed — uses only the standard library

---

## Network access

All devices must be on the **same Wi-Fi or LAN**.  
The terminal will print your network URL, e.g.:

```
  ║  Network: http://192.168.1.42:7070       ║
```

Share that URL with anyone on the same network.

---

## Where files are stored

Uploaded files are cached in `~/.lanshare_uploads/`.  
You can safely delete this folder when done.