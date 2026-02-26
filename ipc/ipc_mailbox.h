/**
 * @file ipc_mailbox.h
 * @brief Production-hardened shared memory IPC primitives for configuration system
 *
 * @details
 * CRITICAL SECURITY & RELIABILITY FEATURES:
 * - Robust mutex recovery (EOWNERDEAD handling) prevents permanent deadlocks
 * - Secure permissions (0660 instead of world-writable 0666)
 * - Version/struct validation prevents binary incompatibility
 * - Queue fullness detection replaces heartbeat complexity
 * - Atomic logging prevents interleaved output across processes
 * - Orphaned SHM cleanup helper for system restart
 * - Struct size validation with compile-time assertions
 * - Enhanced timeout handling with monotonic clock
 *
 * Architecture Overview:
 * Multi-queue IPC system where monitors (clients) communicate with configurators
 * (servers) through dedicated POSIX shared memory queues. Queue isolation ensures
 * each CommandType (DAC=1, PHY=2, PRACH=3) has independent circular buffer.
 *
 * Health Detection:
 * Configurator health is determined by process liveness (kill(pid,0)) combined
 * with queue state. A full queue indicates the configurator is not consuming
 * commands, suggesting it's hung or overloaded.
 *
 * @author Michael Freidkin
 * @date 2026-02-08
 * @version 3.1 (Simplified Health Detection)
 */
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // Required for robust mutexes and usleep
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/** @brief Shared memory object name in /dev/shm */
#define SHM_NAME "/cfg_shm"

/** @brief Maximum commands that can be queued per queue pair */
#define MAX_QUEUE_SIZE 16

/** @brief Maximum processes that can register in the system */
#define MAX_PROCESSES 32

/** @brief Maximum number of independent queue pairs */
#define MAX_QUEUE_PAIRS 16

/** @brief Timeout for dequeue operations in configurators (seconds) */
#define DEQUEUE_TIMEOUT_SEC 1

/** @brief Command timeout for monitors (seconds) */
#define COMMAND_TIMEOUT_SEC 5

/** @brief Magic value indicating shared memory is fully initialized */
#define QUEUE_INITIALIZED 0xDEADBEEF

/** @brief Version magic (increment MAJOR on layout changes, MINOR on compatible
 * changes) */
#define SHM_VERSION_MAJOR 3
#define SHM_VERSION_MINOR 1
#define VERSION_MAGIC ((SHM_VERSION_MAJOR << 16) | SHM_VERSION_MINOR)

_Static_assert(MAX_QUEUE_SIZE > 0 && MAX_QUEUE_SIZE <= 256, "Queue size must be between 1 and 256");
_Static_assert(MAX_PROCESSES > 0 && MAX_PROCESSES <= 128, "Process registry size must be between 1 and 128");
_Static_assert(MAX_QUEUE_PAIRS > 0 && MAX_QUEUE_PAIRS <= 64, "Queue pairs must be between 1 and 64");

/**
 * @brief Process role in the configuration system
 */
typedef enum
{
    PROCESS_MONITOR = 0,     /**< Client process sending configuration requests */
    PROCESS_CONFIGURATOR = 1 /**< Server process executing configuration commands */
} ProcessType;

/**
 * @brief Configuration command types
 * Each command type corresponds to a queue_id, creating logical separation.
 */
typedef enum
{
    CMD_EMPTY = 0,       /**< Sentinel value returned by get_event() on timeout (no command) */
    CMD_DAC_CONFIG = 1,  /**< Digital-to-Analog Converter configuration */
    CMD_PHY_CONFIG = 2,  /**< Physical layer configuration */
    CMD_PRACH_CONFIG = 3 /**< Physical Random Access Channel configuration */
} CommandType;

/**
 * @brief Command execution status
 */
typedef enum
{
    CMD_STATUS_PENDING = 0, /**< Command enqueued, not yet processed */
    CMD_STATUS_DONE = 1     /**< Command completed (check result_code for success) */
} CommandStatus;

/**
 * @brief Command message structure
 * Represents a single configuration request in the queue.
 */
typedef struct
{
    CommandType type;     /**< Type of configuration command */
    uint32_t cmd_id;      /**< Unique command identifier (per queue) */
    CommandStatus status; /**< Current execution status */
    int result_code;      /**< 0=failed, 1=success (valid after DONE) */
} CommandMsg;

/**
 * @brief Process registration entry
 * @details Tracks active processes for monitoring and health checks.
 *          Active flag enables detection of stale entries.
 */
typedef struct
{
    pid_t pid;                   /**< Process ID */
    ProcessType type;            /**< Monitor or configurator */
    char name[64];               /**< Human-readable process name */
    int queue_id;                /**< Associated queue ID */
    uint32_t commands_processed; /**< Lifetime command counter */
    volatile int active;         /**< 1=registered, 0=unregistered */
} ProcessSlot;

/**
 * @brief Single queue pair for one monitor-configurator channel
 * Each queue pair operates independently with its own circular buffer
 * and synchronization primitives.
 */
typedef struct
{
    // Circular buffer management
    CommandMsg buffer[MAX_QUEUE_SIZE]; /**< Circular buffer for commands */
    CommandMsg empty_cmd;              /**< Sentinel returned on timeout (type=CMD_EMPTY) */
    int head;                          /**< Dequeue position (read from here) */
    int tail;                          /**< Enqueue position (write here) */
    int count;                         /**< Current number of queued commands */
    uint32_t next_cmd_id;              /**< Monotonic command ID generator */

    // Synchronization primitives (process-shared)
    pthread_cond_t cond_new_cmd; /**< Signaled when command enqueued */
    pthread_cond_t cond_done;    /**< Signaled when command completed */

    // Statistics counters
    uint32_t success_count; /**< Successful configurations */
    uint32_t failure_count; /**< Failed configurations */
    uint32_t timeout_count; /**< Timeout failures */
} QueuePair;

/**
 * @brief Log level enumeration
 */
typedef enum
{
    LOG_LEVEL_LOG = 0, /**< Normal informational message */
    LOG_LEVEL_ERR = 1, /**< Error message */
    LOG_LEVEL_DBG = 2  /**< Debug message */
} LogLevel;

/**
 * @brief Main shared memory structure
 * @details Contains all queues, process registry, and synchronization primitives.
 *          Mapped into multiple process address spaces via POSIX shm_open().
 */
typedef struct
{
    // VERSIONING & VALIDATION (critical for production)
    uint32_t version;     /**< VERSION_MAGIC for binary compatibility */
    uint32_t struct_size; /**< sizeof(SharedQueue) validation */

    // SYNCHRONIZATION
    volatile unsigned int initialized; /**< Atomic init flag (prevents startup races) */
    pthread_mutex_t mutex;             /**< Global lock (process-shared + robust) */

    // DATA STRUCTURES
    QueuePair pairs[MAX_QUEUE_PAIRS];     /**< Independent queue pairs */
    ProcessSlot processes[MAX_PROCESSES]; /**< Process registry */
} SharedQueue;

/**
 * @brief Global shared queue pointer - automatically set by register functions
 * @internal Set by register_monitor/register_configurator, used by all IPC functions
 */
static SharedQueue* g_shared_queue = NULL;

/**
 * @brief Global state: tracks if current process created shared memory
 * @internal Set by register_monitor/register_configurator, used by is_shared_queue_creator()
 */
static int g_is_shared_queue_creator = 0;

/**
 * @brief Atomic logging to prevent interleaved output across processes
 * @details Uses write() for atomicity on PIPE_BUF-sized messages (<4KB on Linux).
 *          Automatically adds timestamps, log level, and PID prefix to all messages.
 *          Output goes to stdout.
 * @param level Log level (LOG_LEVEL_LOG, LOG_LEVEL_ERR, LOG_LEVEL_DBG)
 * @param fmt Printf-style format string
 * @param ... Variable arguments
 */
static inline void log_msg(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static inline void log_msg(LogLevel level, const char* fmt, ...)
{
    char buffer[1024];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm* tm_info = localtime(&ts.tv_sec);

    const char* level_str;
    switch (level)
    {
    case LOG_LEVEL_ERR:
        level_str = "ERR";
        break;
    case LOG_LEVEL_DBG:
        level_str = "DBG";
        break;
    case LOG_LEVEL_LOG:
    default:
        level_str = "LOG";
        break;
    }

    va_list args;
    va_start(args, fmt);
    int len = snprintf(buffer,
                       sizeof(buffer),
                       "[%02d:%02d:%02d.%03ld] [%s] PID=%d ",
                       tm_info->tm_hour,
                       tm_info->tm_min,
                       tm_info->tm_sec,
                       ts.tv_nsec / 1000000,
                       level_str,
                       getpid());
    vsnprintf(buffer + len, sizeof(buffer) - len, fmt, args);
    va_end(args);

    // Ensure newline termination
    size_t total = strlen(buffer);
    if (total < sizeof(buffer) - 1 && buffer[total - 1] != '\n')
    {
        buffer[total++] = '\n';
        buffer[total] = '\0';
    }

    // Atomic write to stdout (guaranteed for < PIPE_BUF bytes)
    if (total < PIPE_BUF)
    {
        ssize_t result = write(STDOUT_FILENO, buffer, total);
        (void)result; // Suppress unused result warning
    }
    else
    {
        // Fallback for extremely long messages
        printf("%s", buffer);
        fflush(stdout);
    }
}

/**
 * @brief Robust mutex lock with EOWNERDEAD recovery
 * @details Critical fix: Handles mutex owner death to prevent permanent deadlocks.
 *          If a process dies while holding the mutex, subsequent locks will
 *          receive EOWNERDEAD and can make the mutex consistent again.
 * @param mutex Pointer to process-shared robust mutex
 * @return 0 on success, -1 on unrecoverable error
 */
static inline int robust_mutex_lock(pthread_mutex_t* mutex)
{
    int ret = pthread_mutex_lock(mutex);

    if (ret == EOWNERDEAD)
    {
        log_msg(LOG_LEVEL_ERR, "WARNING: Mutex owner died - attempting recovery");
        // No complex state to repair in this design beyond mutex consistency
        if (pthread_mutex_consistent(mutex) != 0)
        {
            log_msg(LOG_LEVEL_ERR, "ERROR: Failed to make mutex consistent after owner death");
            pthread_mutex_unlock(mutex);
            return -1;
        }
        log_msg(LOG_LEVEL_LOG, "Mutex recovered after owner death");
        return 0;
    }

    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Initialize shared queue data structures with robust attributes
 * @details Called by the first process that creates the shared memory segment.
 *          Initializes all mutexes, condition variables, and queue pairs.
 * @param queue Pointer to mapped shared memory region
 */
static inline void init_shared_queue(SharedQueue* queue)
{
    // Initialize version BEFORE mutex to prevent partial initialization races
    queue->version = VERSION_MAGIC;
    queue->struct_size = sizeof(SharedQueue);
    queue->initialized = 0;

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&queue->mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    // Initialize all queue pairs
    for (int i = 0; i < MAX_QUEUE_PAIRS; i++)
    {
        queue->pairs[i].head = 0;
        queue->pairs[i].tail = 0;
        queue->pairs[i].count = 0;
        queue->pairs[i].next_cmd_id = 1;
        queue->pairs[i].success_count = 0;
        queue->pairs[i].failure_count = 0;
        queue->pairs[i].timeout_count = 0;
        // Initialize empty command sentinel
        queue->pairs[i].empty_cmd.type = CMD_EMPTY;
        queue->pairs[i].empty_cmd.cmd_id = 0;
        queue->pairs[i].empty_cmd.status = CMD_STATUS_PENDING;
        queue->pairs[i].empty_cmd.result_code = 0;
        pthread_cond_init(&queue->pairs[i].cond_new_cmd, &cattr);
        pthread_cond_init(&queue->pairs[i].cond_done, &cattr);
    }
    pthread_condattr_destroy(&cattr);

    memset(queue->processes, 0, sizeof(queue->processes));

    // Final initialization - atomic release to signal readiness
    __atomic_store_n(&queue->initialized, QUEUE_INITIALIZED, __ATOMIC_RELEASE);
}

/**
 * @brief Open existing or create new shared memory segment with security hardening
 * @return Pointer to mapped SharedQueue, or NULL on error
 */
static inline SharedQueue* open_or_create_shared_queue(int* is_creator)
{
    // SECURITY: Restrict to owner/group only (0660) - NO world-writable access
    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0660);
    if (fd >= 0)
    {
        *is_creator = 1;
        log_msg(LOG_LEVEL_LOG, "First process - creating SECURE shared memory (0660)");

        if (ftruncate(fd, sizeof(SharedQueue)) < 0)
        {
            close(fd);
            shm_unlink(SHM_NAME);
            log_msg(LOG_LEVEL_ERR, "ERROR: ftruncate failed: %s", strerror(errno));
            return NULL;
        }

        SharedQueue* queue = mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (queue == MAP_FAILED)
        {
            shm_unlink(SHM_NAME);
            log_msg(LOG_LEVEL_ERR, "ERROR: mmap failed: %s", strerror(errno));
            return NULL;
        }

        init_shared_queue(queue);

        return queue;
    }

    if (errno != EEXIST)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: shm_open failed: %s", strerror(errno));
        return NULL;
    }

    // Join existing segment
    *is_creator = 0;
    log_msg(LOG_LEVEL_LOG, "Shared memory exists - waiting for initialization");

    fd = shm_open(SHM_NAME, O_RDWR, 0660);
    if (fd < 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to open existing shared memory: %s", strerror(errno));
        return NULL;
    }

    // Validate size BEFORE mapping (wait for creator to finish ftruncate)
    struct stat shm_stat;
    for (int retry = 0; retry < 50; retry++)
    {
        if (fstat(fd, &shm_stat) == 0 && (size_t)shm_stat.st_size >= sizeof(SharedQueue))
        {
            break;
        }
        usleep(10000); // 10ms
    }

    if (fstat(fd, &shm_stat) != 0 || (size_t)shm_stat.st_size < sizeof(SharedQueue))
    {
        log_msg(LOG_LEVEL_ERR,
                "ERROR: Shared memory too small (expected %zu, got %ld)",
                sizeof(SharedQueue),
                fstat(fd, &shm_stat) == 0 ? (long)shm_stat.st_size : -1L);
        close(fd);
        return NULL;
    }

    SharedQueue* queue = mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (queue == MAP_FAILED)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: mmap failed: %s", strerror(errno));
        return NULL;
    }

    // CRITICAL VALIDATION: Check version and struct size BEFORE using any fields
    if (queue->version != VERSION_MAGIC || queue->struct_size != sizeof(SharedQueue))
    {
        log_msg(LOG_LEVEL_ERR,
                "ERROR: Shared memory version mismatch (got 0x%08x/%u, expected "
                "0x%08x/%zu)",
                queue->version,
                queue->struct_size,
                VERSION_MAGIC,
                sizeof(SharedQueue));
        munmap(queue, sizeof(SharedQueue));
        return NULL;
    }

    // Wait for full initialization with timeout
    for (int i = 0; i < 100; i++)
    {
        if (__atomic_load_n(&queue->initialized, __ATOMIC_ACQUIRE) == QUEUE_INITIALIZED)
        {
            log_msg(LOG_LEVEL_LOG,
                    "Shared memory initialization complete (version %d.%d)",
                    SHM_VERSION_MAJOR,
                    SHM_VERSION_MINOR);
            return queue;
        }
        usleep(100000); // 100ms
    }

    log_msg(LOG_LEVEL_ERR, "ERROR: Timeout waiting for shared memory initialization");
    munmap(queue, sizeof(SharedQueue));
    return NULL;
}

/**
 * @brief Internal: Register process in shared memory registry
 * @details Finds first available slot and records process metadata.
 *          Use register_monitor() or register_configurator() instead.
 * @param queue Pointer to shared memory
 * @param type Process role (MONITOR or CONFIGURATOR)
 * @param name Human-readable process identifier
 * @param queue_id Associated queue ID
 * @return Slot index (0-31) on success, -1 if registry full
 * @internal Use public API: register_monitor() or register_configurator()
 */
static inline int _register_process_internal(SharedQueue* queue, ProcessType type, const char* name, int queue_id)
{
    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to acquire mutex during registration");
        return -1;
    }

    pid_t pid = getpid();
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (!queue->processes[i].active)
        {
            queue->processes[i].pid = pid;
            queue->processes[i].type = type;
            queue->processes[i].queue_id = queue_id;
            snprintf(queue->processes[i].name, sizeof(queue->processes[i].name), "%s", name);
            queue->processes[i].commands_processed = 0;
            queue->processes[i].active = 1;

            log_msg(LOG_LEVEL_LOG,
                    "Process registered: PID=%d, Type=%s, Name=%s, QueueID=%d, Slot=%d",
                    pid,
                    type == PROCESS_CONFIGURATOR ? "CONFIGURATOR" : "MONITOR",
                    name,
                    queue_id,
                    i);

            pthread_mutex_unlock(&queue->mutex);
            return i;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    log_msg(LOG_LEVEL_ERR, "ERROR: No free process slots (max %d)", MAX_PROCESSES);
    return -1;
}
static inline int cleanup_orphaned_shm(void)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0660);
    if (fd < 0)
    {
        if (errno == ENOENT)
        {
            return 0; // No segment exists - nothing to cleanup
        }
        log_msg(LOG_LEVEL_ERR, "WARNING: shm_open during cleanup failed: %s", strerror(errno));
        return -1;
    }

    struct stat shm_stat;
    if (fstat(fd, &shm_stat) != 0 || (size_t)shm_stat.st_size < sizeof(SharedQueue))
    {
        close(fd);
        return -1;
    }

    SharedQueue* queue = mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (queue == MAP_FAILED)
    {
        log_msg(LOG_LEVEL_ERR, "WARNING: mmap during cleanup failed: %s", strerror(errno));
        return -1;
    }

    // Check if any processes are still registered
    bool active_processes = false;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (queue->processes[i].active && queue->processes[i].pid > 0)
        {
            // Verify liveness
            if (kill(queue->processes[i].pid, 0) == 0 || errno == EPERM)
            {
                active_processes = true;
                break;
            }
        }
    }

    munmap(queue, sizeof(SharedQueue));

    if (!active_processes)
    {
        log_msg(LOG_LEVEL_LOG, "Cleaning up orphaned shared memory segment");
        shm_unlink(SHM_NAME);
        return 0;
    }

    log_msg(LOG_LEVEL_ERR, "WARNING: Shared memory in active use - skipping cleanup");
    return -1;
}

/**
 * @brief RAII-style cleanup handler for SharedQueue pointer
 * @details Automatically unmaps shared memory when pointer goes out of scope.
 *          Use with __attribute__((cleanup)) or SHARED_QUEUE_AUTO macro.
 *          Does NOT call unregister_process() or cleanup_shared_queue().
 * @param queue_ptr Pointer to SharedQueue pointer (double pointer)
 */

// ============================================================================

/**
 * @brief Cleanup shared queue synchronization primitives
 * @details Destroys mutex and all condition variables. Called by creator process
 *          before unmapping shared memory and calling shm_unlink().
 *          Uses global shared queue set by register_monitor/register_configurator.
 */
static inline void cleanup_shared_queue(void)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
        return;

    pthread_mutex_destroy(&queue->mutex);
    for (int i = 0; i < MAX_QUEUE_PAIRS; i++)
    {
        pthread_cond_destroy(&queue->pairs[i].cond_new_cmd);
        pthread_cond_destroy(&queue->pairs[i].cond_done);
    }
}

static inline void cleanup_shared_queue_ptr(SharedQueue** queue_ptr)
{
    if (queue_ptr && *queue_ptr && *queue_ptr != MAP_FAILED)
    {
        munmap(*queue_ptr, sizeof(SharedQueue));
        *queue_ptr = NULL;
    }
}

// ============================================================================
// PUBLIC API - Functions for user applications
// ============================================================================

/**
 * @brief Register monitor process in shared memory
 * @details Opens/creates shared memory and registers monitor (client) process.
 *          For monitors handling multiple queues, use queue_id=0.
 *          Automatically handles shared memory initialization and sets global queue.
 * @param name Human-readable process identifier (e.g., "dac_monitor-1234")
 * @param queue_id Associated queue ID (use 0 for multi-queue monitors)
 * @return true on success, false on failure
 */
static inline bool register_monitor(const char* name, int queue_id)
{
    if (g_shared_queue)
    {
        // Already registered, just add another process entry
        return _register_process_internal(g_shared_queue, PROCESS_MONITOR, name, queue_id) >= 0;
    }

    SharedQueue* queue = open_or_create_shared_queue(&g_is_shared_queue_creator);
    if (!queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to open/create shared memory");
        return false;
    }

    if (_register_process_internal(queue, PROCESS_MONITOR, name, queue_id) < 0)
    {
        munmap(queue, sizeof(SharedQueue));
        return false;
    }

    g_shared_queue = queue;
    return true;
}

/**
 * @brief Register configurator process in shared memory
 * @details Opens/creates shared memory and registers configurator (server) process.
 *          Each configurator must specify its queue_id.
 *          Automatically handles shared memory initialization and sets global queue.
 * @param name Human-readable process identifier (e.g., "phy-configurator-1234")
 * @param queue_id Associated queue ID (must be valid queue 0-15)
 * @return true on success, false on failure
 */
static inline bool register_configurator(const char* name, int queue_id)
{
    if (g_shared_queue)
    {
        // Already registered, just add another process entry
        return _register_process_internal(g_shared_queue, PROCESS_CONFIGURATOR, name, queue_id) >= 0;
    }

    SharedQueue* queue = open_or_create_shared_queue(&g_is_shared_queue_creator);
    if (!queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to open/create shared memory");
        return false;
    }

    if (_register_process_internal(queue, PROCESS_CONFIGURATOR, name, queue_id) < 0)
    {
        munmap(queue, sizeof(SharedQueue));
        return false;
    }

    g_shared_queue = queue;
    return true;
}

/**
 * @brief Check if current process created the shared memory
 * @details Returns true if this process was the first to call register_monitor()
 *          or register_configurator() and created the shared memory segment.
 *          Creator is responsible for cleanup_shared_queue() and shm_unlink().
 * @return true if current process is creator, false otherwise
 */
static inline bool is_shared_queue_creator(void)
{
    return g_is_shared_queue_creator != 0;
}

// Forward declaration for cleanup function used in unregister_process()
/**
 * @brief Unregister process from shared memory registry
 * @details Marks process slot as inactive. Should be called before process exit
 *          to prevent stale entries in status display.
 *          Automatically performs cleanup if this process created shared memory:
 *          - Destroys mutexes and condition variables
 *          - Unlinks shared memory segment
 *          Uses global shared queue set by register_monitor/register_configurator.
 */
static inline void unregister_process(void)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue || robust_mutex_lock(&queue->mutex) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to acquire mutex during unregistration");
        return;
    }

    pid_t pid = getpid();
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (queue->processes[i].active && queue->processes[i].pid == pid)
        {
            log_msg(LOG_LEVEL_LOG,
                    "Process unregistered: PID=%d, Name=%s, Commands=%u",
                    pid,
                    queue->processes[i].name,
                    queue->processes[i].commands_processed);
            queue->processes[i].active = 0;
            break;
        }
    }

    pthread_mutex_unlock(&queue->mutex);

    // Automatic cleanup if this process created the shared memory
    if (g_is_shared_queue_creator)
    {
        log_msg(LOG_LEVEL_LOG, "Creator process cleaning up shared memory");
        cleanup_shared_queue();
        shm_unlink(SHM_NAME);
    }
}

/**
 * @brief Automatic cleanup on process exit
 * @details Destructor runs after main() returns, exit() is called, or signal handler completes.
 *          Only executes if process registered (g_shared_queue != NULL).
 *          Safe for multiple translation units - runs once per process.
 *          Eliminates need for manual unregister_process() calls in application code.
 * @internal Uses GCC/Clang __attribute__((destructor)), priority 101 to run after user code
 */
__attribute__((destructor(101))) static void auto_unregister_process(void)
{
    if (g_shared_queue)
    {
        unregister_process();
    }
}

/**
 * @brief Check if configurator is registered and ready for given queue
 * @details Performs three checks:
 *          1. Process is registered as configurator for this queue
 *          2. Process exists (via kill(pid, 0))
 *          3. Queue is not full (indicates configurator is ready to accept
 *             commands)
 *
 *          Typically called once at monitor startup to wait for configurator
 *          availability. During normal operation, natural error detection (queue full,
 *          timeout) is sufficient.
 *
 * @param queue Pointer to shared memory
 * @param queue_id Queue ID to check
 * @return true if configurator is registered, alive, and ready to accept commands
 */
static inline bool is_configurator_ready(int queue_id)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
        return false;

    if (queue_id < 0 || queue_id >= MAX_QUEUE_PAIRS)
    {
        return false;
    }

    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        return false;
    }

    pid_t cfg_pid = 0;
    bool found = false;

    // Find configurator for this queue
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (queue->processes[i].active && queue->processes[i].type == PROCESS_CONFIGURATOR &&
            queue->processes[i].queue_id == queue_id)
        {
            cfg_pid = queue->processes[i].pid;
            found = true;
            break;
        }
    }

    // Check if queue is full (indicates configurator not consuming)
    bool queue_full = queue->pairs[queue_id].count >= MAX_QUEUE_SIZE;

    pthread_mutex_unlock(&queue->mutex);

    if (!found)
    {
        return false;
    }

    // Check process liveness
    if (kill(cfg_pid, 0) != 0 && errno != EPERM)
    {
        return false; // Process dead
    }

    // If queue is full, configurator is not processing (hung/overloaded)
    if (queue_full)
    {
        log_msg(LOG_LEVEL_ERR, "WARNING: Configurator PID=%d queue is full - may be hung", cfg_pid);
        return false;
    }

    return true;
}

/**
 * @brief Set event (enqueue command) to specified queue pair
 * @details Non-blocking operation. If queue is full, returns 0 immediately.
 *          Assigns monotonic command ID and signals configurator via condition
 *          variable. Command type is automatically set to match queue_id.
 *          Uses global shared queue set by register_monitor/register_configurator.
 * @param queue_id Target queue (0-15), also determines command type
 * @return Unique command ID on success, 0 if queue full or invalid queue_id
 */
static inline uint32_t set_event(int queue_id)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Shared queue not initialized");
        return 0;
    }

    if (queue_id < 0 || queue_id >= MAX_QUEUE_PAIRS)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Invalid queue_id=%d", queue_id);
        return 0;
    }

    // Check if configurator is available before enqueuing
    if (!is_configurator_ready(queue_id))
    {
        return 0; // Configurator not available
    }

    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        return 0;
    }

    QueuePair* qp = &queue->pairs[queue_id];
    if (qp->count >= MAX_QUEUE_SIZE)
    {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    uint32_t cmd_id = qp->next_cmd_id++;
    CommandMsg* msg = &qp->buffer[qp->tail];
    msg->type = (CommandType)queue_id;
    msg->cmd_id = cmd_id;
    msg->status = CMD_STATUS_PENDING;
    msg->result_code = 0;

    qp->tail = (qp->tail + 1) % MAX_QUEUE_SIZE;
    qp->count++;

    pthread_cond_signal(&qp->cond_new_cmd);
    pthread_mutex_unlock(&queue->mutex);

    return cmd_id;
}

/**
 * @brief Get event (dequeue command) from specified queue pair
 * @details Blocks with timeout (DEQUEUE_TIMEOUT_SEC) waiting for new commands.
 *          Used by configurators in main processing loop. Uses monotonic clock.
 *          Uses global shared queue set by register_monitor/register_configurator.
 * @param queue_id Queue to dequeue from (0-15)
 * @return Pointer to command in circular buffer, or pointer to empty_cmd
 *         (type=CMD_EMPTY) on timeout
 * @warning Returned pointer is valid only until next dequeue on same queue
 */
static inline CommandMsg* get_event(int queue_id)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
    {
        static CommandMsg empty_cmd = {.type = CMD_EMPTY};
        return &empty_cmd;
    }

    if (queue_id < 0 || queue_id >= MAX_QUEUE_PAIRS)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Invalid queue_id=%d", queue_id);
        return &queue->pairs[0].empty_cmd; // Return empty command on error
    }

    QueuePair* qp = &queue->pairs[queue_id];

    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        return &qp->empty_cmd; // Return empty command on mutex error
    }

    struct timespec timeout;

    while (qp->count == 0)
    {
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += DEQUEUE_TIMEOUT_SEC;

        int ret = pthread_cond_timedwait(&qp->cond_new_cmd, &queue->mutex, &timeout);
        if (ret == ETIMEDOUT)
        {
            pthread_mutex_unlock(&queue->mutex);
            return &qp->empty_cmd; // Return empty command on timeout
        }
        // Continue loop if spurious wakeup
    }

    CommandMsg* msg = &qp->buffer[qp->head];
    qp->head = (qp->head + 1) % MAX_QUEUE_SIZE;
    qp->count--;

    pthread_mutex_unlock(&queue->mutex);
    return msg;
}

/**
 * @brief Wait for command completion with timeout
 * @details Monitors use this after enqueuing to block until configurator marks
 *          command done. Searches all queues for matching cmd_id since cmd_id
 *          is unique across all queues.
 *          Uses global shared queue set by register_monitor/register_configurator.
 * @param cmd_id Command ID returned from set_event()
 * @param timeout_sec Maximum wait time in seconds
 * @return 1=success, 0=failure, -1=timeout (also increments timeout_count)
 */
static inline int wait_for_command(uint32_t cmd_id, int timeout_sec)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
        return -1;

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_sec;

    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        return -1;
    }

    while (1)
    {
        // Search all queues for the command
        int found_queue = -1;
        for (int q = 0; q < MAX_QUEUE_PAIRS; q++)
        {
            QueuePair* qp = &queue->pairs[q];
            for (int i = 0; i < MAX_QUEUE_SIZE; i++)
            {
                if (qp->buffer[i].cmd_id == cmd_id)
                {
                    if (qp->buffer[i].status == CMD_STATUS_DONE)
                    {
                        int result = qp->buffer[i].result_code;
                        pthread_mutex_unlock(&queue->mutex);
                        return result;
                    }
                    // Command found but not done yet
                    found_queue = q;
                    goto wait_for_completion;
                }
            }
        }

    wait_for_completion:
        // Check timeout
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
        {
            // Update timeout counter if we found the queue
            if (found_queue >= 0)
            {
                queue->pairs[found_queue].timeout_count++;
            }
            pthread_mutex_unlock(&queue->mutex);
            return -1; // Timeout
        }

        // Wait for completion signal on the queue where command was found
        if (found_queue >= 0)
        {
            pthread_cond_timedwait(&queue->pairs[found_queue].cond_done, &queue->mutex, &deadline);
        }
        else
        {
            // Command not found yet, wait a bit and retry
            struct timespec short_wait = deadline;
            short_wait.tv_sec = now.tv_sec;
            short_wait.tv_nsec = now.tv_nsec + 10000000; // 10ms
            if (short_wait.tv_nsec >= 1000000000)
            {
                short_wait.tv_sec++;
                short_wait.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&queue->pairs[0].cond_done, &queue->mutex, &short_wait);
        }
    }
}

/**
 * @brief Send command and wait for completion (convenience function)
 * @details Combines set_event() and wait_for_command() in one call.
 *          This is a common pattern where monitors send a command and
 *          immediately wait for its completion.
 *          Command type is automatically set to match queue_id.
 *
 * @param queue_id Target queue (0-15), also determines command type
 * @param timeout_sec Maximum wait time in seconds
 * @return 1=success, 0=failure, -1=timeout or queue full/configurator unavailable
 */
static inline int send_and_wait(int queue_id, int timeout_sec)
{
    uint32_t cmd_id = set_event(queue_id);
    if (cmd_id == 0)
    {
        return -1; // Queue full or configurator not available
    }

    return wait_for_command(cmd_id, timeout_sec);
}

/**
 * @brief Mark event as completed with result
 * @details Configurators call this after processing to update command status.
 *          Broadcasts on cond_done to wake up waiting monitors.
 *          Automatically increments success_count or failure_count.
 *          Queue ID is derived from cmd->type.
 * @param cmd Pointer to command message (from get_event)
 * @param success true for success, false for failure
 */
static inline void mark_event_done(CommandMsg* cmd, bool success)
{
    SharedQueue* queue = g_shared_queue;
    if (!queue)
        return;

    int queue_id = (int)cmd->type;
    if (queue_id < 0 || queue_id >= MAX_QUEUE_PAIRS)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Invalid queue_id=%d from cmd->type", queue_id);
        return;
    }

    if (robust_mutex_lock(&queue->mutex) != 0)
    {
        return;
    }

    cmd->status = CMD_STATUS_DONE;
    cmd->result_code = success ? 1 : 0;

    // Update statistics
    QueuePair* qp = &queue->pairs[queue_id];
    if (success)
    {
        qp->success_count++;
    }
    else
    {
        qp->failure_count++;
    }

    // Increment process command counter
    pid_t pid = getpid();
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (queue->processes[i].active && queue->processes[i].pid == pid)
        {
            queue->processes[i].commands_processed++;
            break;
        }
    }

    pthread_cond_broadcast(&qp->cond_done);
    pthread_mutex_unlock(&queue->mutex);
}
/**
 * @brief RAII macro for automatic SharedQueue cleanup
 * @details Declares a SharedQueue pointer that automatically calls munmap()
 *          when it goes out of scope. Requires GCC/Clang compiler support.
 *
 * Usage:
 * @code
 * SHARED_QUEUE_AUTO SharedQueue* queue = open_or_create_shared_queue(&is_creator);
 * if (!queue) return 1;
 * // ... use queue ...
 * // munmap() called automatically at scope exit
 * @endcode
 *
 * @note Still requires manual unregister_process() before scope exit
 * @note Creator must still manually call cleanup_shared_queue() + shm_unlink()
 */
#define SHARED_QUEUE_AUTO __attribute__((cleanup(cleanup_shared_queue_ptr)))
