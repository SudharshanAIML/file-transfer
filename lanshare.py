#!/usr/bin/env python3
"""
LANShare — browser-based LAN file sharing
Run: python3 lanshare.py
"""

import http.server
import threading
import webbrowser
import json
import socket
import mimetypes
import urllib.parse
import argparse
import io
import zipfile
import os
from pathlib import Path

# ── Configuration ───────────────────────────────────────────────────────────────
DEFAULT_PORT = 7070
UPLOAD_DIR   = Path.home() / ".lanshare_uploads"

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

# ── HTML ─────────────────────────────────────────────────────────────────────────
HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LANShare</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d0d1a;--surface:#161626;--surface2:#1e1e32;--surface3:#252540;
  --border:#2a2a48;--accent:#667eea;--accent2:#764ba2;
  --green:#38ef7d;--text:#e2e2f0;--muted:#6868a0;--radius:12px;
}
body{font-family:'Inter',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column}

/* Header */
header{
  background:linear-gradient(135deg,#161626,#12122a);
  border-bottom:1px solid var(--border);
  padding:14px 28px;display:flex;align-items:center;gap:14px;
  position:sticky;top:0;z-index:200;
}
.logo{font-size:19px;font-weight:700;background:linear-gradient(90deg,#667eea,#a78bfa);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.tagline{color:var(--muted);font-size:12px}
header .spacer{flex:1}
.ip-badge{
  display:inline-flex;align-items:center;gap:5px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:20px;padding:5px 13px;font-size:12px;font-family:monospace;
  color:var(--accent);margin-left:6px;cursor:pointer;transition:all .2s;
}
.ip-badge:hover{background:var(--border);border-color:var(--accent)}

/* Layout */
.container{max-width:1100px;margin:0 auto;padding:24px 20px;width:100%;display:flex;flex-direction:column;gap:18px}

/* Panel */
.panel{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:20px}
.panel-header{display:flex;align-items:center;gap:10px;margin-bottom:16px;flex-wrap:wrap}
.panel-title{font-size:15px;font-weight:600;flex:1}
.pill{background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:20px;
  padding:2px 10px;font-size:11px;font-weight:700;color:#fff}

/* Drop zone */
#dropzone{
  border:2px dashed var(--border);border-radius:var(--radius);padding:36px 24px;
  text-align:center;cursor:pointer;transition:all .2s;background:rgba(102,126,234,.02);
}
#dropzone:hover{border-color:var(--accent);background:rgba(102,126,234,.06)}
#dropzone.uploading{opacity:.6;pointer-events:none;cursor:default}
.dz-emoji{font-size:44px;margin-bottom:12px;display:block}
.dz-title{font-size:17px;font-weight:700;margin-bottom:5px}
.dz-sub{color:var(--muted);font-size:13px;line-height:1.5}
#folderInput{display:none}

/* Progress */
#progressSection{display:none;margin-top:16px;background:var(--surface2);border:1px solid var(--border);border-radius:10px;padding:16px}
.prog-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.prog-label{font-size:13px;font-weight:600}
.prog-pct{font-size:22px;font-weight:700;color:var(--accent);font-variant-numeric:tabular-nums}
.prog-track{height:10px;background:var(--surface3);border-radius:99px;overflow:hidden;margin-bottom:8px}
.prog-fill{
  height:100%;border-radius:99px;width:0%;
  background:linear-gradient(90deg,var(--accent),var(--accent2));
  transition:width .25s ease;position:relative;overflow:hidden;
}
.prog-fill::after{content:'';position:absolute;inset:0;
  background:linear-gradient(90deg,transparent 0%,rgba(255,255,255,.25) 50%,transparent 100%);
  animation:shim 1.2s infinite}
@keyframes shim{from{transform:translateX(-100%)}to{transform:translateX(200%)}}
.prog-meta{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:5px}
.prog-file{font-size:12px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.prog-file b{color:var(--accent);font-weight:500}

/* Dir chips */
.dir-list{display:flex;flex-direction:column;gap:7px;margin-top:14px}
.dir-chip{display:flex;align-items:center;gap:10px;background:var(--surface2);
  border:1px solid var(--border);border-radius:9px;padding:9px 13px;
  animation:fadeIn .2s ease}
.dir-chip .dname{font-weight:600;font-size:13px}
.dir-chip .dmeta{color:var(--muted);font-size:11px;margin-top:1px}
.dir-chip .dinfo{flex:1;min-width:0}
.rm-btn{background:rgba(249,83,198,.1);border:1px solid rgba(249,83,198,.25);
  color:#f953c6;border-radius:7px;padding:4px 10px;font-size:12px;cursor:pointer;
  transition:all .2s;white-space:nowrap;flex-shrink:0}
.rm-btn:hover{background:rgba(249,83,198,.2)}

/* Viewer notice */
.viewer-notice{background:rgba(56,239,125,.07);border:1px solid rgba(56,239,125,.2);
  border-radius:10px;padding:11px 16px;font-size:13px;color:var(--green);display:none}

/* Files top bar */
.files-topbar{display:flex;align-items:center;gap:8px;margin-bottom:16px;flex-wrap:wrap}
#countLabel{font-size:13px;color:var(--muted);margin-right:4px}
#searchBox{
  flex:1;min-width:160px;max-width:300px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:8px;padding:7px 13px;color:var(--text);font-size:13px;
  outline:none;transition:border-color .2s;
}
#searchBox:focus{border-color:var(--accent)}
#searchBox::placeholder{color:var(--muted)}
.icon-btn{
  display:inline-flex;align-items:center;gap:6px;
  background:var(--surface2);border:1px solid var(--border);
  color:var(--text);border-radius:8px;padding:7px 13px;font-size:13px;
  cursor:pointer;transition:all .2s;white-space:nowrap;
}
.icon-btn:hover{background:var(--border)}
.dl-all-btn{
  display:inline-flex;align-items:center;gap:7px;
  background:linear-gradient(90deg,var(--accent),var(--accent2));
  color:#fff;border:none;border-radius:8px;padding:7px 16px;
  font-size:13px;font-weight:600;cursor:pointer;text-decoration:none;
  transition:opacity .2s;white-space:nowrap;
}
.dl-all-btn:hover{opacity:.88}

/* ── Tree view ── */
.tree-root{display:flex;flex-direction:column;gap:6px}

/* Folder row */
.folder-row{
  display:flex;align-items:center;gap:0;
  cursor:pointer;user-select:none;
  border-radius:8px;padding:0;
  transition:background .15s;
}
.folder-row:hover > .folder-head{background:var(--surface2)}
.folder-head{
  display:flex;align-items:center;gap:8px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:8px;padding:9px 14px;flex:1;
  transition:background .15s,border-color .15s;
}
.folder-row.open > .folder-head{border-color:rgba(102,126,234,.4);background:var(--surface3)}
.folder-chevron{font-size:11px;color:var(--muted);transition:transform .2s;width:14px;text-align:center}
.folder-row.open .folder-chevron{transform:rotate(90deg)}
.folder-icon{font-size:18px;flex-shrink:0}
.folder-name{font-weight:600;font-size:14px;flex:1}
.folder-badge{
  background:rgba(102,126,234,.15);color:var(--accent);
  border-radius:20px;padding:2px 9px;font-size:11px;font-weight:600;white-space:nowrap;
}
.folder-size{color:var(--muted);font-size:12px;white-space:nowrap}

/* Folder children */
.folder-children{
  display:none;flex-direction:column;gap:5px;
  margin-top:5px;padding-left:28px;
}
.folder-row.open + .folder-children{display:flex}

/* File row */
.file-row{
  display:flex;align-items:center;gap:10px;
  background:var(--surface);border:1px solid var(--border);
  border-radius:8px;padding:10px 14px;
  transition:border-color .18s,transform .15s,box-shadow .18s;
  animation:fadeIn .2s ease;
}
.file-row:hover{border-color:var(--accent);transform:translateX(2px);box-shadow:0 3px 16px rgba(102,126,234,.1)}
.file-icon{font-size:20px;flex-shrink:0}
.file-info{flex:1;min-width:0}
.file-name{font-weight:500;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.file-size{color:var(--muted);font-size:11px;margin-top:1px}
.dl-btn{
  display:inline-flex;align-items:center;gap:5px;
  background:linear-gradient(90deg,var(--accent),var(--accent2));
  color:#fff;border:none;border-radius:7px;padding:6px 13px;
  font-size:12px;font-weight:600;cursor:pointer;text-decoration:none;
  transition:opacity .18s;white-space:nowrap;flex-shrink:0;
}
.dl-btn:hover{opacity:.85}

/* Empty */
.empty-state{text-align:center;padding:56px 20px;color:var(--muted)}
.empty-state .e-emoji{font-size:48px;display:block;margin-bottom:12px}
.empty-state p{font-size:14px;line-height:1.7}

/* Toast */
#toast{position:fixed;bottom:24px;right:24px;max-width:320px;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:11px;padding:11px 16px;font-size:13px;
  display:none;z-index:9999;box-shadow:0 12px 40px rgba(0,0,0,.5);
  animation:toastIn .2s ease}
@keyframes toastIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}
@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}
::-webkit-scrollbar{width:5px}
::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
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
    👋 Browsing shared files — click <strong>Download</strong> on any file, or <strong>Download All</strong> to get everything at once.
  </div>

  <!-- HOST: share folders -->
  <div class="panel" id="hostPanel" style="display:none">
    <div class="panel-header">
      <span class="panel-title">📁 Share a Folder</span>
      <span class="pill">HOST</span>
    </div>
    <div id="dropzone">
      <span class="dz-emoji">📂</span>
      <div class="dz-title">Click to choose a folder</div>
      <div class="dz-sub">All files and sub-folders are shared recursively.<br>Works with Desktop, Downloads, Videos, Documents, or any directory.</div>
      <input type="file" id="folderInput" webkitdirectory multiple>
    </div>

    <!-- Progress bar -->
    <div id="progressSection">
      <div class="prog-row">
        <span class="prog-label" id="progLabel">Uploading…</span>
        <span class="prog-pct" id="progPct">0%</span>
      </div>
      <div class="prog-track"><div class="prog-fill" id="progFill"></div></div>
      <div class="prog-meta">
        <span id="progDone">0 of 0 files</span>
        <span id="progBytes">0 B / 0 B</span>
      </div>
      <div class="prog-file" id="progFile">Preparing…</div>
    </div>

    <div class="dir-list" id="dirList"></div>
  </div>

  <!-- File tree (everyone) -->
  <div class="panel">
    <div class="panel-header">
      <span class="panel-title">📄 Shared Files</span>
    </div>
    <div class="files-topbar">
      <span id="countLabel">Loading…</span>
      <input id="searchBox" type="text" placeholder="🔍  Search…">
      <button class="icon-btn" id="refreshBtn">↻ Refresh</button>
      <a class="dl-all-btn" id="dlAllBtn" href="/api/download-all" download="LANShare.zip">⬇ Download All</a>
    </div>
    <div id="fileTree"></div>
  </div>

</div>
<div id="toast"></div>

<script>
const PORT   = __PORT__;
let allFiles = [];
const isHost = location.hostname === 'localhost' || location.hostname === '127.0.0.1';

if (isHost) {
  document.getElementById('hostPanel').style.display = 'block';
} else {
  document.getElementById('viewerNotice').style.display = 'block';
}

// IP badge copy
document.querySelectorAll('.ip-badge').forEach(el => {
  el.title = 'Click to copy';
  el.addEventListener('click', () => {
    navigator.clipboard.writeText(el.textContent.trim()).then(() => toast('📋 Copied!'));
  });
});

// ── Drop zone ────────────────────────────────────────────────────────────────
const dropzone    = document.getElementById('dropzone');
const folderInput = document.getElementById('folderInput');

dropzone.addEventListener('click', () => { if (!dropzone.classList.contains('uploading')) folderInput.click(); });
dropzone.addEventListener('dragover',  e => { e.preventDefault(); dropzone.classList.add('drag-over'); });
dropzone.addEventListener('dragleave', () => dropzone.classList.remove('drag-over'));
dropzone.addEventListener('drop', e => { e.preventDefault(); toast('⚠️ Use the button — browsers cannot read full folder paths from drag-drop'); });

folderInput.addEventListener('change', async () => {
  const files = Array.from(folderInput.files);
  if (!files.length) return;

  // Group by top-level directory
  const byDir = {};
  for (const f of files) {
    const top = f.webkitRelativePath.split('/')[0];
    (byDir[top] = byDir[top] || []).push(f);
  }
  for (const [dirName, dirFiles] of Object.entries(byDir)) {
    await uploadOneByOne(dirName, dirFiles);
  }
  folderInput.value = '';
  loadFiles();
});

// Upload each file individually — gives per-file progress, no timeouts
async function uploadOneByOne(dirName, files) {
  const total      = files.length;
  const totalBytes = files.reduce((s, f) => s + f.size, 0);
  let doneBytes    = 0;

  showProg(true);
  setProg(0, total, 0, totalBytes, 'Starting…');

  for (let i = 0; i < total; i++) {
    const file = files[i];
    setProg(i, total, doneBytes, totalBytes, file.webkitRelativePath);

    const fd = new FormData();
    fd.append('dir_name', dirName);
    fd.append('rel_path', file.webkitRelativePath);
    fd.append('file',     file, file.webkitRelativePath);

    try {
      const res = await fetch('/api/upload-file', { method: 'POST', body: fd });
      if (!res.ok) toast('⚠️ Skipped: ' + file.name);
    } catch (e) {
      toast('⚠️ Error: ' + file.name);
    }
    doneBytes += file.size;
  }

  setProg(total, total, totalBytes, totalBytes, 'Done!');
  toast('✅ "' + dirName + '" shared — ' + total + ' file' + (total !== 1 ? 's' : ''));
  setTimeout(() => showProg(false), 2800);
  loadDirs();
}

// ── Progress ────────────────────────────────────────────────────────────────
function showProg(show) {
  document.getElementById('progressSection').style.display = show ? 'block' : 'none';
  dropzone.classList.toggle('uploading', show);
}
function setProg(done, total, doneB, totalB, file) {
  const pct = total > 0 ? Math.round(done / total * 100) : 0;
  document.getElementById('progFill').style.width  = pct + '%';
  document.getElementById('progPct').textContent   = pct + '%';
  document.getElementById('progLabel').textContent = done >= total ? '✅ Complete!' : 'Uploading…';
  document.getElementById('progDone').textContent  = done + ' of ' + total + ' files';
  document.getElementById('progBytes').textContent = humanSize(doneB) + ' / ' + humanSize(totalB);
  document.getElementById('progFile').innerHTML    =
    done < total ? 'Current: <b>' + esc(file) + '</b>' : '<b>All files uploaded!</b>';
}
function humanSize(n) {
  const u = ['B','KB','MB','GB','TB'];
  for (const unit of u) { if (n < 1024) return n.toFixed(1) + ' ' + unit; n /= 1024; }
  return n.toFixed(1) + ' PB';
}

// ── Dirs ─────────────────────────────────────────────────────────────────────
async function loadDirs() {
  const dirs = await fetch('/api/dirs').then(r => r.json());
  const list = document.getElementById('dirList');
  list.innerHTML = '';
  for (const d of dirs) {
    const chip = document.createElement('div');
    chip.className = 'dir-chip';
    chip.innerHTML =
      '<span style="font-size:18px">📁</span>' +
      '<div class="dinfo">' +
        '<div class="dname">' + esc(d.name) + '</div>' +
        '<div class="dmeta">' + d.file_count + ' files · ' + d.total_size + '</div>' +
      '</div>' +
      '<button class="rm-btn">✕ Remove</button>';
    chip.querySelector('.rm-btn').addEventListener('click', () => removeDir(d.name));
    list.appendChild(chip);
  }
}
async function removeDir(name) {
  await fetch('/api/dirs/' + encodeURIComponent(name), { method: 'DELETE' });
  toast('🗑️ Removed "' + name + '"');
  loadFiles();
  loadDirs();
}

// ── File list + tree ─────────────────────────────────────────────────────────
document.getElementById('searchBox').addEventListener('input', () => {
  const q = document.getElementById('searchBox').value.toLowerCase().trim();
  renderTree(q ? allFiles.filter(f =>
    f.name.toLowerCase().includes(q) ||
    f.rel_path.toLowerCase().includes(q)
  ) : allFiles);
});
document.getElementById('refreshBtn').addEventListener('click', loadFiles);

async function loadFiles() {
  allFiles = await fetch('/api/files').then(r => r.json());
  renderTree(allFiles);
  if (isHost) loadDirs();
  // update dl-all btn visibility
  document.getElementById('dlAllBtn').style.display = allFiles.length ? 'inline-flex' : 'none';
}

// ── Build a nested tree from flat file list ──────────────────────────────────
function buildTree(files) {
  // node = { __files: [], subDir: node, ... }
  const root = { __files: [] };
  for (const f of files) {
    const parts = f.rel_path.replace(/\\/g, '/').split('/');
    let node = root;
    // Walk all parts except the last (filename)
    for (let i = 0; i < parts.length - 1; i++) {
      const part = parts[i];
      if (!node[part]) node[part] = { __files: [] };
      node = node[part];
    }
    node.__files.push(f);
  }
  return root;
}

// ── Render the tree ──────────────────────────────────────────────────────────
function renderTree(files) {
  const container = document.getElementById('fileTree');
  const countEl   = document.getElementById('countLabel');
  countEl.textContent = files.length + ' file' + (files.length !== 1 ? 's' : '') + ' shared';

  if (!files.length) {
    container.innerHTML =
      '<div class="empty-state"><span class="e-emoji">📭</span><p>' +
      (isHost ? 'No files yet.<br>Choose a folder above to start sharing.'
               : 'No files shared yet.<br>Come back soon.') +
      '</p></div>';
    return;
  }

  const tree = buildTree(files);
  container.innerHTML = '';
  const root = document.createElement('div');
  root.className = 'tree-root';
  renderNode(root, tree, 0, true);
  container.appendChild(root);
}

function renderNode(container, node, depth, isRoot) {
  // 1. Files at this level
  const sortedFiles = [...node.__files].sort((a, b) => a.name.localeCompare(b.name));
  for (const f of sortedFiles) {
    container.appendChild(makeFileRow(f));
  }

  // 2. Sub-directories
  const subDirs = Object.keys(node).filter(k => k !== '__files').sort();
  for (const dirName of subDirs) {
    const child = node[dirName];
    const totalCount = countFiles(child);

    // Folder toggle row
    const folderRow = document.createElement('div');
    folderRow.className = 'folder-row';
    folderRow.innerHTML =
      '<div class="folder-head">' +
        '<span class="folder-chevron">▶</span>' +
        '<span class="folder-icon">📁</span>' +
        '<span class="folder-name">' + esc(dirName) + '</span>' +
        '<span class="folder-badge">' + totalCount + ' file' + (totalCount !== 1 ? 's' : '') + '</span>' +
      '</div>';

    // Children container (hidden by default; top-level starts open)
    const childrenEl = document.createElement('div');
    childrenEl.className = 'folder-children';

    // Toggle on click
    folderRow.addEventListener('click', () => {
      const isOpen = folderRow.classList.toggle('open');
      childrenEl.style.display = isOpen ? 'flex' : 'none';
    });

    // Open top-level folders by default
    if (isRoot) {
      folderRow.classList.add('open');
      childrenEl.style.display = 'flex';
    }

    renderNode(childrenEl, child, depth + 1, false);
    container.appendChild(folderRow);
    container.appendChild(childrenEl);
  }
}

function countFiles(node) {
  let n = node.__files.length;
  for (const k of Object.keys(node)) {
    if (k !== '__files') n += countFiles(node[k]);
  }
  return n;
}

function makeFileRow(f) {
  const row = document.createElement('div');
  row.className = 'file-row';
  row.innerHTML =
    '<span class="file-icon">' + fileIcon(f.name) + '</span>' +
    '<div class="file-info">' +
      '<div class="file-name" title="' + esc(f.rel_path) + '">' + esc(f.name) + '</div>' +
      '<div class="file-size">' + esc(f.size_hr) + '</div>' +
    '</div>' +
    '<a class="dl-btn" href="/api/download/' + encodeURIComponent(f.rel_path) +
      '" download="' + esc(f.name) + '">⬇ Download</a>';
  return row;
}

// ── Utils ─────────────────────────────────────────────────────────────────────
function fileIcon(name) {
  const ext = name.split('.').pop().toLowerCase();
  const m = {
    mp4:'🎬',mkv:'🎬',avi:'🎬',mov:'🎬',webm:'🎬',flv:'🎬',m4v:'🎬',
    mp3:'🎵',wav:'🎵',flac:'🎵',ogg:'🎵',aac:'🎵',m4a:'🎵',opus:'🎵',
    jpg:'🖼️',jpeg:'🖼️',png:'🖼️',gif:'🖼️',webp:'🖼️',svg:'🖼️',bmp:'🖼️',heic:'🖼️',
    pdf:'📕',epub:'📕',
    doc:'📝',docx:'📝',odt:'📝',rtf:'📝',
    xls:'📊',xlsx:'📊',csv:'📊',ods:'📊',
    ppt:'📊',pptx:'📊',odp:'📊',
    zip:'📦',tar:'📦',gz:'📦',rar:'📦','7z':'📦',xz:'📦',
    py:'🐍',js:'📜',ts:'📜',jsx:'📜',tsx:'📜',
    html:'🌐',htm:'🌐',css:'🎨',
    json:'📋',xml:'📋',yml:'📋',yaml:'📋',toml:'📋',
    sh:'⚙️',bash:'⚙️',c:'⚙️',cpp:'⚙️',h:'⚙️',rs:'⚙️',go:'⚙️',java:'⚙️',
    txt:'📄',md:'📄',log:'📄',
    exe:'💿',deb:'💿',dmg:'💿',apk:'💿',iso:'💿',
    db:'🗄️',sqlite:'🗄️',sql:'🗄️',
  };
  return m[ext] || '📄';
}
function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
function toast(msg, dur=3500) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(t._t);
  t._t = setTimeout(() => { t.style.display = 'none'; }, dur);
}

// Load on start + auto-refresh for viewers
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

    # ── GET ───────────────────────────────────────────────────────────────────
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

        elif path == "/api/download-all":
            self._serve_zip()

        elif path.startswith("/api/download/"):
            rel = urllib.parse.unquote(path[len("/api/download/"):])
            self._serve_file(rel)

        else:
            self.send_response(404)
            self.end_headers()

    # ── DELETE ────────────────────────────────────────────────────────────────
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

    # ── POST ──────────────────────────────────────────────────────────────────
    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/upload-file":
            self._handle_single_upload()
        else:
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    # ── Single-file upload ────────────────────────────────────────────────────
    def _handle_single_upload(self):
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
            self.send_json({"error": "no boundary"}, 400)
            return

        length     = int(self.headers.get("Content-Length", 0))
        body       = self.rfile.read(length)
        dir_name, rel_path, file_bytes, filename = self._parse_multipart(body, boundary.encode())

        if file_bytes is None:
            self.send_json({"error": "no file in payload"}, 400)
            return

        # Sanitize — no '..'
        raw        = rel_path or filename or "upload"
        safe_parts = [p for p in Path(raw).parts if p not in ("..", ".")]
        safe_rel   = str(Path(*safe_parts)) if safe_parts else "upload"

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

        existing              = shared_upload_dirs.get(eff_dir, [])
        path_map              = {f["rel_path"]: f for f in existing}
        path_map[safe_rel]    = entry
        shared_upload_dirs[eff_dir] = list(path_map.values())

        self.send_json({"ok": True, "saved": safe_rel})

    def _parse_multipart(self, body: bytes, boundary: bytes):
        delim      = b"--" + boundary
        parts      = body.split(delim)
        dir_name   = rel_path = file_bytes = filename = None

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

            hdr    = hdr_raw.decode("utf-8", errors="replace")
            cd     = next((l for l in hdr.splitlines() if l.lower().startswith("content-disposition:")), "")
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

    # ── Download All as ZIP ───────────────────────────────────────────────────
    def _serve_zip(self):
        files = get_all_files()
        if not files:
            self.send_response(204)
            self.end_headers()
            return

        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for f in files:
                abs_path = Path(f["abs_path"])
                if abs_path.exists():
                    # Preserve full relative path inside ZIP
                    zf.write(abs_path, f["rel_path"])

        zip_bytes = buf.getvalue()

        self.send_response(200)
        self.send_header("Content-Type",        "application/zip")
        self.send_header("Content-Length",       str(len(zip_bytes)))
        self.send_header("Content-Disposition", 'attachment; filename="LANShare.zip"')
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(zip_bytes)

    # ── Single file download ──────────────────────────────────────────────────
    def _serve_file(self, rel_path: str):
        match = next((f for f in get_all_files() if f["rel_path"] == rel_path), None)
        if not match:
            self.send_response(404); self.end_headers(); return

        abs_path = Path(match["abs_path"])
        if not abs_path.exists():
            self.send_response(404); self.end_headers(); return

        mime, _ = mimetypes.guess_type(str(abs_path))
        mime     = mime or "application/octet-stream"

        self.send_response(200)
        self.send_header("Content-Type",        mime)
        self.send_header("Content-Length",       str(abs_path.stat().st_size))
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
    parser.add_argument("--port",       type=int, default=DEFAULT_PORT)
    parser.add_argument("--no-browser", action="store_true")
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
    print("  ║  Choose a folder → tree view for LAN    ║")
    print("  ║  Download All → gets a ZIP of everything ║")
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
