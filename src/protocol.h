// protocol.h — shared wire-format constants and message structures
#pragma once
#include <cstdint>

// Port used for both UDP discovery and TCP transfers
constexpr quint16 LANSHARE_PORT     = 45454;
constexpr quint16 LANSHARE_UDP_PORT = 45454;

// UDP discovery broadcast interval (ms)
constexpr int DISCOVERY_INTERVAL_MS = 2000;

// How long before a peer is considered gone (ms)
constexpr int PEER_TIMEOUT_MS = 8000;

// File-chunk size during streaming
constexpr qint64 CHUNK_SIZE = 512 * 1024; // 512 KiB

// ── TCP message types (1-byte tag at the start of every message) ──────────────
enum class MsgType : quint8 {
    // Sender → Receiver
    TransferRequest  = 0x01,   // JSON: device name, list of {name,size,relPath}
    FileHeader       = 0x02,   // JSON: current file name, size, relative path
    FileData         = 0x03,   // raw bytes (chunk)
    TransferComplete = 0x04,   // JSON: SHA-256 of transferred data

    // Receiver → Sender
    TransferAccepted = 0x10,
    TransferRejected = 0x11,
    FileAck          = 0x12,   // receiver confirms file fully received
};
