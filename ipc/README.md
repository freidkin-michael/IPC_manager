# IPC & Monitoring Systems

**Two complementary inter-process communication libraries**

---

## 📂 Project Structure

```
config/
├── ipc/                    # Command-Response IPC System
│   ├── ipc_mailbox.h      # Main IPC library (single-header)
│   ├── configurator_*.c   # Server processes
│   ├── *_monitor.c        # Client processes
│   └── status.c           # Status monitoring tool
│
├── mon/                    # Publish-Subscribe Status Monitoring
│   └── mon.h              # Status monitoring library (single-header)
│
├── test.sh                # IPC test suite (21 tests)
├── Makefile               # Build system
└── README.md              # This file
```

---

## Table of Contents
1. [IPC Mailbox System](#ipc-mailbox-system) - Command-Response pattern
2. [Status Monitor System](#status-monitor-system) - Publish-Subscribe pattern
3. [When to Use Which?](#when-to-use-which)
4. [Building and Running](#building-and-running)

---

## IPC Mailbox System

**Simple, robust inter-process communication using POSIX shared memory**

Located in: `ipc/`

### What is This?

This is a **command-response IPC system** that lets separate programs send commands and wait for results. Think of it like a **mailbox system**:

- **Monitors** send requests (like "configure DAC please!")
- **Configurators** receive requests, do work, and reply with results
- Everything happens through **shared memory** (fast!)

### Key Concept: Dedicated Queue Pairs

Each queue pair is like a **private conversation channel**:
- Queue 1: DAC configuration
- Queue 2: PHY configuration  
- Queue 3: PRACH configuration

**Important**: One configurator per queue, but multiple monitors can send to the same queue.

---

## Architecture Overview

```
+---------------------------------------------------------+
|              POSIX Shared Memory (/dev/shm)             |
|                                                         |
|  +--------------------------------------------------+   |
|  |           SharedQueue Structure                  |   |
|  +--------------------------------------------------+   |
|  |  Global Mutex + 16 Queue Pairs + 32 Process Slots|   |
|  +--------------------------------------------------+   |
|                                                         |
|  Queue Pair #1 (DAC):                                   |
|  +--------------------------------------------+         |
|  | Circular Buffer [256 commands]             |         |
|  | Condition Variables (new_cmd + done)       |         |
|  +--------------------------------------------+         |
|         ^                            |                  |
|         | enqueue                    | dequeue          |
|         |                            v                  |
|  +--------------+            +--------------------+     |
|  | dac_monitor  |            | configurator_dac   |     |
|  | (client)     |            | (server)           |     |
|  +--------------+            +--------------------+     |
|                                                         |
|  Queue Pair #2 (PHY):                                   |
|  +--------------------------------------------+         |
|  | Circular Buffer [256 commands]             |         |
|  +--------------------------------------------+         |
|         ^                            |                  |
|  +--------------+            +--------------------+     |
|  | phy_monitor  |            | configurator_phy   |     |
|  +--------------+            +--------------------+     |
|                                                         |
+---------------------------------------------------------+
```

---

## Single-Include Design

### What Does "Single-Include" Mean?

**ipc_mailbox.h** contains **EVERYTHING**:
- Data structures (structs, enums)
- Function implementations (marked `static inline`)
- Documentation (Doxygen comments)

**No separate .c file needed for the library!**

### How to Use It

Just include the header in your program:

```c
#include "ipc_mailbox.h"

int main() {
    int is_creator;
    SharedQueue* queue = open_or_create_shared_queue(&is_creator);
    // ... use the queue ...
}
```

That's it! The compiler automatically includes all the code.

### Why `static inline`?

- **`static`**: Each .c file gets its own copy (no linking conflicts)
- **`inline`**: Compiler can optimize by inserting code directly (faster)
- **Trade-off**: Slightly larger executables, but simpler build process

### Directory Structure

```
config/
+-- ipc_mailbox.h               # <- Single-include library (ALL IPC code)
+-- configurator_phy.c          # PHY server application
+-- configurator_dac_prach.c    # DAC+PRACH multi-threaded server
+-- dac_monitor.c               # DAC client application
+-- phy_prach_monitor.c         # PHY+PRACH multi-threaded client
+-- status.c                    # Status viewer utility
+-- Makefile                    # Build system
+-- README.md                   # This file
```

**Note**: Each `.c` file is a complete application that includes `ipc_mailbox.h`.

---

## Process Registration API

The library provides simplified registration functions that hide implementation details:

### For Monitors (Clients)

```c
SharedQueue* register_monitor(const char* name, int queue_id);
```

**Usage:**
```c
// Single-queue monitor
char name[64];
snprintf(name, sizeof(name), "dac_monitor-%d", getpid());
SharedQueue* queue = register_monitor(name, CMD_DAC_CONFIG);
if (!queue) {
    // Handle error
    return 1;
}

// Multi-queue monitor (monitors multiple configurators)
char name[64];
snprintf(name, sizeof(name), "multi_monitor-%d", getpid());
SharedQueue* queue = register_monitor(name, 0);  // Use queue_id=0
```

### For Configurators (Servers)

```c
SharedQueue* register_configurator(const char* name, int queue_id);
```

**Usage:**
```c
// Single-queue configurator
char name[64];
snprintf(name, sizeof(name), "phy-configurator-%d", getpid());
SharedQueue* queue = register_configurator(name, CMD_PHY_CONFIG);
if (!queue) {
    // Handle error
    return 1;
}

// Multi-queue configurator (handles multiple queues with threads)
char name_dac[64];
snprintf(name_dac, sizeof(name_dac), "dac-configurator-%d", getpid());
SharedQueue* queue = register_configurator(name_dac, CMD_DAC_CONFIG);

char name_prach[64];
snprintf(name_prach, sizeof(name_prach), "prach-configurator-%d", getpid());
// Register second queue using internal API
_register_process_internal(queue, PROCESS_CONFIGURATOR, name_prach, CMD_PRACH_CONFIG);
```

### Return Values

- **Success**: Returns pointer to SharedQueue (use with SHARED_QUEUE_AUTO for automatic cleanup)
- **Failure**: Returns NULL (registration failed or shm init failed)

### Creator Check

To determine if cleanup is needed on exit:

```c
bool is_shared_queue_creator(void);
```

**Usage:**
```c
if (is_shared_queue_creator())
{
    cleanup_shared_queue(queue);
    shm_unlink(SHM_NAME);
}
```

### Automatic Cleanup

**Process unregistration is now fully automatic!** The library uses `__attribute__((destructor))` to automatically call `unregister_process()` when your program exits (normally, via signal, or `exit()`).

**You don't need to do anything** - just register and the library handles cleanup:
- [+] Automatic unregistration on normal exit
- [+] Works with SIGTERM/SIGINT signals
- [+] Creator processes automatically cleanup shared memory
- [+] No manual `unregister_process()` calls needed

**Note:** The internal function `_register_process_internal()` can be used for registering additional queues in multi-queue configurators (after the first registration with `register_configurator()`).

---

## How It Works

### Step-by-Step Flow

#### 1. Startup: Creating Shared Memory

```c
// First process to run (race condition handled atomically)
int is_creator;
SharedQueue* queue = open_or_create_shared_queue(&is_creator);

if (is_creator) {
    // I won the race! Init mutex, condition variables, buffers
    // (This happens inside open_or_create_shared_queue)
}
```

**Behind the scenes**:
```c
fd = shm_open("/cfg_shm", O_CREAT | O_EXCL | O_RDWR, 0666);
//                        ^^^^^^^^
// O_EXCL = atomic check: fails if file exists
// First process: fd >= 0 (success, must initialize)
// Other processes: fd < 0, errno=EEXIST (just map memory)
```

#### 2. Registration: "I'm alive!"

```c
// Configurator registers itself
register_configurator(queue, "configurator-dac-12345", CMD_DAC_CONFIG);

// This writes to shared process registry:
// processes[slot] = {
//     .pid = 12345,
//     .type = CONFIGURATOR,
//     .name = "configurator-dac-12345",
//     .queue_id = 1,
//     .active = 1
// }
```

**Why register?**
- Monitors can check: "Is there a configurator for queue 1?"
- Status tool shows all running processes
- Uses `kill(pid, 0)` to detect crashes

#### 3. Communication: Request-Response Pattern

**Monitor (Client) Side:**
```c
// Step 1: Check if configurator is alive
if (!is_configurator_registered(queue, CMD_DAC_CONFIG)) {
    log_msg("No DAC configurator - waiting...");
    sleep(3);
    continue;
}

// Step 2: Send command (non-blocking enqueue)
uint32_t cmd_id = set_event(queue, CMD_DAC_CONFIG, CMD_DAC_CONFIG);
if (cmd_id == 0) {
    log_msg("Queue full!");
    continue;
}

// Step 3: Wait for result (blocking with timeout)
int result = wait_for_command(queue, CMD_DAC_CONFIG, cmd_id, 5);
if (result < 0) {
    log_msg("Timeout!");
} else {
    log_msg("Success! Result=%d", result);
}
```

**Configurator (Server) Side:**
```c
// Step 1: Register on startup
register_configurator(queue, "configurator-dac", CMD_DAC_CONFIG);

// Step 2: Main loop - wait for commands
while (!stop) {
    CommandMsg* cmd = get_event(queue, CMD_DAC_CONFIG);
    if (!cmd) continue; // Timeout or shutdown
    
    // Step 3: Do the actual work
    log_msg("Processing command %u", cmd->cmd_id);
    bool success = dac_config(); // Your business logic here
    
    // Step 4: Send result back to monitor
    mark_event_done(queue, CMD_DAC_CONFIG, cmd, success);
    increment_command_counter(queue);
}
```

#### 4. Synchronization: Mutex + Condition Variables

**Problem**: Multiple processes accessing same memory = chaos!

**Solution**: One global mutex + per-queue condition variables

```c
// Inside set_event():
pthread_mutex_lock(&queue->mutex);        // <- Lock (all processes wait here)

queue->pairs[queue_id].buffer[tail] = msg;  // Write to circular buffer
queue->pairs[queue_id].tail = (tail + 1) % QUEUE_SIZE; // Move tail
queue->pairs[queue_id].count++;

pthread_cond_signal(&queue->pairs[queue_id].cond_new_cmd); // Wake configurator

pthread_mutex_unlock(&queue->mutex);      // <- Unlock (others can proceed)
```

**Condition Variables**:
- `cond_new_cmd`: Configurator waits here until monitor sends command
- `cond_done`: Monitor waits here until configurator finishes work

#### 5. Circular Buffer: Never-Full Queue

```
Buffer size: 256 commands

Empty state:
[_][_][_][_][_][_][_][_]
 ^
 head=0, tail=0, count=0

After 3 enqueues:
[A][B][C][_][_][_][_][_]
 ^     ^
 head  tail
 count=3

After 2 dequeues:
[_][_][C][_][_][_][_][_]
       ^  ^
       head tail
       count=1

Wrap-around after 8 enqueues:
[H][_][_][_][_][_][D][E][F][G]
    ^                 ^
    tail              head
    count=5
```

**Key equations**:
- Enqueue: `buffer[tail] = msg; tail = (tail+1) % SIZE`
- Dequeue: `msg = buffer[head]; head = (head+1) % SIZE`
- Full: `count == SIZE`
- Empty: `count == 0`

---

## Code Examples

### Example 1: Simple Monitor (Client)

```c
#include "ipc_mailbox.h"
#include <signal.h>

volatile sig_atomic_t stop = 0;
void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
}

int main()
{
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    char name[64];
    snprintf(name, sizeof(name), "dac_monitor-%d", getpid());
    
    SHARED_QUEUE_AUTO SharedQueue* queue = register_monitor(name, CMD_DAC_CONFIG);
    if (!queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register monitor");
        return 1;
    }
    
    log_msg(LOG_LEVEL_LOG, "DAC Monitor started (queue_id=%d)", CMD_DAC_CONFIG);
    
    while (!stop)
    {
        // Send command (set_event checks configurator availability internally)
        uint32_t cmd_id = set_event(queue, CMD_DAC_CONFIG, CMD_DAC_CONFIG);
        if (cmd_id == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Configurator not available or queue full, waiting...");
            sleep(2);
            continue;
        }
        
        log_msg(LOG_LEVEL_LOG, "Sent command %u", cmd_id);
        
        // Wait for response (5 second timeout)
        int result = wait_for_command(queue, CMD_DAC_CONFIG, cmd_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "Timeout waiting for command %u", cmd_id);
        }
        else if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Command %u failed", cmd_id);
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "Command %u completed successfully", cmd_id);
        }
        
        sleep(3); // Send every 3 seconds
    }
    
    return 0;  // Automatic cleanup via destructor
}
```

### Example 2: Simple Configurator (Server)

```c
#include "ipc_mailbox.h"
#include <signal.h>
#include <unistd.h>

volatile sig_atomic_t stop = 0;
void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
}

// Your business logic
bool dac_config(void)
{
    usleep(100000); // Simulate 100ms work
    return true; // Success
}

int main()
{
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    // Register as configurator for DAC queue
    char name[64];
    snprintf(name, sizeof(name), "dac-configurator-%d", getpid());
    
    SHARED_QUEUE_AUTO SharedQueue* queue = register_configurator(name, CMD_DAC_CONFIG);
    if (!queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register configurator");
        return 1;
    }
    
    log_msg(LOG_LEVEL_LOG, "DAC Configurator started (queue_id=%d)", CMD_DAC_CONFIG);
    
    while (!stop)
    {
        // Block until command arrives (timeout handled internally)
        // get_event() uses pthread_cond_timedwait() - no CPU busy-wait!
        CommandMsg* cmd = get_event(queue, CMD_DAC_CONFIG);
        if (cmd->type == CMD_EMPTY)
        {
            // Timeout after DEQUEUE_TIMEOUT_SEC (1s), check stop flag and retry
            continue;
        }
        
        log_msg(LOG_LEVEL_DBG, "Processing command %u", cmd->cmd_id);
        
        // Do actual work
        bool success = dac_config();
        
        // Send result back
        mark_event_done(queue, CMD_DAC_CONFIG, cmd, success);
        
        log_msg(LOG_LEVEL_DBG, "Command %u done, result=%d", cmd->cmd_id, success);
    }
    
    return 0;  // Automatic cleanup via destructor
}
```

### Example 3: Multi-Threaded Configurator

```c
#include "ipc_mailbox.h"
#include <pthread.h>
#include <signal.h>

volatile sig_atomic_t stop = 0;
static SharedQueue* g_queue = NULL;

void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
}

bool dac_config(void)
{
    usleep(100000);
    return true;
}

bool prach_config(void)
{
    usleep(200000);
    return true;
}

// Thread 1: Handle DAC queue
void* dac_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_DAC_CONFIG;
    
    while (!stop)
    {
        CommandMsg* cmd = get_event(g_queue, queue_id);
        if (cmd->type == CMD_EMPTY)
        {
            continue; // Timeout - get_event() already blocked for 1s
        }
        
        log_msg(LOG_LEVEL_DBG, "DAC: Processing command %u", cmd->cmd_id);
        bool success = dac_config();
        mark_event_done(g_queue, queue_id, cmd, success);
    }
    
    return NULL;
}

// Thread 2: Handle PRACH queue
void* prach_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PRACH_CONFIG;
    
    while (!stop)
    {
        CommandMsg* cmd = get_event(g_queue, queue_id);
        if (cmd->type == CMD_EMPTY)
        {
            continue; // Timeout - get_event() already blocked for 1s
        }
        
        log_msg(LOG_LEVEL_DBG, "PRACH: Processing command %u", cmd->cmd_id);
        bool success = prach_config();
        mark_event_done(g_queue, queue_id, cmd, success);
    }
    
    return NULL;
}

int main()
{
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    int is_creator = 0;
    SHARED_QUEUE_AUTO SharedQueue* g_queue = open_or_create_shared_queue(&is_creator);
    if (!g_queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to open/create shared memory");
        return 1;
    }
    
    // Register for both queues
    char name_dac[64];
    snprintf(name_dac, sizeof(name_dac), "dac-configurator-%d", getpid());
    register_configurator(g_queue, name_dac, CMD_DAC_CONFIG);
    
    char name_prach[64];
    snprintf(name_prach, sizeof(name_prach), "prach-configurator-%d", getpid());
    register_configurator(g_queue, name_prach, CMD_PRACH_CONFIG);
    
    log_msg(LOG_LEVEL_LOG, "DAC/PRACH Configurator started (queues %d and %d)", 
            CMD_DAC_CONFIG, CMD_PRACH_CONFIG);
    
    pthread_t dac_tid, prach_tid;
    
    // Start both threads
    if (pthread_create(&dac_tid, NULL, dac_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create DAC thread");
        munmap(g_queue, sizeof(SharedQueue));
        return 1;
    }
    
    if (pthread_create(&prach_tid, NULL, prach_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PRACH thread");
        stop = 1;
        pthread_join(dac_tid, NULL);
        munmap(g_queue, sizeof(SharedQueue));
        return 1;
    }
    
    // Wait for threads
    pthread_join(dac_tid, NULL);
    pthread_join(prach_tid, NULL);
    
    return 0;  // Automatic cleanup via destructor
}
```

---

## Fully Automatic Resource Management

### Zero-Boilerplate Cleanup

The library provides **complete automatic cleanup** using GCC/Clang `__attribute__` extensions:

**1. Automatic Memory Cleanup** - `SHARED_QUEUE_AUTO` macro:
```c
SHARED_QUEUE_AUTO SharedQueue* queue = open_or_create_shared_queue(&is_creator);
// munmap() called automatically at scope exit
```

**2. Automatic Process Unregistration** - `__attribute__((destructor))`:
```c
int main() {
    register_monitor("my-monitor", CMD_DAC_CONFIG);
    // ... do work ...
    return 0;  // unregister_process() called automatically!
}
```

### What Happens Automatically

[+] **Process unregistration** - via destructor on exit/signal  
[+] **Memory unmapping** - via `SHARED_QUEUE_AUTO` cleanup attribute  
[+] **Shared memory cleanup** - creator processes clean up synchronization primitives  
[+] **Shared memory unlinking** - creator processes call `shm_unlink()`  

### Benefits

[+] **Zero boilerplate** - No manual cleanup code needed  
[+] **Signal-safe** - Works with SIGTERM, SIGINT, normal exit  
[+] **Exception safety** - Cleanup happens on early returns, goto, break  
[+] **Can't forget** - Compiler automatically inserts cleanup code  
[+] **Less code** - Applications are shorter and cleaner  

### Migration from Manual Cleanup

**Before (manual cleanup):**
```c
int main() {
    SharedQueue* queue = register_monitor("mon", 1);
    // ... work ...
    unregister_process();  // [-] Manual
    return 0;
}
```

**After (automatic cleanup):**
```c
int main() {
    register_monitor("mon", 1);
    // ... work ...
    return 0;  // [+] Automatic
}
```

**You write less code, the library handles everything!**

### Implementation

Uses GCC's `__attribute__((cleanup))` - supported since GCC 4.0 (2005) and Clang 3.0 (2011):

```c
static inline void cleanup_shared_queue_ptr(SharedQueue** queue_ptr)
{
    if (queue_ptr && *queue_ptr && *queue_ptr != MAP_FAILED)
    {
        munmap(*queue_ptr, sizeof(SharedQueue));
        *queue_ptr = NULL;
    }
}

#define SHARED_QUEUE_AUTO __attribute__((cleanup(cleanup_shared_queue_ptr)))
```

See [RAII_EXAMPLE.md](RAII_EXAMPLE.md) for detailed examples and migration guide.

---

## Advantages

### 1. **Simplicity**
- [OK] Single header file = no library to link
- [OK] Copy `ipc_mailbox.h` to your project, done!
- [OK] No complex build system needed

### 2. **Fast**
- [OK] Shared memory = no data copying (just pointers)
- [OK] `static inline` functions = compiler optimizes aggressively
- [OK] Circular buffer = O(1) enqueue/dequeue

### 3. **Reliable**
- [OK] Process-shared mutex = atomic operations across processes
- [OK] Condition variables = efficient blocking (no busy-wait)
- [OK] Timeout support = never hang forever
- [OK] `kill(pid, 0)` = detect crashed processes instantly

### 4. **Flexible**
- [OK] 16 independent queue pairs = scalable
- [OK] 256 commands per queue = handles bursts
- [OK] Multi-threaded support = one process can handle multiple queues
- [OK] Process registry = health monitoring built-in

### 5. **No Dependencies**
- [OK] Just standard POSIX APIs (works on all Linux)
- [OK] No external libraries (libevent, ZeroMQ, etc.)
- [OK] Minimal code size (~600 lines)

### 6. **Easy Debugging**
- [OK] `status` utility shows all processes and queue states
- [OK] Timestamped logs from `log_msg()`
- [OK] Everything in `/dev/shm` = inspect with `ls -lh /dev/shm`

---

## Limitations

### 1. **Single Machine Only**
- [FAIL] Shared memory requires all processes on same Linux box
- [FAIL] Can't communicate over network
- [TOOL] **Workaround**: Use TCP sockets + serialization for distributed systems

### 2. **Fixed Capacity**
- [FAIL] 256 commands per queue (compile-time constant)
- [FAIL] 16 total queues maximum
- [FAIL] 32 processes maximum in registry
- [TOOL] **Workaround**: Increase `QUEUE_SIZE`, `MAX_QUEUE_PAIRS`, `MAX_PROCESSES` and recompile

### 3. **No Persistence**
- [FAIL] Shared memory vanishes on reboot (lives in `/dev/shm` = tmpfs)
- [FAIL] No durability guarantees if machine crashes
- [TOOL] **Workaround**: Use real IPC message queues (mqueue) or databases for persistence

### 4. **Global Mutex Contention**
- [FAIL] Single mutex for all operations = bottleneck under heavy load
- [FAIL] One slow process can delay others
- [TOOL] **Workaround**: Use per-queue mutexes (requires refactoring)

### 5. **Static Linking Bloat**
- [FAIL] Every executable includes full `ipc_mailbox.h` code
- [FAIL] 5 programs x 600 lines = more binary size than shared library
- [TOOL] **Workaround**: Convert to shared library if size matters

### 6. **No Automatic Cleanup**
- [FAIL] If last process crashes, shared memory stays in `/dev/shm`
- [FAIL] Must manually `rm /dev/shm/cfg_shm` or reboot
- [TOOL] **Workaround**: Use systemd to cleanup on service stop

### 7. **No Versioning**
- [FAIL] If you change struct sizes, old/new processes will crash
- [FAIL] Must restart all processes after recompile
- [TOOL] **Workaround**: Add version field to `SharedQueue`, check on startup

### 8. **Limited Error Handling**
- [FAIL] Returns `NULL` or `-1` on error, but limited diagnostics
- [FAIL] No detailed error codes (just `errno`)
- [TOOL] **Workaround**: Add logging, check `errno` everywhere

---

## Building and Running

### Prerequisites
```bash
# Linux with POSIX threads and shared memory support
sudo apt install build-essential  # Ubuntu/Debian
```

### Build Everything
```bash
make clean
make
```

**Output:**
```
build/configurator_phy          # PHY server
build/configurator_dac_prach    # DAC+PRACH server
build/dac_monitor               # DAC client
build/phy_prach_monitor         # PHY+PRACH client
build/status                    # Status viewer
```

### Run Status Viewer
```bash
./build/status
```

**Example output:**
```
=== System Status ===
Date: 2026-02-08 14:32:10

Process Registry:
PID     Type            Name                    QueueID  Status  CmdsProcessed
12345   CONFIGURATOR    configurator-dac        1        Active  42
12346   MONITOR         dac-monitor-12346       1        Active  0
12347   CONFIGURATOR    configurator-phy        2        Active  28

Active Queue Pairs:
QueueID  CommandType      ActiveCmds  NextCmdID
1        DAC_CONFIG       0           43
2        PHY_CONFIG       1           29
```

### Basic Test Scenario

**Terminal 1** - Start DAC configurator:
```bash
./build/configurator_dac_prach
# [14:30:01.234] First process - creating shared memory
# [14:30:01.235] DAC thread started
# [14:30:01.235] PRACH thread started
```

**Terminal 2** - Start PHY configurator:
```bash
./build/configurator_phy
# [14:30:05.123] Shared memory exists - waiting for initialization
# [14:30:05.124] PHY Configurator started
```

**Terminal 3** - Start DAC monitor:
```bash
./build/dac_monitor
# [14:30:10.456] DAC configurator available, sending command...
# [14:30:10.556] Command 1 completed successfully
# [14:30:13.456] DAC configurator available, sending command...
# [14:30:13.556] Command 2 completed successfully
```

**Terminal 4** - Check status:
```bash
./build/status
# Shows all 3 processes registered and active
```

**Terminal 5** - Kill configurator, watch monitor adapt:
```bash
kill <configurator_pid>

# Monitor output:
# [14:31:00.789] DAC configurator not registered, waiting...
# [14:31:03.789] DAC configurator not registered, waiting...
```

### Cleanup
```bash
# Stop all processes (Ctrl+C or kill)
# Remove shared memory manually if needed:
rm /dev/shm/cfg_shm
```

### Using with systemd (Production)

Create `/etc/systemd/system/configurator_dac.service`:
```ini
[Unit]
Description=DAC Configurator
After=network.target

[Service]
Type=simple
ExecStart=/path/to/build/configurator_dac_prach
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable configurator_dac.service
sudo systemctl start configurator_dac.service
```

### Automated Testing

Run the comprehensive test suite:
```bash
# Standard functional tests (5 scenarios, ~30 seconds)
make test

# Statistics stress test (SUCCESS/FAILURE/TIMEOUT counters, ~40 seconds)
make test-stats

# Or run directly
./test_scenarios.sh
./test_stress_stats.sh
```

**Test Coverage:**

**Standard Tests (`make test`):**
1. **Basic Functionality** - Normal operation with all processes
2. **Crash Recovery** - Configurator crashes, monitor gracefully waits
3. **Monitor First** - Monitor starts before configurator (waits patiently)
4. **Multi-threaded Load** - Stress test with multiple queues/threads
5. **Shared Memory Integrity** - First process initialization pattern

**Statistics Test (`make test-stats`):**
- Demonstrates **SUCCESS/FAILURE/TIMEOUT** counters in action
- 4 phases: normal operation -> kill PHY -> kill all -> restart
- Uses `stress_monitor` to generate continuous load
- Shows real-time statistics with `./build/status`

**Stress Monitor:**

`stress_monitor` is a testing utility that sends events continuously without checking configurator registration:

```bash
# Usage
./build/stress_monitor <queue_id>

# Examples
./build/stress_monitor 1  # Send to Queue 1 (DAC)
./build/stress_monitor 2  # Send to Queue 2 (PHY)
./build/stress_monitor 3  # Send to Queue 3 (PRACH)
```

**Why stress_monitor?**
- **Generate timeouts**: Regular monitors check `is_configurator_ready()` once at startup. Stress monitor sends immediately, so when you kill configurator, it generates TIMEOUT stats.
- **Continuous load**: Sends events every 1 second for testing throughput
- **Statistics testing**: Essential for demonstrating SUCCESS/FAILURE/TIMEOUT counters

**Viewing Statistics:**

```bash
./build/status
```

Shows combined queue status & statistics:
```
Queue Status & Statistics:
QUEUE  QUEUED     NEXT_ID    SUCCESS    FAILURE    TIMEOUT    TOTAL
-----  ------     -------    -------    -------    -------    -----
1      0          18         13         1          4          18
2      1          18         13         0          5          18
3      0          18         14         0          4          18
```

**Expected Output:**
```
+==========================================================+
|     Queue Pair IPC System - Test Suite                  |
+==========================================================+

[OK][OK][OK] Test 1 PASSED
[OK][OK][OK] Test 2 PASSED (graceful degradation verified)
[OK][OK][OK] Test 3 PASSED
[OK][OK][OK] Test 4 PASSED
[OK][OK][OK] Test 5 PASSED

+==========================================================+
|              [OK][OK][OK] ALL TESTS PASSED [OK][OK][OK]                   |
+==========================================================+
```

**What Tests Verify:**
- [OK] Processes can start in any order
- [OK] Monitors detect crashed configurators (kill check)
- [OK] No automatic restart loops (monitors wait patiently)
- [OK] Multi-threaded configurators work correctly
- [OK] Shared memory initialization is thread-safe
- [OK] No process crashes under normal/abnormal conditions
- [OK] Status utility accurately reflects system state
- [OK] SUCCESS/FAILURE/TIMEOUT statistics tracking works correctly
- [OK] System recovers gracefully after configurator restarts

---

## FAQ

### Q: What happens if configurator crashes?

**A:** Monitor detects via natural error signals (queue full, timeout). Monitor waits at startup using `is_configurator_ready()`. Use systemd to auto-restart configurator.

### Q: Can multiple monitors send to same queue?

**A:** Yes! Mutex protects concurrent enqueues. Each command gets unique ID. Configurator processes in FIFO order.

### Q: What if queue fills up (256 commands)?

**A:** `set_event()` returns `0` (failure). Monitor should retry later or increase `QUEUE_SIZE`.

### Q: How to add new command type?

```c
// 1. Add to enum in ipc_mailbox.h
typedef enum {
    CMD_DAC_CONFIG = 1,
    CMD_PHY_CONFIG = 2,
    CMD_PRACH_CONFIG = 3,
    CMD_NEW_THING = 4    // <- Add here
} CommandType;

// 2. Update cmd_type_name()
const char* cmd_type_name(CommandType type) {
    switch(type) {
        case CMD_NEW_THING: return "NEW_THING";
        // ...
    }
}

// 3. Create configurator_new_thing.c (copy configurator_phy.c template)
// 4. Create new_thing_monitor.c (copy dac_monitor.c template)
// 5. Update Makefile
```

### Q: How to monitor performance?

Use `status` utility to see `CmdsProcessed` counter. Add timestamps to commands for latency measurements.

### Q: Thread-safe?

**Yes**. All operations use global mutex. Safe to call from multiple threads in same process. Use `pthread_detach()` for worker threads (see `configurator_dac_prach.c`).

---

## Summary

This is a **simple, fast, single-include IPC library** for Linux. Perfect for:
- [OK] Multi-process applications on single machine
- [OK] Low latency request-response patterns
- [OK] Rapid prototyping (just copy `ipc_mailbox.h`)
- [OK] Learning POSIX IPC concepts

**Not suitable for:**
- [FAIL] Distributed systems (use gRPC, ZeroMQ)
- [FAIL] Persistence requirements (use databases)
- [FAIL] Extreme scalability (use message brokers)

**Philosophy**: Simple, correct, fast (pick three). No magic, no hidden state, no surprises.

---

**Author:** Michael Freidkin  
**Date:** February 2026  
**Version:** 2.0  


