# ⚡ C-DataBase

A high-performance, in-memory Key-Value store written entirely in C from scratch.  
Inspired by Redis internals: event-driven networking, binary protocol, memory-mapped I/O, and fork-based persistence.

```
┌──────────┐    Binary Protocol    ┌──────────────────────────────────────┐
│  CLI     │◄─────────────────────►│  Server (kqueue event loop)         │
│  Client  │   [9B header + payload]│                                     │
└──────────┘                       │  ┌──────────┐    ┌───────────────┐  │
                                   │  │ Hashmap   │    │ WAL (binary)  │  │
                                   │  │ (in-memory)│   │ (append-only) │  │
                                   │  └──────────┘    └───────────────┘  │
                                   │         │              │            │
                                   │         ▼              ▼            │
                                   │  ┌─────────────────────────────┐   │
                                   │  │  mmap replay on startup     │   │
                                   │  │  fork() + CoW for snapshots │   │
                                   │  └─────────────────────────────┘   │
                                   └──────────────────────────────────────┘
```

---

## Architecture

### Event-Driven Networking (`kqueue`)
A single-threaded event loop handles all client connections, timers, and I/O without any threads or locks.  
Each client is tracked by a **state machine** (`HEADER → KEY → VALUE → PROCESS`) that handles partial reads on non-blocking sockets — the same pattern used by Redis, Nginx, and Node.js.

### Custom Binary Protocol
Every message is a packed 9-byte header followed by raw key/value payloads:

```
┌──────────┬───────────┬───────────┬─────────┬──────────┐
│ opcode   │ key_len   │ val_len   │  key    │  value   │
│ (1 byte) │ (4 bytes) │ (4 bytes) │ (N bytes)│ (M bytes)│
└──────────┴───────────┴───────────┴─────────┴──────────┘
```

Zero string parsing. The server casts raw bytes directly to a C struct — O(1) decode time.

### Crash-Safe Persistence
The storage engine implements a Redis-inspired durability strategy:

| Layer | Purpose |
|---|---|
| **Write-Ahead Log** | Every `SET`/`DEL` is appended as binary to `wal.log` before updating memory |
| **Binary Snapshots** | Full database dumps in the same binary format as the WAL |
| **Dual-File Recovery** | On startup, replays `dump.txt` → `wal.log.old` → `wal.log` in sequence |

### Non-Blocking BGSAVE (`fork` + Copy-on-Write)
Snapshots are created via `fork()`. The OS provides a frozen copy of the hashmap through Copy-on-Write semantics — the parent continues serving clients at full speed while the child writes to disk. The parent never blocks: it saves the child PID and checks completion asynchronously via `waitpid(WNOHANG)`.

### Memory-Mapped I/O (`mmap`)
WAL and snapshot files are loaded at startup using `mmap` instead of `fread`. The kernel maps the file directly into virtual memory, eliminating per-entry syscall overhead and leveraging the OS page cache for optimal performance.

---

## Tech Stack

| Component | Technology |
|---|---|
| Language | C (C11, no external dependencies) |
| Networking | `kqueue` event loop (macOS/BSD) |
| Protocol | Custom binary, packed structs |
| Data Structure | Chained hashmap with dynamic expansion |
| Persistence | Binary WAL + fork-based snapshots |
| File I/O | `mmap` for reads, `fwrite` for appends |
| Build | `make` + `gcc` |

---

## Build & Run

### Prerequisites
- macOS (uses `kqueue` for the event loop)
- `gcc` and `make`

### Compilation
```bash
git clone https://github.com/AndreaMaccio/C-DataBase.git
cd C-DataBase
make
```

### Usage

**1. Start the server:**
```bash
./bin/server
```
The server listens on `localhost:8080`. Persistence files are stored in `data/`.

**2. Connect via CLI:**
```bash
./bin/cli
```

**3. Available commands:**
```
SET <key> <value>   Store a key-value pair (written to WAL first)
GET <key>           Retrieve a value
DEL <key>           Remove a key
SAVE                Trigger an immediate snapshot
```

**4. Graceful shutdown:**  
Press `Ctrl+C` — the server completes a final checkpoint before exiting.

---

## Project Structure

```
├── include/
│   ├── hashmap.h        # Hash table interface
│   ├── protocol.h       # Binary protocol definitions (opcodes, packed header)
│   ├── server.h         # Server interface
│   ├── signalhandling.h # Signal handler setup
│   └── storage.h        # Persistence layer interface
├── src/
│   ├── main.c           # Entry point, recovery sequence, shutdown
│   ├── server.c         # kqueue event loop, client state machine
│   ├── storage.c        # WAL, checkpoint, bgsave, mmap replay
│   ├── hashmap.c        # Chained hashmap with dynamic resizing
│   ├── signalhandling.c # SIGINT/SIGTERM handlers
│   └── cli.c            # Interactive CLI client
├── data/                # Runtime persistence files (auto-created)
└── Makefile
```

---

## Key Design Decisions

- **Single-threaded by design**: No mutexes, no deadlocks, no race conditions. The event loop serializes all operations naturally.
- **Binary everywhere**: The same `[header][key][value]` format is used for the wire protocol, WAL, and snapshots. One parser to rule them all.
- **Deferred cleanup**: `bgsave` tracks child PIDs globally and checks completion asynchronously, ensuring the event loop never blocks on disk I/O.
- **Copy-on-Write over locking**: Instead of locking the hashmap during a snapshot, `fork()` gives the child a frozen copy for free via OS-level page table duplication.

---

## Limitations & Future Work

- **macOS only**: Uses `kqueue`. An `epoll` backend (or `poll` for portability) could be added via `#ifdef`.
- **No TTL/expiry**: Keys persist forever. A timer-based eviction system could be added to the event loop.
- **No authentication**: All clients have full access. A simple AUTH command could gate connections.
- **No replication**: Single-node only. Leader-follower replication would be a natural next step.

---

Built from scratch by **Andrea Macciocca**.
