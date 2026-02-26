
# MON — Publish-Subscribe Status Monitor

Lightweight publish-subscribe typed-status system for inter-process status sharing.

---

## Project layout (relevant)

```
mon/
├── common.h             # Shared type definitions
├── mon.h                # Single-header status monitor library
├── publisher_phy.c      # PHY publisher example
├── publisher_dac_prach.c# DAC+PRACH publisher example
├── subscriber_*         # Subscriber example programs
└── README.md            # This file
```

---

## Overview

`mon` implements a type-safe publish-subscribe pattern using POSIX shared memory.
Producers (publishers) register named status objects and publish typed data.
Consumers (subscribers) attach to the shared memory and receive notifications
when a named status changes.

---

## Architecture Overview

```
+---------------------------------------------------------+
|              POSIX Shared Memory (/typed_status_shm)    |
|                                                         |
|  +--------------------------------------------------+   |
|  |        Typed Status Shared Memory Structure      |   |
|  +--------------------------------------------------+   |
|  |  Data Pool (1MB) + Status Array + Event Pool     |   |
|  +--------------------------------------------------+   |
|                                                         |
|  Publisher Process:                                     |
|  +--------------------------------------------+         |
|  | Registers "dac_status" with DacStatus data |         |
|  | Calls typed_status_set() to update         |         |
|  | Increments version atomically              |         |
|  +--------------------------------------------+         |
|                                                         |
|  Subscriber Process:                                    |
|  +--------------------------------------------+         |
|  | Subscribes to "dac_status"                 |         |
|  | Receives eventfd notification              |         |
|  | Copies data and invokes callback           |         |
|  +--------------------------------------------+         |
|                                                         |
+---------------------------------------------------------+
```

### Workflow Steps

1. **Initialization**: Publisher calls `typed_status_init_publisher()` to create or attach to shared memory. Subscriber calls `typed_status_init_subscriber()` to attach.

2. **Registration**: Publisher registers named statuses with `typed_status_register()`, allocating space in the shared data pool.

3. **Subscription**: Subscriber subscribes to status names with `typed_status_subscribe()`, providing a callback function.

4. **Publishing**: Publisher updates status data with `typed_status_set()`, which atomically increments the version and notifies subscribers via eventfd.

5. **Notification**: Subscriber threads wake on eventfd signals, copy the updated data, and invoke the callback with old/new data pointers.

6. **Cleanup**: Automatic cleanup on process exit via destructor attributes.

---

## Message Flow and Data Handling

### How Updates Propagate

When a publisher calls `typed_status_set("dac_status", &new_dac)`:

1. **Data Copy**: The new `DacStatus` structure is copied into the shared memory data pool at the pre-allocated offset.

2. **Version Increment**: The status's atomic version counter is incremented (e.g., from 5 to 6).

3. **Notification**: Eventfds are signaled for the publisher's eventfd and all subscribed subscriber eventfds.

4. **Subscriber Wakeup**: Each subscriber thread wakes from `poll()` and checks if the subscribed status version has changed.

5. **Data Protection**: If changed, the subscriber copies the **old** data (from previous callback) and **new** data (from shared memory) into local memory buffers.

6. **Callback Invocation**: The callback is called with `callback(status_name, old_copy, new_copy, type_name, user_data)`.

### Custom Structure Passing

Callbacks receive `const void* old_data` and `const void* new_data` pointers to protected copies:

```c
void my_callback(const char* status_name, const void* old_data, const void* new_data, const char* type_name, void* user_data) {
    // Cast to your custom structure
    const DacStatus* old_dac = (const DacStatus*)old_data;
    const DacStatus* new_dac = (const DacStatus*)new_data;

    if (old_dac) {
        printf("Old gain: %u\n", old_dac->gain);
    }
    printf("New gain: %u\n", new_dac->gain);

    // Safe to access all fields - data is copied and protected
}
```

**Key Points**:
- Data pointers are to **local copies**, not shared memory - no synchronization needed in callbacks.
- `old_data` may be `NULL` on first update (no previous data).
- Structures are passed by value semantics - full copies ensure thread safety.
- Use `common.h` types for consistent casting across processes.

Key properties:
- **Single-include library**: No separate compilation or linking required - just include `mon.h` in your source for a complete, self-contained implementation.
- Offset-based shared memory addressing (safe across processes)
- Atomic version counters per-status for change detection
- Eventfd-based notifications to wake subscribers efficiently
- Read-write locks for safe concurrent access

---

## When to use

- Use `mon` when you need lightweight, schema-aware status sharing between
  processes (telemetry, configuration status, heartbeat, etc.).
- Prefer `ipc` (command/response) when you need request/response RPC semantics.

---

## Quick start (examples)

1. Build the project (from parent directory):

```bash
cd ..
make
```

2. Start publishers (from mon/ directory):

```bash
../build/publisher_phy &
../build/publisher_dac_prach &
```

3. Inspect registered statuses:

```bash
../build/mon_status
```

4. Start a subscriber example:

```bash
../build/subscriber_dac
```

---

## Public API (high-level)

The single-header exposes simple, easy-to-use functions:

- `bool typed_status_init_publisher(void);`
  - Initialize and (if needed) create the shared memory region.

- `int typed_status_register(const char* status_name, size_t type_size, const void* initial_data);`
  - Register a named status and optionally provide initial value.

- `int typed_status_set(const char* status_name, const void* new_data);`
  - Update a status value; the version is incremented atomically and
    subscribers are notified.

- `bool typed_status_init_subscriber(void);`
  - Attach to the existing shared memory as a subscriber process.

- `int32_t typed_status_subscribe(const char* status_name, typed_status_simple_callback_t cb);`
  - Register a callback to be invoked when `status_name` changes.

- `void* typed_status_get_copy(const char* status_name, uint32_t* version, uint64_t* timestamp);`
  - Grab a copy of current data (caller frees the returned pointer).

See `mon.h` for full signatures and details.

---

## Code Examples

### Publisher Example

```c
#include "common.h"
#include "mon.h"
#include <signal.h>

volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // Initialize as publisher
    if (!typed_status_init_publisher()) {
        fprintf(stderr, "Failed to init publisher\n");
        return 1;
    }

    // Register a status with initial data
    DacStatus dac = {.gain = 100, .frequency = 2400000, .amplitude = 1000, .enabled = 1};
    if (typed_status_register("dac_status", sizeof(DacStatus), &dac) < 0) {
        fprintf(stderr, "Failed to register dac_status\n");
        return 1;
    }

    printf("Publisher started\n");

    // Update status periodically
    while (running) {
        sleep(2);
        dac.gain += 10;  // Modify data
        typed_status_set("dac_status", &dac);
        printf("Updated DAC gain to %u\n", dac.gain);
    }

    printf("Publisher stopped\n");
    return 0;
}
```

### Subscriber Example

```c
#include "common.h"
#include "mon.h"
#include <signal.h>

volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

// Callback invoked when status changes
void dac_callback(const void* old_data, const void* new_data) {
    const DacStatus* old_dac = old_data;
    const DacStatus* new_dac = new_data;
    printf("DAC updated: gain %u -> %u\n",
           old_dac ? old_dac->gain : 0, new_dac->gain);
}

int main() {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // Initialize as subscriber
    if (!typed_status_init_subscriber()) {
        fprintf(stderr, "Failed to init subscriber\n");
        return 1;
    }

    // Subscribe to status changes
    int32_t sub_id = typed_status_subscribe("dac_status", dac_callback);
    if (sub_id < 0) {
        fprintf(stderr, "Failed to subscribe to dac_status\n");
        return 1;
    }

    printf("Subscriber started (sub_id=%d)\n", sub_id);

    // Wait for updates
    while (running) {
        sleep(1);
    }

    printf("Subscriber stopped\n");
    return 0;
}
```

These examples demonstrate basic publisher and subscriber usage. Build them with `mon.h` and `common.h` included, and link against required libraries (pthread, rt).

---

## Implementation notes

- Shared memory object name: `/typed_status_shm` (see `mon.h`).
- The library reserves offset `0` in the internal data pool as an invalid
  sentinel; valid data offsets are non-zero.
- Each registered status contains an atomic `version` counter that publishers
  increment when updating data — subscribers can compare versions to detect
  fresh updates.
- Eventfds are used for notifications to avoid busy-waiting.

### Using `common.h`

The project provides a `common.h` header with shared type definitions and
helpers used by both publishers and subscribers. Include `common.h` in any
example or application that interacts with typed statuses to ensure consistent
struct definitions (for example `DacStatus`, `PhyStatus`, and `PrachStatus`).

Typical usage:

```c
#include "common.h"
#include "mon.h"

// Register or read a status using the types defined in common.h
typed_status_register("dac_status", sizeof(DacStatus), &initial_dac);
```

Keeping `common.h` in sync across build units prevents size/alignment
mismatches when storing typed data in shared memory.

---

## Troubleshooting

- If `mon_status` shows fewer statuses than expected:
  - Ensure the publisher process actually called `typed_status_register()`.
  - Check for shared memory conflicts or leftover `/dev/shm/typed_status_shm` —
    remove it if stale and restart publishers.

- If versions do not appear to increment:
  - Confirm publishers call `typed_status_set()` periodically.
  - Increase test wait intervals if updates are slow.

---

## Tests

See `mon/test.sh` for a comprehensive test suite validating registration,
data accessibility, versioning, and subscriber behavior.

---

## Contact

Report issues or questions to the repository maintainers.
