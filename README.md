# C-DataBase

A lightweight, high-performance in-memory Key-Value database written entirely in C.
Designed as an educational and portfolio project to explore advanced Systems Engineering concepts such as concurrency, thread safety, and persistent storage.

---

# English

## Key Features

### Thread-Safe Architecture

Implements a Thread-per-Client model using `pthreads`, enabling safe and efficient handling of multiple simultaneous TCP connections.

### Reliable ACID-Inspired Persistence

The database adopts a Redis-inspired persistence strategy to ensure durability and crash recovery:

* **Write-Ahead Log (WAL)**
  Every mutating operation is immediately appended and synchronized (`fsync`) to a persistent log file, minimizing the risk of data loss during unexpected crashes.

* **Automatic Snapshotting (Autosave)**
  Periodically serializes the entire Hashmap to disk, preventing unbounded WAL growth and improving recovery times.

### Deadlock Prevention

Implements strict Lock Ordering (`wal_mutex -> map_mutex`) together with `_nolock` transactional boundaries to guarantee atomic state consistency across memory and persistent storage without deadlocks.

### Separation of Concerns

Modular and scalable project structure with clear responsibility boundaries:

* `server.c` → TCP networking and client handling
* `storage.c` → Persistence layer and WAL/Snapshot management
* `hashmap.c` → In-memory data structures

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

The executable will be generated inside the `bin/` directory.

### Start the Server

```bash
./bin/server
```

The server binds to `localhost:8080` by default.
Persistence files (`dump.txt` and `wal.log`) are automatically generated inside the `data/` directory.

---

## Usage (TCP Protocol)

Connect using `netcat`:

```bash
nc localhost 8080
```

Available commands:

| Command | Syntax              | Description                                                  |
| ------- | ------------------- | ------------------------------------------------------------ |
| `SET`   | `SET <key> <value>` | Stores a key-value pair and appends the operation to the WAL |
| `GET`   | `GET <key>`         | Retrieves the value associated with a key                    |
| `DEL`   | `DEL <key>`         | Removes a key from the database                              |
| `SAVE`  | `SAVE`              | Forces an immediate manual snapshot                          |

---

## Roadmap

### 1. BGSAVE — Non-Blocking Checkpointing

Implement `fork()`-based asynchronous snapshots leveraging the operating system’s Copy-On-Write mechanism to eliminate disk I/O lock contention.

### 2. Event-Driven Networking

Migrate from the Thread-per-Client model to an asynchronous architecture based on `epoll` / `kqueue` for significantly improved scalability and throughput.

### 3. Binary Protocol & Storage Engine

Replace text-based parsing (`strtok`) with an optimized binary protocol to reduce overhead and improve overall performance.

---

# Italiano

## Caratteristiche Principali

### Architettura Thread-Safe

Implementa un modello Thread-per-Client tramite `pthreads`, consentendo la gestione concorrente di multiple connessioni TCP simultanee in modo sicuro ed efficiente.

### Persistenza Affidabile Ispirata ai Sistemi ACID

Il sistema utilizza una strategia di sincronizzazione su disco ispirata a Redis per garantire integrità e durabilità dei dati:

* **Write-Ahead Log (WAL)**
  Ogni operazione di modifica viene immediatamente registrata e sincronizzata (`fsync`) su un file di log persistente, minimizzando il rischio di perdita dati in caso di crash.

* **Snapshotting Automatico (Autosave)**
  Il database esegue periodicamente il dump completo della Hashmap su disco, evitando la crescita indefinita del WAL e migliorando i tempi di recovery.

### Prevenzione dei Deadlock

Implementa una rigorosa politica di Lock Ordering (`wal_mutex -> map_mutex`) e confini transazionali `_nolock` per garantire aggiornamenti atomici coerenti tra memoria RAM e storage persistente senza condizioni di deadlock.

### Separazione delle Responsabilità

Struttura modulare e scalabile con una chiara separazione dei componenti:

* `server.c` → Networking e gestione client TCP
* `storage.c` → Persistenza e gestione WAL/Snapshot
* `hashmap.c` → Strutture dati in memoria

---

## Build & Run

### Prerequisiti

* Sistema operativo POSIX-compliant (Linux/macOS)
* `gcc`
* `make`

### Compilazione

```bash
git clone https://github.com/AndreaMaccio/C-DataBase.git
cd C-DataBase
make
```

L’eseguibile verrà generato nella directory `bin/`.

### Avvio del Server

```bash
./bin/server
```

Il server verrà avviato su `localhost:8080`.
I file di persistenza (`dump.txt` e `wal.log`) verranno creati automaticamente nella directory `data/`.

---

## Utilizzo (Protocollo TCP)

Connessione tramite `netcat`:

```bash
nc localhost 8080
```

Comandi disponibili:

| Comando | Sintassi            | Descrizione                                          |
| ------- | ------------------- | ---------------------------------------------------- |
| `SET`   | `SET <key> <value>` | Memorizza una coppia chiave-valore e aggiorna il WAL |
| `GET`   | `GET <key>`         | Recupera il valore associato alla chiave             |
| `DEL`   | `DEL <key>`         | Elimina una chiave dal database                      |
| `SAVE`  | `SAVE`              | Forza manualmente uno snapshot immediato             |

---

## Roadmap

### 1. BGSAVE — Checkpointing Non Bloccante

Implementazione di `fork()` per consentire snapshot asincroni tramite meccanismo Copy-On-Write del sistema operativo, eliminando la contesa sui lock durante le operazioni di I/O.

### 2. Networking Event-Driven

Migrazione da modello Thread-per-Client a un’architettura asincrona basata su `epoll` / `kqueue` per migliorare scalabilità e throughput.

### 3. Protocollo e Storage Binario

Sostituzione del parsing testuale (`strtok`) con un protocollo binario ottimizzato per ridurre overhead e migliorare le performance.

---

Sviluppato da Andrea Macciocca.
