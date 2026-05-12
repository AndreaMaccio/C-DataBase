# C-DataBase

A high-performance, in-memory Key-Value database written entirely in C from scratch.
Designed to explore advanced Systems Engineering concepts such as custom binary protocols, multi-threading, Copy-on-Write persistence, and crash recovery.

---

# English

## Key Features

### Custom Binary Protocol
Replaces traditional text-based protocols (like Redis' RESP) with a highly optimized, raw binary protocol over TCP. 
By utilizing packed structs and strict byte-length headers, it completely eliminates string parsing (`strtok`) overhead, drastically improving memory safety and throughput. Includes a custom interactive CLI client to communicate with the engine.

### Non-Blocking BGSAVE (Copy-on-Write)
Snapshots are generated asynchronously using `fork()`. The OS-level Copy-on-Write mechanism guarantees that the main database can continue serving thousands of clients at full speed while a background process writes a consistent snapshot to disk, eliminating I/O lock contention.

### Reliable ACID-Inspired Persistence
The database adopts a Redis-inspired persistence strategy:
* **Binary Write-Ahead Log (WAL)**: Mutating operations (`SET`, `DEL`) are appended as raw bytes to a persistent log file.
* **Dual-File Crash Recovery**: Implements a robust `wal.log` rotation. During boot, the system sequentially replays `wal.log.old` and `wal.log` ensuring zero data loss even if a crash occurs during a snapshot rotation.

### Thread-Safe Architecture & Deadlock Prevention
Implements a Thread-per-Client model using `pthreads`. Employs strict Lock Ordering (`wal_mutex -> map_mutex`) and `_nolock` transactional boundaries to guarantee atomic state consistency across memory and persistent storage without deadlocks.

---

## Build & Run

### Prerequisites
* POSIX-compliant operating system (Linux/macOS)
* `gcc`
* `make`

### Compilation
```bash
git clone https://github.com/AndreaMaccio/C-DataBase.git
cd C-DataBase
make
```
This will compile both the Database Server and the CLI Client in the `bin/` directory.

### Usage

**1. Start the Server**
```bash
./bin/server
```
The server binds to `localhost:8080`. Persistence files (`dump.txt` and `wal.log`) are stored in `data/`.

**2. Connect via Custom CLI**
```bash
./bin/cli
```
Available commands in the interactive shell:
* `SET <key> <value>`: Stores a key-value pair and appends to the Binary WAL.
* `GET <key>`: Retrieves the value associated with a key.
* `DEL <key>`: Removes a key from the database.
* `SAVE`: Forces an immediate background snapshot (`BGSAVE`).

---

## Roadmap

### 1. Event-Driven Networking (Event Loop)
Migrate from the Thread-per-Client model to a single-threaded asynchronous architecture based on `epoll` (Linux) / `kqueue` (macOS) to eliminate context-switching overhead and support tens of thousands of concurrent connections (C10k problem).

---

# Italiano

## Caratteristiche Principali

### Protocollo Binario Custom
Sostituisce i tradizionali protocolli testuali con un protocollo binario raw su TCP altamente ottimizzato. 
Utilizzando struct packed e lunghezze prefissate nell'header, elimina completamente l'overhead del parsing testuale, migliorando drasticamente la sicurezza della memoria e il throughput. Include un Client CLI custom interattivo per la comunicazione di rete.

### BGSAVE Non Bloccante (Copy-on-Write)
I dump della memoria vengono generati in modo asincrono tramite `fork()`. Il meccanismo di Copy-on-Write a livello di Sistema Operativo permette al database principale di continuare a servire i client a piena velocità mentre un processo in background scrive uno snapshot coerente su disco, azzerando le latenze di I/O.

### Persistenza Affidabile
Il sistema utilizza una strategia di persistenza ispirata a Redis:
* **Write-Ahead Log Binario (WAL)**: Ogni operazione di scrittura viene registrata in formato binario su file.
* **Crash Recovery a Doppio File**: Implementa una solida logica di rotazione del WAL. All'avvio, il sistema ricostruisce lo stato rileggendo sequenzialmente `wal.log.old` e `wal.log`, garantendo zero perdita di dati anche in caso di interruzione di corrente durante un backup.

### Architettura Thread-Safe
Implementa un modello Thread-per-Client tramite `pthreads` con una rigorosa politica di Lock Ordering per prevenire i deadlock durante gli aggiornamenti atomici tra RAM e Disco.

---

## Compilazione e Avvio

```bash
make clean && make
```

Avvia il server:
```bash
./bin/server
```

Connettiti con il client:
```bash
./bin/cli
```

*(Non è più possibile usare `netcat` a causa del protocollo binario).*

---

Sviluppato da Andrea Macciocca.
