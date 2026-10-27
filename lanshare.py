#!/usr/bin/env python3
"""
LANShare — browser-based LAN file sharing
Run: python3 lanshare.py
Then open http://localhost:PORT on your machine to share files.
Anyone on the same network can visit http://YOUR-IP:PORT to download.
"""

import http.server
import threading
import webbrowser
import json
import socket
import mimetypes
import urllib.parse
import argparse
import os
from pathlib import Path

# ── Configuration ───────────────────────────────────────────────────────────────
DEFAULT_PORT = 7070
UPLOAD_DIR   = Path.home() / ".lanshare_uploads"

# Shared state
shared_upload_dirs: dict[str, list[dict]] = {}
SERVER_PORT = DEFAULT_PORT

# ── Helpers ─────────────────────────────────────────────────────────────────────
def human_size(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} PB"

def get_all_files() -> list[dict]:
    return [f for files in shared_upload_dirs.values() for f in files]

def get_dirs_summary() -> list[dict]:
    result = []
    for name, files in shared_upload_dirs.items():
        total = sum(f["size"] for f in files)
        result.append({"name": name, "file_count": len(files), "total_size": human_size(total)})
    return result

def get_local_ips() -> list[str]:
    ips = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.append(s.getsockname()[0])
        s.close()
    except Exception:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None):
            ip = info[4][0]
            if ":" not in ip and not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except Exception:
        pass
    return ips

def find_free_port(start: int) -> int:
    for port in range(start, start + 200):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind(("", port))
                return port
            except OSError:
                continue
    raise RuntimeError("No free port found")

# ── HTML template ────────────────────────────────────────────────────────────────
# Uses __PORT__ and __IPS__ as substitution markers (avoids f-string brace issues)
HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LANShare — File Sharing</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d0d1a;--surface:#161626;--surface2:#1e1e32;--surface3:#252540;
  --border:#2a2a48;--accent:#667eea;--accent2:#764ba2;
  --green:#38ef7d;--red:#f953c6;--text:#e2e2f0;--muted:#6868a0;--radius:14px;
}
html{scroll-behavior:smooth}
body{font-family:'Inter',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column}

/* ── Header ── */
header{
  background:linear-gradient(135deg,#161626 0%,#12122a 100%);
  border-bottom:1px solid var(--border);
  padding:16px 32px;display:flex;align-items:center;gap:14px;
  position:sticky;top:0;z-index:200;backdrop-filter:blur(16px);
}
.logo{font-size:20px;font-weight:700;background:linear-gradient(90deg,#667eea,#a78bfa);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;letter-spacing:-.3px}
.tagline{color:var(--muted);font-size:12px}
header .spacer{flex:1}
.ip-badge{
  display:inline-flex;align-items:center;gap:6px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:20px;padding:5px 14px;font-size:12px;font-family:monospace;
  color:var(--accent);margin-left:6px;cursor:pointer;transition:all .2s;
}
.ip-badge:hover{background:var(--border);border-color:var(--accent)}
.ip-badge::before{content:"🌐";font-size:10px}

/* ── Layout ── */
.container{max-width:1140px;margin:0 auto;padding:28px 24px;width:100%;display:flex;flex-direction:column;gap:20px}

/* ── Panel ── */
.panel{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:22px}
.panel-header{display:flex;align-items:center;gap:10px;margin-bottom:18px}
.panel-title{font-size:15px;font-weight:600}
.pill{background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:20px;
  padding:2px 10px;font-size:11px;font-weight:700;color:#fff;letter-spacing:.3px}

/* ── Drop zone ── */
#dropzone{
  border:2px dashed var(--border);border-radius:var(--radius);padding:44px 24px;
  text-align:center;cursor:pointer;transition:all .25s;position:relative;
  background:rgba(102,126,234,.03);
}
#dropzone:hover{border-color:var(--accent);background:rgba(102,126,234,.07);transform:scale(1.005)}
#dropzone.drag-over{border-color:var(--green);background:rgba(56,239,125,.06)}
#dropzone.uploading{cursor:default;pointer-events:none;opacity:.7}
.dz-emoji{font-size:52px;margin-bottom:14px;display:block;transition:transform .3s}
#dropzone:hover .dz-emoji{transform:scale(1.1) rotate(-5deg)}
.dz-title{font-size:18px;font-weight:700;margin-bottom:6px;letter-spacing:-.2px}
.dz-sub{color:var(--muted);font-size:13px;line-height:1.5}
#folderInput{display:none}

/* ── Progress section ── */
#progressSection{display:none;margin-top:18px}
.progress-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.progress-label{font-size:13px;font-weight:600;color:var(--text)}
.progress-pct{font-size:20px;font-weight:700;color:var(--accent);font-variant-numeric:tabular-nums}
.progress-track{
  height:10px;background:var(--surface3);border-radius:99px;overflow:hidden;margin-bottom:10px;
  position:relative;
}
.progress-fill{
  height:100%;border-radius:99px;
  background:linear-gradient(90deg,var(--accent),var(--accent2));
  width:0%;transition:width .3s ease;
  position:relative;
}
.progress-fill::after{
  content:'';position:absolute;right:0;top:0;bottom:0;width:24px;
  background:linear-gradient(90deg,transparent,rgba(255,255,255,.3));
  border-radius:99px;animation:shimmer 1s infinite;
}
@keyframes shimmer{0%{opacity:0}50%{opacity:1}100%{opacity:0}}
.progress-stats{display:flex;justify-content:space-between;align-items:center;font-size:12px;color:var(--muted)}
.progress-file{font-size:12px;color:var(--muted);margin-top:6px;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:100%}
.progress-file span{color:var(--accent)}

/* ── Dir chips ── */
.dir-list{display:flex;flex-direction:column;gap:8px;margin-top:14px}
.dir-chip{
  display:flex;align-items:center;gap:10px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:10px;padding:10px 14px;font-size:13px;
  animation:fadeIn .25s ease;
}
.dir-chip .icon{font-size:18px;flex-shrink:0}
.dir-chip .info{flex:1;min-width:0}
.dir-chip .name{font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dir-chip .meta{color:var(--muted);font-size:11px;margin-top:2px}
.remove-btn{
  background:rgba(249,83,198,.1);border:1px solid rgba(249,83,198,.2);
  color:#f953c6;border-radius:8px;padding:4px 10px;font-size:12px;cursor:pointer;
  transition:all .2s;white-space:nowrap;
}
.remove-btn:hover{background:rgba(249,83,198,.25)}

/* ── Viewer notice ── */
.viewer-notice{
  background:rgba(56,239,125,.07);border:1px solid rgba(56,239,125,.2);
  border-radius:10px;padding:12px 18px;font-size:13px;color:var(--green);display:none;
}

/* ── Files top bar ── */
.files-topbar{display:flex;align-items:center;gap:10px;margin-bottom:18px;flex-wrap:wrap}
#countLabel{font-size:13px;color:var(--muted)}
#searchBox{
  flex:1;min-width:180px;max-width:340px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:8px;padding:7px 14px;color:var(--text);font-size:13px;
  outline:none;transition:border-color .2s;
}
#searchBox:focus{border-color:var(--accent)}
#searchBox::placeholder{color:var(--muted)}
.refresh-btn{
  display:flex;align-items:center;gap:6px;
  background:var(--surface2);border:1px solid var(--border);
  color:var(--text);border-radius:8px;padding:7px 14px;font-size:13px;cursor:pointer;
  transition:all .2s;
}
.refresh-btn:hover{background:var(--border)}

/* ── File grid ── */
.file-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:14px}
.file-card{
  background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);
  padding:18px;display:flex;flex-direction:column;gap:10px;
  transition:border-color .2s,transform .2s,box-shadow .2s;animation:fadeIn .3s ease;
}
.file-card:hover{border-color:var(--accent);transform:translateY(-3px);box-shadow:0 10px 40px rgba(102,126,234,.15)}
@keyframes fadeIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}
.file-top{display:flex;align-items:flex-start;gap:12px}
.file-icon{font-size:30px;flex-shrink:0;margin-top:2px}
.file-info{flex:1;min-width:0}
.file-name{font-weight:600;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:3px}
.file-size{color:var(--muted);font-size:12px}
.file-dir{
  display:inline-block;background:rgba(102,126,234,.12);color:var(--accent);
  border-radius:6px;padding:3px 10px;font-size:11px;font-weight:600;
  max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
}
.dl-btn{
  display:flex;align-items:center;justify-content:center;gap:8px;
  background:linear-gradient(90deg,var(--accent),var(--accent2));
  color:#fff;border:none;border-radius:10px;padding:10px 16px;
  font-size:13px;font-weight:700;cursor:pointer;text-decoration:none;
  transition:all .2s;width:100%;letter-spacing:.2px;
}
.dl-btn:hover{opacity:.88;transform:scale(1.02)}
.dl-btn:active{transform:scale(.97)}

/* ── Empty state ── */
.empty-state{text-align:center;padding:64px 20px;color:var(--muted);grid-column:1/-1}
.empty-state .e-emoji{font-size:52px;display:block;margin-bottom:14px}
.empty-state p{font-size:15px;line-height:1.7}

/* ── Toast ── */
#toast{
  position:fixed;bottom:28px;right:28px;max-width:340px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:12px;padding:12px 18px;font-size:13px;
  display:none;z-index:9999;box-shadow:0 12px 40px rgba(0,0,0,.5);
  animation:toastIn .2s ease;
}
@keyframes toastIn{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}

/* ── Scrollbar ── */
::-webkit-scrollbar{width:5px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:var(--border);border-radius:4px}
</style>
</head>
<body>

<header>
  <span class="logo">🔗 LANShare</span>
  <span class="tagline">Local network file sharing</span>
  <span class="spacer"></span>
  <span id="ipBadges">__IPS__</span>
</header>

<div class="container">

  <div class="viewer-notice" id="viewerNotice">
    👋 You're browsing shared files. Click <strong>Download</strong> on any file to save it to your device.
  </div>

  <!-- HOST panel -->
  <div class="panel" id="hostPanel" style="display:none">
    <div class="panel-header">
      <span class="panel-title">📁 Share a Folder</span>
      <span class="pill">HOST</span>
    </div>

    <div id="dropzone">
      <span class="dz-emoji">📂</span>
      <div class="dz-title">Click to choose a folder</div>
      <div class="dz-sub">
        Every file inside — including all sub-folders — will be shared.<br>
        Works with Desktop, Videos, Documents, Downloads and any directory.
      </div>
      <input type="file" id="folderInput" webkitdirectory multiple>
    </div>

    <!-- Upload progress (shown during upload) -->
    <div id="progressSection">
      <div class="progress-header">
        <span class="progress-label" id="progressLabel">Uploading…</span>
        <span class="progress-pct" id="progressPct">0%</span>
      </div>
      <div class="progress-track">
        <div class="progress-fill" id="progressFill"></div>
      </div>
      <div class="progress-stats">
        <span id="progressDone">0 of 0 files</span>
        <span id="progressSize">0 B / 0 B</span>
      </div>
      <div class="progress-file" id="progressFile">Preparing…</div>
    </div>

    <!-- Shared dir chips -->
    <div class="dir-list" id="dirList"></div>
  </div>

  <!-- File list (visible to everyone) -->
  <div class="panel">
    <div class="panel-header">
      <span class="panel-title">📄 Shared Files</span>
    </div>
    <div class="files-topbar">
      <span id="countLabel">Loading…</span>
      <input id="searchBox" type="text" placeholder="🔍  Search files…">
      <button class="refresh-btn" id="refreshBtn">↻ Refresh</button>
    </div>
    <div id="fileGrid" class="file-grid"></div>
  </div>

</div>
<div id="toast"></div>

<script>
const PORT   = __PORT__;
let allFiles = [];
const isHost = location.hostname === 'localhost' || location.hostname === '127.0.0.1';

// Host vs viewer
if (isHost) {
  document.getElementById('hostPanel').style.display = 'block';
} else {
  document.getElementById('viewerNotice').style.display = 'block';
}

// Click-to-copy IP badges
document.querySelectorAll('.ip-badge').forEach(el => {
  el.title = 'Click to copy';
  el.addEventListener('click', () => {
    navigator.clipboard.writeText(el.textContent.trim())
      .then(() => toast('📋 Copied!'));
  });
});

// ── Drop zone ────────────────────────────────────────────────────────────────
const dropzone    = document.getElementById('dropzone');
const folderInput = document.getElementById('folderInput');

dropzone.addEventListener('click', () => { if (!dropzone.classList.contains('uploading')) folderInput.click(); });
dropzone.addEventListener('dragover',  e => { e.preventDefault(); dropzone.classList.add('drag-over'); });
dropzone.addEventListener('dragleave', () => dropzone.classList.remove('drag-over'));
dropzone.addEventListener('drop', e => {
  e.preventDefault();
  dropzone.classList.remove('drag-over');
  toast('⚠️ Use the button — drag-drop cannot read folder paths in browsers');
});

// ── Folder input change → upload all files one-by-one ───────────────────────
folderInput.addEventListener('change', async () => {
  const files = Array.from(folderInput.files);
  if (!files.length) return;

  // Group by top-level folder name (webkitRelativePath = "FolderName/sub/file.ext")
  const byDir = {};
  for (const f of files) {
    const top = f.webkitRelativePath.split('/')[0];
    (byDir[top] = byDir[top] || []).push(f);
  }

  for (const [dirName, dirFiles] of Object.entries(byDir)) {
    await uploadDirOneByOne(dirName, dirFiles);
  }

  folderInput.value = '';
  loadFiles();
});

// Upload each file individually so we get accurate progress and no timeouts
async function uploadDirOneByOne(dirName, files) {
  const total      = files.length;
  const totalBytes = files.reduce((s, f) => s + f.size, 0);
  let   doneBytes  = 0;

  showProgress(true);
  updateProgress(0, total, 0, totalBytes, 'Starting…');

  for (let i = 0; i < total; i++) {
    const file    = files[i];
    const relPath = file.webkitRelativePath; // e.g. "Desktop/subdir/photo.jpg"

    updateProgress(i, total, doneBytes, totalBytes, file.webkitRelativePath);

    const fd = new FormData();
    fd.append('dir_name',  dirName);
    fd.append('rel_path',  relPath);
    fd.append('file',      file, relPath);

    try {
      const res = await fetch('/api/upload-file', { method: 'POST', body: fd });
      if (!res.ok) {
        toast('⚠️ Skipped: ' + file.name + ' (server error)');
      }
    } catch (e) {
      toast('⚠️ Skipped: ' + file.name + ' (' + e.message + ')');
    }

    doneBytes += file.size;
  }

  updateProgress(total, total, totalBytes, totalBytes, 'Done!');
  toast('✅ "' + dirName + '" shared — ' + total + ' files');

  setTimeout(() => showProgress(false), 2500);
  loadDirs();
}

// ── Progress UI ──────────────────────────────────────────────────────────────
function showProgress(show) {
  document.getElementById('progressSection').style.display = show ? 'block' : 'none';
  dropzone.classList.toggle('uploading', show);
  if (!show) {
    document.getElementById('progressFill').style.width = '0%';
    document.getElementById('progressPct').textContent  = '0%';
    document.getElementById('progressLabel').textContent = 'Uploading…';
  }
}

function updateProgress(done, total, doneBytes, totalBytes, currentFile) {
  const pct  = total > 0 ? Math.round((done / total) * 100) : 0;
  document.getElementById('progressFill').style.width   = pct + '%';
  document.getElementById('progressPct').textContent    = pct + '%';
  document.getElementById('progressLabel').textContent  =
    done >= total ? '✅ Upload complete!' : 'Uploading…';
  document.getElementById('progressDone').textContent   =
    done + ' of ' + total + ' file' + (total !== 1 ? 's' : '');
  document.getElementById('progressSize').textContent   =
    humanSize(doneBytes) + ' / ' + humanSize(totalBytes);
  document.getElementById('progressFile').innerHTML =
    done < total
      ? 'Current file: <span>' + esc(currentFile) + '</span>'
      : '<span>All files uploaded successfully!</span>';
}

function humanSize(n) {
  const units = ['B','KB','MB','GB','TB'];
  for (const u of units) { if (n < 1024) return n.toFixed(1) + ' ' + u; n /= 1024; }
  return n.toFixed(1) + ' PB';
}

// ── Directory chip list ──────────────────────────────────────────────────────
async function loadDirs() {
  const dirs = await fetch('/api/dirs').then(r => r.json());
  const list = document.getElementById('dirList');
  list.innerHTML = '';
  for (const d of dirs) {
    const chip = document.createElement('div');
    chip.className = 'dir-chip';
    chip.innerHTML =
      '<span class="icon">📁</span>' +
      '<div class="info">' +
        '<div class="name">' + esc(d.name) + '</div>' +
        '<div class="meta">' + d.file_count + ' files &bull; ' + d.total_size + '</div>' +
      '</div>' +
      '<button class="remove-btn">✕ Remove</button>';
    chip.querySelector('.remove-btn').addEventListener('click', () => removeDir(d.name));
    list.appendChild(chip);
  }
}

async function removeDir(name) {
  await fetch('/api/dirs/' + encodeURIComponent(name), { method: 'DELETE' });
  toast('🗑️ Stopped sharing "' + name + '"');
  loadFiles();
  loadDirs();
}

// ── File list ────────────────────────────────────────────────────────────────
document.getElementById('searchBox').addEventListener('input', filterFiles);
document.getElementById('refreshBtn').addEventListener('click', loadFiles);

async function loadFiles() {
  allFiles = await fetch('/api/files').then(r => r.json());
  renderFiles(allFiles);
  if (isHost) loadDirs();
}

function renderFiles(files) {
  const grid  = document.getElementById('fileGrid');
  const count = document.getElementById('countLabel');
  count.textContent = files.length + ' file' + (files.length !== 1 ? 's' : '') + ' shared';

  if (!files.length) {
    grid.innerHTML =
      '<div class="empty-state">' +
        '<span class="e-emoji">📭</span>' +
        '<p>' + (isHost
          ? 'No files yet.<br>Click the folder picker above to start sharing.'
          : 'No files shared yet.<br>The host hasn\'t shared anything — check back soon.') +
        '</p></div>';
    return;
  }

  grid.innerHTML = '';
  files.forEach(f => {
    const card = document.createElement('div');
    card.className = 'file-card';
    card.innerHTML =
      '<div class="file-top">' +
        '<span class="file-icon">' + fileIcon(f.name) + '</span>' +
        '<div class="file-info">' +
          '<div class="file-name" title="' + esc(f.rel_path) + '">' + esc(f.name) + '</div>' +
          '<div class="file-size">' + esc(f.size_hr) + '</div>' +
        '</div>' +
      '</div>' +
      '<span class="file-dir" title="' + esc(f.rel_path) + '">📁 ' + esc(f.dir) + '</span>' +
      '<a class="dl-btn" href="/api/download/' + encodeURIComponent(f.rel_path) +
        '" download="' + esc(f.name) + '">⬇ Download</a>';
    grid.appendChild(card);
  });
}

function filterFiles() {
  const q = document.getElementById('searchBox').value.toLowerCase().trim();
  renderFiles(q ? allFiles.filter(f =>
    f.name.toLowerCase().includes(q) || f.dir.toLowerCase().includes(q) || f.rel_path.toLowerCase().includes(q)
  ) : allFiles);
}

function fileIcon(name) {
  const ext = name.split('.').pop().toLowerCase();
  const map = {
    mp4:'🎬',mkv:'🎬',avi:'🎬',mov:'🎬',webm:'🎬',flv:'🎬',m4v:'🎬',
    mp3:'🎵',wav:'🎵',flac:'🎵',ogg:'🎵',aac:'🎵',m4a:'🎵',opus:'🎵',
    jpg:'🖼️',jpeg:'🖼️',png:'🖼️',gif:'🖼️',webp:'🖼️',svg:'🖼️',bmp:'🖼️',heic:'🖼️',avif:'🖼️',
    pdf:'📕',epub:'📕',
    doc:'📝',docx:'📝',odt:'📝',rtf:'📝',
    xls:'📊',xlsx:'📊',csv:'📊',ods:'📊',
    ppt:'📊',pptx:'📊',odp:'📊',
    zip:'📦',tar:'📦',gz:'📦',bz2:'📦',xz:'📦',rar:'📦','7z':'📦',zst:'📦',
    py:'🐍',js:'📜',ts:'📜',jsx:'📜',tsx:'📜',
    html:'🌐',htm:'🌐',css:'🎨',
    json:'📋',xml:'📋',yml:'📋',yaml:'📋',toml:'📋',ini:'📋',
    sh:'⚙️',bash:'⚙️',zsh:'⚙️',fish:'⚙️',
    txt:'📄',md:'📄',log:'📄',rst:'📄',
    exe:'💿',deb:'💿',dmg:'💿',apk:'💿',iso:'💿',img:'💿',
    db:'🗄️',sqlite:'🗄️',sql:'🗄️',
  };
  return map[ext] || '📄';
}

function esc(s) {
  return String(s)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function toast(msg, dur=3500) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(t._t);
  t._t = setTimeout(() => { t.style.display = 'none'; }, dur);
}

// Initial load + auto-refresh for viewers
loadFiles();
if (!isHost) setInterval(loadFiles, 5000);
</script>
</body>
</html>"""


def build_html(port: int, local_ips: list[str]) -> str:
    ips_html = "".join(
        f'<span class="ip-badge">http://{ip}:{port}</span>' for ip in local_ips
    )
    return HTML_TEMPLATE.replace("__PORT__", str(port)).replace("__IPS__", ips_html)


# ── HTTP Handler ─────────────────────────────────────────────────────────────────
class Handler(http.server.BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        path = args[0].split()[1] if args else ""
        if path in ("/api/files", "/api/dirs"):
            return
        print(f"  [{self.address_string()}]  {fmt % args}")

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type",   "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    # ── GET ──────────────────────────────────────────────────────────────────────
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path

        if path in ("/", "/index.html"):
            body = build_html(SERVER_PORT, get_local_ips()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type",   "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        elif path == "/api/files":
            self.send_json(get_all_files())

        elif path == "/api/dirs":
            self.send_json(get_dirs_summary())

        elif path.startswith("/api/download/"):
            rel = urllib.parse.unquote(path[len("/api/download/"):])
            self._serve_file(rel)

        else:
            self.send_response(404)
            self.end_headers()

    # ── DELETE ───────────────────────────────────────────────────────────────────
    def do_DELETE(self):
        path = urllib.parse.urlparse(self.path).path
        if path.startswith("/api/dirs/"):
            name = urllib.parse.unquote(path[len("/api/dirs/"):])
            if name in shared_upload_dirs:
                del shared_upload_dirs[name]
                self.send_json({"ok": True})
            else:
                self.send_json({"error": "not found"}, 404)
        else:
            self.send_response(404)
            self.end_headers()

    # ── POST ─────────────────────────────────────────────────────────────────────
    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/upload-file":
            self._handle_single_file_upload()
        else:
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    # ── Single-file upload handler ────────────────────────────────────────────────
    def _handle_single_file_upload(self):
        """
        Accepts one file at a time via multipart/form-data.
        Fields:
          dir_name  — top-level folder label (e.g. "Desktop")
          rel_path  — relative path within that folder (e.g. "Desktop/subfolder/file.jpg")
          file      — the file content
        """
        ct = self.headers.get("Content-Type", "")
        if "multipart/form-data" not in ct:
            self.send_json({"error": "expected multipart/form-data"}, 400)
            return

        boundary = None
        for seg in ct.split(";"):
            seg = seg.strip()
            if seg.lower().startswith("boundary="):
                boundary = seg[9:].strip('"')
                break
        if not boundary:
            self.send_json({"error": "no boundary found"}, 400)
            return

        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length)

        dir_name, rel_path, file_bytes, filename = self._parse_single_file(body, boundary.encode())

        if file_bytes is None:
            self.send_json({"error": "no file in payload"}, 400)
            return

        # Sanitize rel_path — never allow '..'
        safe_parts = [p for p in Path(rel_path or filename or "upload").parts if p not in ("..", ".")]
        safe_rel   = str(Path(*safe_parts)) if safe_parts else "upload"

        # Save to disk
        save_path = UPLOAD_DIR / safe_rel
        save_path.parent.mkdir(parents=True, exist_ok=True)
        save_path.write_bytes(file_bytes)

        size     = len(file_bytes)
        eff_dir  = dir_name or safe_rel.split(os.sep)[0]
        basename = Path(safe_rel).name

        entry = {
            "name":     basename,
            "rel_path": safe_rel,
            "abs_path": str(save_path),
            "size":     size,
            "size_hr":  human_size(size),
            "dir":      eff_dir,
        }

        existing  = shared_upload_dirs.get(eff_dir, [])
        path_map  = {f["rel_path"]: f for f in existing}
        path_map[safe_rel] = entry
        shared_upload_dirs[eff_dir] = list(path_map.values())

        self.send_json({"ok": True, "saved": safe_rel, "size": size})

    def _parse_single_file(self, body: bytes, boundary: bytes):
        """Return (dir_name, rel_path, file_bytes, filename)."""
        delim    = b"--" + boundary
        parts    = body.split(delim)
        dir_name = None
        rel_path = None
        file_bytes = None
        filename   = None

        for part in parts:
            if part in (b"", b"--\r\n", b"--"):
                continue
            if part.startswith(b"\r\n"):
                part = part[2:]
            if b"\r\n\r\n" not in part:
                continue

            hdr_raw, content = part.split(b"\r\n\r\n", 1)
            if content.endswith(b"\r\n"):
                content = content[:-2]

            hdr   = hdr_raw.decode("utf-8", errors="replace")
            cd    = next((l for l in hdr.splitlines() if l.lower().startswith("content-disposition:")), "")
            params = {}
            for seg in cd.split(";"):
                seg = seg.strip()
                if "=" in seg:
                    k, _, v = seg.partition("=")
                    params[k.strip().lower()] = v.strip().strip('"')

            field = params.get("name", "")

            if field == "dir_name":
                dir_name = content.decode("utf-8", errors="replace").strip()
            elif field == "rel_path":
                rel_path = content.decode("utf-8", errors="replace").strip()
            elif field == "file":
                file_bytes = content
                filename   = params.get("filename", "upload")

        return dir_name, rel_path, file_bytes, filename

    # ── File download ─────────────────────────────────────────────────────────────
    def _serve_file(self, rel_path: str):
        match = next((f for f in get_all_files() if f["rel_path"] == rel_path), None)
        if not match:
            self.send_response(404); self.end_headers(); return

        abs_path = Path(match["abs_path"])
        if not abs_path.exists():
            self.send_response(404); self.end_headers(); return

        mime, _ = mimetypes.guess_type(str(abs_path))
        mime     = mime or "application/octet-stream"
        size     = abs_path.stat().st_size

        self.send_response(200)
        self.send_header("Content-Type",        mime)
        self.send_header("Content-Length",      str(size))
        self.send_header("Content-Disposition", f'attachment; filename="{abs_path.name}"')
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        with open(abs_path, "rb") as fh:
            while chunk := fh.read(512 * 1024):
                self.wfile.write(chunk)


# ── Main ─────────────────────────────────────────────────────────────────────────
def main():
    global SERVER_PORT

    parser = argparse.ArgumentParser(description="LANShare — browser-based LAN file sharing")
    parser.add_argument("--port",       type=int, default=DEFAULT_PORT,
                        help=f"Port to listen on (default: {DEFAULT_PORT})")
    parser.add_argument("--no-browser", action="store_true",
                        help="Don't open browser automatically")
    args = parser.parse_args()

    UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
    SERVER_PORT = find_free_port(args.port)
    ips         = get_local_ips()

    server = http.server.ThreadingHTTPServer(("", SERVER_PORT), Handler)

    print()
    print("  ╔══════════════════════════════════════════╗")
    print("  ║          🔗  LANShare is running         ║")
    print("  ╠══════════════════════════════════════════╣")
    print(f"  ║  Local:   http://localhost:{SERVER_PORT}         ║")
    for ip in ips:
        print(f"  ║  Network: http://{ip}:{SERVER_PORT}   ║")
    print("  ╠══════════════════════════════════════════╣")
    print("  ║  Pick a folder → files listed for LAN   ║")
    print("  ║  Others: open Network URL to download   ║")
    print("  ║  Ctrl+C to stop                          ║")
    print("  ╚══════════════════════════════════════════╝")
    print()

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(f"http://localhost:{SERVER_PORT}")).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  👋  LANShare stopped.")
        server.shutdown()


if __name__ == "__main__":
    main()
