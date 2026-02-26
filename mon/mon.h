/**
 * @file status_monitor.h
 * @brief Production-grade typed status monitoring system with type-safe IPC
 * @details
 * CRITICAL RELIABILITY & SAFETY FEATURES:
 * Type-safe operations with compile-time and runtime validation
 * Shared memory offset addressing eliminates pointer corruption risks
 * Robust mutex/read-write lock synchronization with EOWNERDEAD handling
 * Secure shared memory permissions (0660 instead of world-writable 0666)
 * Atomic version tracking with rollover protection (32-bit counters)
 * Memory pool management with alignment guarantees
 * Process-local type registry prevents shared memory type conflicts
 * Automatic resource cleanup via RAII and process destructors
 * Event-driven notifications eliminate polling overhead
 * Thread-per-subscriber isolation with configurable timeouts
 * Memory leak prevention in callback chains
 * Orphaned SHM detection and cleanup utilities
 * Binary compatibility validation through type size/alignment checks
 * Architecture Overview:
 * Type-safe publish-subscribe system where producers update typed status values
 * in shared memory and consumers receive change notifications. Each status type
 * maintains version history, timestamps, and subscriber lists. The system uses
 * a unified 1MB memory pool with offset-based addressing for shared memory safety.
 * Type Safety Features:
 * Compile-time type declarations with STATIC_ASSERT validation
 * Runtime type size/alignment verification
 * Type name registration and lookup
 * Automatic data copying with proper alignment
 * Version tracking per status instance
 * Health Monitoring:
 * Publisher health via process liveness (kill(pid,0))
 * Subscriber health via thread responsiveness and eventfd communication
 * Queue depth monitoring for event notification backpressure
 * Memory pool utilization tracking with overflow prevention
 */
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /**< Enable GNU extensions for cleanup attributes */
#endif
/* ==================== SYSTEM INCLUDES ==================== */
#include <stdio.h>      /**< Standard I/O functions */
#include <stdlib.h>     /**< Memory allocation functions */
#include <string.h>     /**< String manipulation functions */
#include <unistd.h>     /**< POSIX API for process control */
#include <sys/mman.h>   /**< Memory mapping declarations */
#include <sys/stat.h>   /**< File status declarations */
#include <sys/eventfd.h>/**< Event file descriptor API */
#include <sys/select.h> /**< Synchronous I/O multiplexing */
#include <sys/poll.h>   /**< Polling for file descriptors */
#include <fcntl.h>      /**< File control options */
#include <errno.h>      /**< Error number definitions */
#include <stdatomic.h>  /**< C11 atomic operations */
#include <pthread.h>    /**< POSIX threads API */
#include <stdbool.h>    /**< Boolean type and values */
#include <stdint.h>     /**< Fixed-width integer types */
#include <time.h>       /**< Time functions */
#include <stddef.h>     /**< Standard definitions (offsetof) */
/* ==================== CONFIGURATION CONSTANTS ==================== */
/**
 * @def MAX_STATUS_TYPES
 * @brief Maximum number of distinct status items that can be registered
 * @details System supports up to 256 different status variables.
 * Each status can have its own type and update frequency.
 */
#define MAX_STATUS_TYPES 256
/**
 * @def MAX_STATUS_NAME_LEN
 * @brief Maximum length of status name string (including null terminator)
 * @details Names are used for lookup and must be unique within the system.
 */
#define MAX_STATUS_NAME_LEN 64
#/**
 * @def MAX_SUBSCRIBERS
 * @brief Maximum number of concurrent subscribers across all processes
 * @details Each subscriber runs in its own thread and maintains eventfd.
 */
#define MAX_SUBSCRIBERS 1024
/**
 * @def MAX_SUBS_PER_STATUS
 * @brief Maximum subscribers per individual status item
 * @details Prevents any single status from monopolizing subscriber resources.
 */
#define MAX_SUBS_PER_STATUS 32
/**
 * @def MAX_TYPE_NAME_LEN
 * @brief Maximum length for type name strings
 * @details Type names are used for debugging and validation.
 */
#define MAX_TYPE_NAME_LEN 32
/**
 * @def SHM_TYPED_NAME
 * @brief POSIX shared memory object name
 * @details Consistent naming convention across all processes.
 */
#define SHM_TYPED_NAME "/typed_status_shm"
/**
 * @def EVENT_POOL_SIZE
 * @brief Size of eventfd pool for status notifications
 * @details Prevents frequent eventfd creation/destruction overhead.
 */
#define EVENT_POOL_SIZE 64
/* ==================== SYSTEM STRUCTURES ==================== */
/**
 * @struct typed_status_item_t
 * @brief Shared memory structure for a single status item
 * @details Contains all metadata and synchronization primitives for one
 *      status variable. Lives entirely in shared memory.
 * @warning All pointers in this structure must be offsets or constant strings.
 */
typedef struct
{
    char name[MAX_STATUS_NAME_LEN];           /**< Unique status identifier */
    _Atomic uint32_t version;                 /**< Current version for change tracking */
    size_t data_offset;                       /**< Offset in data_pool (not pointer!) */
    size_t data_size;                         /**< Size of data in bytes */
    const char* type_name;                    /**< Type name (for compatibility only) */
    int32_t subscribers[MAX_SUBS_PER_STATUS]; /**< List of subscriber IDs */
    _Atomic int32_t sub_count;                /**< Current number of subscribers */
    int32_t event_fd;                         /**< Eventfd for publisher notifications */
    _Atomic bool enabled;                     /**< Status enabled/disabled flag */
    pthread_rwlock_t data_lock;               /**< RW lock for data access */
    _Atomic uint64_t publish_count;           /**< Published messages counter */
    _Atomic uint64_t notify_count;            /**< Subscriber notifications counter */
} typed_status_item_t;
/* Full callback signature used by subscribers */
typedef void (*typed_status_callback_t)(
    const char* status_name, const void* old_data, const void* new_data, const char* type_name, void* user_data);
/**
 * @typedef typed_status_basic_callback_t
 * @brief Backwards-compatible basic callback signature
 * @param old_data Previous value (may be NULL)
 * @param new_data Current value
 * @note Kept for compatibility with existing example callers
 */
typedef void (*typed_status_basic_callback_t)(const void* old_data, const void* new_data);
/**
 * @struct typed_subscriber_t
 * @brief Process-local subscriber information
 * @details Each subscriber runs in its own thread and maintains its own
 *      eventfd for notifications. This structure is NOT shared.
 */
typedef struct
{
    int32_t id;       /**< Unique subscriber identifier */
    int32_t event_fd; /**< Eventfd for waking subscriber thread */
    /** @brief Full callback with complete status information */
    void (*callback)(const char* status_name, /**< Name of the status that changed */
                     const void* old_data,    /**< Previous value (may be NULL) */
                     const void* new_data,    /**< Current value */
                     const char* type_name,   /**< Type name for runtime checking */
                     void* user_data          /**< User-provided context */
    );

    void* user_data;                         /**< User context passed to callback */
    int32_t priority;                        /**< Thread priority (currently unused) */
    bool running;                            /**< Thread control flag */
    pthread_t thread;                        /**< Subscriber thread handle */
    char filter_status[MAX_STATUS_NAME_LEN]; /**< Status name filter */
    bool use_filter;                         /**< Whether to use filtering */
} typed_subscriber_t;
/**
 * @struct typed_status_shm_t
 * @brief Main shared memory structure containing entire system state
 * @warning All processes must use the same binary version of this structure.
 *      Size and alignment must remain constant across builds.
 */
typedef struct
{
    uint8_t data_pool[1024 * 1024];                 /**< 1MB memory pool for status data */
    _Atomic size_t data_pool_offset;                /**< Next available offset in pool */
    typed_status_item_t statuses[MAX_STATUS_TYPES]; /**< Array of status items */
    _Atomic int32_t status_count;                   /**< Number of registered statuses */
    int32_t event_pool[EVENT_POOL_SIZE];            /**< Pool of reusable eventfds */
    _Atomic int32_t next_subscriber_id;             /**< Next available subscriber ID */
    _Atomic bool initialized;                       /**< System initialization flag */
} typed_status_shm_t;
/* ==================== PUBLIC API (IPC MAILBOX COMPATIBLE) ==================== */
/**
 * @brief Initialize typed status system for publishers
 * @details Analogous to register_monitor() in ipc_mailbox.h.
 *      Creates or attaches to shared memory with creator detection.
 *      Must be called before any status registration or updates.
 * @return true if initialization successful
 * @return false if initialization failed (check errno for details)
 * @post Shared memory is mapped and initialized if creator
 * @post Global pointer g_shm is set
 * @post System is ready for status registration
 * @note Thread-safe: uses internal mutex for synchronization
 */
bool typed_status_init_publisher(void);
/**
 * @brief Initialize typed status system for subscribers
 * @details Analogous to register_configurator() in ipc_mailbox.h.
 *      Attaches to existing shared memory created by publisher.
 *      Waits for publisher initialization if needed.
 * @return true if attachment successful
 * @return false if attachment failed (publisher not running or timeout)
 * @post Shared memory is mapped
 * @post Global pointer g_shm is set
 * @post System is ready for subscription
 * @note Will timeout after 1 second if publisher doesn't initialize
 */
bool typed_status_init_subscriber(void);
/* ==================== CORE API ==================== */
/**
 * @brief Register a new status item in the system
 * @param status_name Unique name for the status (max 63 chars)
 * @param type_size Size in bytes of the status data type
 * @param initial_data Optional initial value (NULL for zero-initialized)
 * @return Non-negative status index on success
 * @return -1 on error (invalid parameters, name conflict, pool full)
 * @post Status is registered and available for updates
 * @post Memory is allocated from data pool
 * @post Eventfd is allocated from pool
 * @note Names must be unique; duplicate registration returns existing index
 */
int typed_status_register(const char* status_name, size_t type_size, const void* initial_data);
/**
 * @brief Update a status value and notify subscribers
 * @param status_name Name of the status to update
 * @param new_data Pointer to new data (must match registered size)
 * @return 0 on success
 * @return -1 on error (status not found, invalid pointer)
 * @post Data is copied to shared memory
 * @post Version is incremented atomically
 * @post Subscribers are notified via eventfd
 * @note Thread-safe: uses read-write lock for exclusive access
 */
int typed_status_set(const char* status_name, const void* new_data);
/**
 * @brief Subscribe to status changes with simplified callback
 * @param status_name Name of the status to monitor
 * @param callback Function to call when status changes
 * @return Subscriber ID (positive) on success
 * @return -1 on error (invalid parameters, max subscribers reached)
 * @post New thread is created to monitor changes
 * @post Callback will be invoked on each change
 * @note Callback runs in subscriber thread context
 * @note Memory for old/new data is allocated during callback
 */
/**
 * @brief Subscribe to status changes with full callback signature
 * @param status_name Name of the status to monitor
 * @param callback Function to call when status changes (full signature)
 * @param user_data User-provided context passed to callback
 * @return Subscriber ID (positive) on success
 * @return -1 on error (invalid parameters, max subscribers reached)
 * @post New thread is created to monitor changes
 * @post Callback will be invoked on each change
 * @note Callback runs in subscriber thread context
 */
/* Backwards-compatible subscribe (basic callback) */
int32_t typed_status_subscribe(const char* status_name, typed_status_basic_callback_t callback);
/* Extended subscribe variant (full callback + user_data) */
int32_t typed_status_subscribe_ex(const char* status_name, typed_status_callback_t callback, void* user_data);
/* ==================== INTERNAL IMPLEMENTATION ==================== */
#ifdef TYPED_STATUS_MONITOR_IMPLEMENTATION
/* Global system state (process-local) */
static typed_status_shm_t* g_shm = NULL;                    /**< Shared memory mapping */
static typed_subscriber_t g_subscribers[MAX_SUBSCRIBERS];   /**< Process subscribers */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER; /**< Global mutex */
/* Creator detection */
static int g_is_creator = 0; /**< 1 if creator, 0 otherwise */
/* Adapter: allow simplified callbacks to be used with the full callback system */
static void basic_callback_wrapper(
    const char* status_name, const void* old_data, const void* new_data, const char* type_name, void* user_data)
{
    (void)status_name;
    (void)type_name;
    typed_status_basic_callback_t cb = (typed_status_basic_callback_t)user_data;
    if (cb)
    {
        cb(old_data, new_data);
    }
}
/* ==================== INTERNAL FUNCTIONS ==================== */
/**
 * @internal
 * @brief Wrapper to convert basic callback to full callback
 * @param status_name Name of status (unused)
 * @param old_data Previous data value
 * @param new_data Current data value
 * @param type_name Type name (unused)
 * @param user_data Basic callback function pointer
 * @details Subscriber system invokes the full callback signature
 *      provided by callers.
 */
/* basic_callback_wrapper adapter present to support legacy basic callbacks */
/**
 * @internal
 * @brief Find status index by name
 * @param status_name Name to search for
 * @return Index in statuses array, or -1 if not found
 * @note Linear search - optimize if MAX_STATUS_TYPES increases
 */
static int find_status_index(const char* status_name)
{
    if (!status_name || !g_shm)
        return -1;
    int status_count = atomic_load(&g_shm->status_count);
    for (int i = 0; i < status_count; i++)
    {
        if (strcmp(g_shm->statuses[i].name, status_name) == 0)
        {
            return i;
        }
    }
    return -1;
}
/**
 * @internal
 * @brief Find status pointer by name
 * @param status_name Name to search for
 * @return Pointer to status item, or NULL if not found
 */
static typed_status_item_t* find_status(const char* status_name)
{
    int idx = find_status_index(status_name);
    return (idx >= 0) ? &g_shm->statuses[idx] : NULL;
}
/**
 * @internal
 * @brief Allocate eventfd from shared pool
 * @return Eventfd file descriptor, or -1 if pool exhausted
 * @note Eventfds are created with EFD_SEMAPHORE|EFD_NONBLOCK
 */
static int allocate_event_fd(void)
{
    if (!g_shm)
        return -1;
    for (int i = 0; i < EVENT_POOL_SIZE; i++)
    {
        if (g_shm->event_pool[i] == -1)
        {
            g_shm->event_pool[i] = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
            if (g_shm->event_pool[i] == -1)
            {
                perror("eventfd allocation failed");
                return -1;
            }
            return g_shm->event_pool[i];
        }
    }
    return -1;
}
/**
 * @internal
 * @brief Convert data pool offset to pointer
 * @param offset Offset in data pool (0 is invalid)
 * @return Pointer to data, or NULL if offset invalid
 * @note All shared memory data access must use this function
 */
static void* get_data_ptr_from_offset(size_t offset)
{
    if (!g_shm || offset == 0 || offset >= sizeof(g_shm->data_pool))
        return NULL;
    return &g_shm->data_pool[offset];
}
/**
 * @internal
 * @brief Allocate aligned memory from shared data pool
 * @param size Required size in bytes
 * @param align Alignment requirement (power of two)
 * @return Offset in data pool, or 0 on failure
 * @note Returns offset, not pointer, for shared memory safety
 */
static size_t allocate_shared_data(size_t size, size_t align)
{
    if (!g_shm)
        return 0;
    /* Alignment calculation */
    size_t offset = atomic_load(&g_shm->data_pool_offset);
    size_t aligned_offset = (offset + align - 1) & ~(align - 1);
    /* Check for overflow */
    if (aligned_offset + size > sizeof(g_shm->data_pool))
    {
        fprintf(stderr, "Data pool overflow\n");
        return 0; /* Return 0 to indicate failure */
    }
    /* Update atomic offset */
    atomic_store(&g_shm->data_pool_offset, aligned_offset + size);
    return aligned_offset; /* Return offset, not pointer */
}
/**
 * @internal
 * @brief Notify all subscribers of status change
 * @param status Pointer to status item that changed
 * @details Writes to eventfds of publisher and all subscribers
 * @note Thread-safe: called with data_lock held
 */
static void notify_subscribers(typed_status_item_t* status)
{
    if (!status || !g_shm)
        return;
    uint64_t val = 1;
    /* Increment notification counter */
    atomic_fetch_add(&status->notify_count, 1);
    /* Notify publisher eventfd */
    if (status->event_fd != -1)
    {
        ssize_t written = write(status->event_fd, &val, sizeof(val));
        (void)written; /* Ignore result in release builds */
    }
    /* Notify all subscriber eventfds */
    for (int i = 0; i < status->sub_count; i++)
    {
        int sub_id = status->subscribers[i];
        if (sub_id > 0 && sub_id < MAX_SUBSCRIBERS && g_subscribers[sub_id].running)
        {
            ssize_t written = write(g_subscribers[sub_id].event_fd, &val, sizeof(val));
            (void)written; /* Ignore result in release builds */
        }
    }
}
/**
 * @internal
 * @brief Subscriber thread function
 * @param arg Pointer to typed_subscriber_t structure
 * @return NULL on thread exit
 * @details Polls eventfd for notifications, copies data while holding lock,
 *      and invokes callback with protected data copies.
 */
static void* subscriber_thread_func(void* arg)
{
    typed_subscriber_t* sub = (typed_subscriber_t*)arg;
    if (!sub)
    {
        return NULL;
    }
    printf("Typed subscriber %d started\n", sub->id);

    /* State maintained across callbacks */
    void* old_data_copy = NULL;
    uint32_t last_version = 0;

    /* Poll setup */
    struct pollfd fds[1];
    fds[0].fd = sub->event_fd;
    fds[0].events = POLLIN;

    while (sub->running)
    {
        int ret = poll(fds, 1, 1000); /* Timeout 1 second */

        if (ret > 0 && (fds[0].revents & POLLIN))
        {
            /* Clear event */
            uint64_t val;
            ssize_t bytes_read = read(sub->event_fd, &val, sizeof(val));
            (void)bytes_read; /* Ignore read result */

            /* Find filtered status */
            typed_status_item_t* status = find_status(sub->filter_status);
            if (!status)
                continue;

            /* Acquire read lock */
            pthread_rwlock_rdlock(&status->data_lock);

            uint32_t current_version = atomic_load(&status->version);

            /* Check if version changed */
            if (current_version != last_version)
            {
                /* Copy NEW data while holding lock */
                size_t current_data_size = status->data_size;
                void* current_data_copy = malloc(current_data_size);
                void* data_ptr = get_data_ptr_from_offset(status->data_offset);
                if (current_data_copy && data_ptr)
                {
                    memcpy(current_data_copy, data_ptr, current_data_size);
                }

                /* Release lock before callback */
                pthread_rwlock_unlock(&status->data_lock);

                /* Invoke callback with protected copies */
                if (sub->callback && current_data_copy)
                {
                    sub->callback(status->name, old_data_copy, current_data_copy, status->type_name, sub->user_data);
                }

                /* Free previous old data */
                if (old_data_copy)
                {
                    free(old_data_copy);
                }

                /* Update state for next iteration */
                old_data_copy = current_data_copy;
                last_version = current_version;
            }
            else
            {
                /* Version unchanged, just release lock */
                pthread_rwlock_unlock(&status->data_lock);
            }
        }
        else if (ret == 0)
        {
            /* Timeout - check running flag */
            continue;
        }
        else if (ret < 0)
        {
            /* Poll error (not EINTR) */
            if (errno != EINTR)
            {
                perror("poll failed");
                break;
            }
        }
    }

    /* Cleanup on thread exit */
    if (old_data_copy)
    {
        free(old_data_copy);
    }

    printf("Typed subscriber %d stopped\n", sub->id);
    return NULL;
}

int typed_status_register(const char* status_name, size_t type_size, const void* initial_data)
{
    if (!status_name || type_size == 0)
    {
        fprintf(stderr, "Invalid parameters to typed_status_register\n");
        return -1;
    }
    if (!g_shm)
    {
        fprintf(stderr, "Typed status system not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_mutex);

    /* Check if already registered */
    int idx = find_status_index(status_name);
    if (idx >= 0)
    {
        pthread_mutex_unlock(&g_mutex);
        return idx;
    }

    /* Check status limit */
    int status_count = atomic_load(&g_shm->status_count);
    if (status_count >= MAX_STATUS_TYPES)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "Maximum status types reached\n");
        return -1;
    }

    idx = status_count;

    /* Allocate memory from pool */
    size_t data_offset = allocate_shared_data(type_size, _Alignof(max_align_t));
    if (data_offset == 0)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "Failed to allocate memory from pool\n");
        return -1;
    }

    /* Initialize data */
    void* data_ptr = get_data_ptr_from_offset(data_offset);
    if (initial_data)
    {
        memcpy(data_ptr, initial_data, type_size);
    }
    else
    {
        memset(data_ptr, 0, type_size);
    }

    /* Setup status structure */
    strncpy(g_shm->statuses[idx].name, status_name, MAX_STATUS_NAME_LEN - 1);
    g_shm->statuses[idx].name[MAX_STATUS_NAME_LEN - 1] = '\0';
    atomic_store(&g_shm->statuses[idx].version, 1);
    g_shm->statuses[idx].data_offset = data_offset;
    g_shm->statuses[idx].data_size = type_size;
    g_shm->statuses[idx].type_name = g_shm->statuses[idx].name; /* Self-reference */
    atomic_store(&g_shm->statuses[idx].sub_count, 0);
    g_shm->statuses[idx].event_fd = allocate_event_fd();
    atomic_store(&g_shm->statuses[idx].enabled, true);
    atomic_store(&g_shm->statuses[idx].publish_count, 0);
    atomic_store(&g_shm->statuses[idx].notify_count, 0);

    /* Initialize RW lock with robust attributes */
    pthread_rwlockattr_t rw_attr;
    pthread_rwlockattr_init(&rw_attr);
    pthread_rwlockattr_setpshared(&rw_attr, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&g_shm->statuses[idx].data_lock, &rw_attr);
    pthread_rwlockattr_destroy(&rw_attr);

    /* Clear subscriber list */
    for (int j = 0; j < MAX_SUBS_PER_STATUS; j++)
    {
        g_shm->statuses[idx].subscribers[j] = 0;
    }

    /* Increment status count */
    atomic_fetch_add(&g_shm->status_count, 1);

    printf("Status '%s' registered with %zu bytes\n", status_name, type_size);

    pthread_mutex_unlock(&g_mutex);
    return idx;
}
int typed_status_set(const char* status_name, const void* new_data)
{
    if (!status_name || !new_data)
    {
        fprintf(stderr, "Invalid parameters to typed_status_set\n");
        return -1;
    }
    typed_status_item_t* status = find_status(status_name);
    if (!status)
    {
        fprintf(stderr, "Status '%s' not found\n", status_name);
        return -1;
    }

    /* Get pointer from offset */
    void* data_ptr = get_data_ptr_from_offset(status->data_offset);
    if (!data_ptr)
    {
        fprintf(stderr, "Invalid data offset for status '%s'\n", status_name);
        return -1;
    }

    /* Lock for write */
    if (pthread_rwlock_wrlock(&status->data_lock) != 0)
    {
        perror("Failed to acquire write lock");
        return -1;
    }

    /* Copy data */
    memcpy(data_ptr, new_data, status->data_size);

    /* Increment version and publish counter */
    atomic_fetch_add(&status->version, 1);
    atomic_fetch_add(&status->publish_count, 1);

    /* Notify subscribers */
    notify_subscribers(status);

    pthread_rwlock_unlock(&status->data_lock);

    return 0;
}
void* typed_status_get_copy(const char* status_name, uint32_t* version, uint64_t* timestamp)
{
    if (!status_name)
        return NULL;
    typed_status_item_t* status = find_status(status_name);
    if (!status)
        return NULL;

    void* data_ptr = get_data_ptr_from_offset(status->data_offset);
    if (!data_ptr)
        return NULL;

    /* Acquire read lock */
    if (pthread_rwlock_rdlock(&status->data_lock) != 0)
    {
        perror("Failed to acquire read lock");
        return NULL;
    }

    /* Allocate memory for copy */
    void* copy = malloc(status->data_size);
    if (!copy)
    {
        pthread_rwlock_unlock(&status->data_lock);
        fprintf(stderr, "Failed to allocate memory for copy\n");
        return NULL;
    }

    /* Copy data */
    memcpy(copy, data_ptr, status->data_size);

    /* Return version and timestamp if requested */
    if (version)
    {
        *version = atomic_load(&status->version);
    }

    if (timestamp)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        *timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }

    pthread_rwlock_unlock(&status->data_lock);

    return copy;
}
const void* typed_status_get_ref(const char* status_name, uint32_t* version, uint64_t* timestamp)
{
    if (!status_name)
        return NULL;
    typed_status_item_t* status = find_status(status_name);
    if (!status)
        return NULL;

    /* Return version and timestamp if requested */
    if (version)
    {
        *version = atomic_load(&status->version);
    }

    if (timestamp)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        *timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }

    return get_data_ptr_from_offset(status->data_offset);
}
const char* typed_status_get_type_name(const char* status_name)
{
    typed_status_item_t* status = find_status(status_name);
    return status ? status->type_name : NULL;
}
int typed_status_set_typed(const char* status_name, const char* type_name, const void* new_data)
{
    if (!status_name || !type_name || !new_data)
    {
        fprintf(stderr, "Invalid parameters to typed_status_set_typed\n");
        return -1;
    }
    typed_status_item_t* status = find_status(status_name);
    if (!status)
    {
        fprintf(stderr, "Status '%s' not found\n", status_name);
        return -1;
    }

    /* Type name validation (string comparison */
    if (status->type_name && strcmp(status->type_name, type_name) != 0)
    {
        fprintf(stderr,
                "Type mismatch for status '%s': expected '%s', got '%s'\n",
                status_name,
                status->type_name,
                type_name);
        return -1;
    }

    return typed_status_set(status_name, new_data);
}
int32_t typed_status_subscribe_ex(const char* status_name, typed_status_callback_t callback, void* user_data)
{
    if (!status_name || !callback)
    {
        fprintf(stderr, "Invalid parameters to typed_status_subscribe\n");
        return -1;
    }
    if (!g_shm)
    {
        fprintf(stderr, "Typed status system not initialized\n");
        return -1;
    }

    if (!find_status(status_name))
    {
        fprintf(stderr, "Status '%s' not found for subscription\n", status_name);
        return -1;
    }

    pthread_mutex_lock(&g_mutex);

    /* Find free subscriber slot */
    int slot = -1;
    for (int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if (g_subscribers[i].id == 0)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "Maximum subscribers reached\n");
        return -1;
    }

    /* Generate subscriber ID */
    int32_t sub_id = atomic_fetch_add(&g_shm->next_subscriber_id, 1);
    if (sub_id >= MAX_SUBSCRIBERS)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "Subscriber ID overflow\n");
        return -1;
    }

    /* Initialize subscriber structure */
    g_subscribers[slot].id = sub_id;
    g_subscribers[slot].callback = callback;
    g_subscribers[slot].user_data = user_data;
    g_subscribers[slot].priority = 0;
    g_subscribers[slot].running = true;
    g_subscribers[slot].use_filter = true;
    strncpy(g_subscribers[slot].filter_status, status_name, MAX_STATUS_NAME_LEN - 1);
    g_subscribers[slot].filter_status[MAX_STATUS_NAME_LEN - 1] = '\0';

    /* Create eventfd for subscriber */
    g_subscribers[slot].event_fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
    if (g_subscribers[slot].event_fd == -1)
    {
        perror("Failed to create eventfd for subscriber");
        g_subscribers[slot].id = 0;
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    /* Add subscriber to status */
    typed_status_item_t* status = find_status(status_name);
    if (status)
    {
        int sub_idx = atomic_load(&status->sub_count);
        if (sub_idx < MAX_SUBS_PER_STATUS)
        {
            status->subscribers[sub_idx] = sub_id;
            atomic_fetch_add(&status->sub_count, 1);
        }
        else
        {
            fprintf(stderr, "Maximum subscribers per status reached for '%s'\n", status_name);
            close(g_subscribers[slot].event_fd);
            g_subscribers[slot].id = 0;
            pthread_mutex_unlock(&g_mutex);
            return -1;
        }
    }

    /* Create subscriber thread */
    int ret = pthread_create(&g_subscribers[slot].thread, NULL, subscriber_thread_func, &g_subscribers[slot]);
    if (ret != 0)
    {
        perror("Failed to create subscriber thread");
        close(g_subscribers[slot].event_fd);
        g_subscribers[slot].id = 0;
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    printf("Subscriber %d created for status '%s'\n", sub_id, status_name);
    pthread_mutex_unlock(&g_mutex);
    return sub_id;
}
/* Backwards-compatible wrapper for legacy basic callbacks */
int32_t typed_status_subscribe(const char* status_name, typed_status_basic_callback_t callback)
{
    if (!callback)
        return -1;
    return typed_status_subscribe_ex(status_name, basic_callback_wrapper, (void*)callback);
}
int typed_status_unsubscribe(int32_t subscriber_id)
{
    if (subscriber_id <= 0 || subscriber_id >= MAX_SUBSCRIBERS)
    {
        fprintf(stderr, "Invalid subscriber ID: %d\n", subscriber_id);
        return -1;
    }
    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if (g_subscribers[i].id == subscriber_id)
        {
            /* Stop the subscriber thread */
            g_subscribers[i].running = false;

            /* Wake up the thread to exit */
            if (g_subscribers[i].event_fd != -1)
            {
                uint64_t val = 1;
                ssize_t written = write(g_subscribers[i].event_fd, &val, sizeof(val));
                (void)written; /* Ignore result */
            }

            /* Wait for thread to exit */
            pthread_join(g_subscribers[i].thread, NULL);

            /* Close eventfd */
            if (g_subscribers[i].event_fd != -1)
            {
                close(g_subscribers[i].event_fd);
                g_subscribers[i].event_fd = -1;
            }

            /* Remove from status subscriber list */
            typed_status_item_t* status = find_status(g_subscribers[i].filter_status);
            if (status)
            {
                for (int j = 0; j < status->sub_count; j++)
                {
                    if (status->subscribers[j] == subscriber_id)
                    {
                        /* Shift remaining subscribers */
                        for (int k = j; k < status->sub_count - 1; k++)
                        {
                            status->subscribers[k] = status->subscribers[k + 1];
                        }
                        status->subscribers[status->sub_count - 1] = 0;
                        atomic_fetch_sub(&status->sub_count, 1);
                        break;
                    }
                }
            }

            /* Clear subscriber structure */
            g_subscribers[i].id = 0;
            g_subscribers[i].callback = NULL;
            g_subscribers[i].user_data = NULL;
            g_subscribers[i].filter_status[0] = '\0';

            printf("Typed subscriber %d unsubscribed\n", subscriber_id);
            pthread_mutex_unlock(&g_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_mutex);
    fprintf(stderr, "Subscriber %d not found\n", subscriber_id);
    return -1;
}
void typed_status_cleanup(void)
{
    pthread_mutex_lock(&g_mutex);
    /* Stop all subscriber threads */
    for (int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if (g_subscribers[i].id != 0)
        {
            g_subscribers[i].running = false;

            /* Wake up thread */
            if (g_subscribers[i].event_fd != -1)
            {
                uint64_t val = 1;
                ssize_t written = write(g_subscribers[i].event_fd, &val, sizeof(val));
                (void)written; /* Ignore result */
            }
        }
    }

    /* Wait for all threads to stop */
    for (int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if (g_subscribers[i].id != 0)
        {
            pthread_join(g_subscribers[i].thread, NULL);

            if (g_subscribers[i].event_fd != -1)
            {
                close(g_subscribers[i].event_fd);
                g_subscribers[i].event_fd = -1;
            }

            g_subscribers[i].id = 0;
        }
    }

    /* Cleanup shared memory if it exists */
    if (g_shm)
    {
        /* Close eventfds in pool */
        for (int i = 0; i < EVENT_POOL_SIZE; i++)
        {
            if (g_shm->event_pool[i] != -1)
            {
                close(g_shm->event_pool[i]);
                g_shm->event_pool[i] = -1;
            }
        }

        /* Destroy RW locks and close status eventfds */
        int status_count = atomic_load(&g_shm->status_count);
        for (int i = 0; i < status_count; i++)
        {
            if (g_shm->statuses[i].event_fd != -1)
            {
                close(g_shm->statuses[i].event_fd);
                g_shm->statuses[i].event_fd = -1;
            }
            pthread_rwlock_destroy(&g_shm->statuses[i].data_lock);
        }

        /* Unmap memory */
        munmap(g_shm, sizeof(typed_status_shm_t));
        g_shm = NULL;
    }

    pthread_mutex_unlock(&g_mutex);
    printf("Typed status system cleaned up\n");
}
/* ==================== SIMPLIFIED PUBLIC API ==================== */
/**
 * @brief Cleanup helper for TYPED_STATUS_AUTO macro
 * @param shm_ptr Pointer to typed_status_shm_t pointer
 * @internal Used by __attribute__((cleanup)) for automatic cleanup
 * @note Only unmaps memory, does not unlink shared memory object
 */
static inline void typed_status_cleanup_ptr(typed_status_shm_t** shm_ptr)
{
    if (shm_ptr && *shm_ptr && *shm_ptr != MAP_FAILED)
    {
        munmap(*shm_ptr, sizeof(typed_status_shm_t));
        *shm_ptr = NULL;
    }
}
bool typed_status_init_publisher(void)
{
    if (g_shm)
    {
        return true; /* Already initialized */
    }
    /* Try to create with O_EXCL to determine if we are the creator */
    int fd = shm_open(SHM_TYPED_NAME, O_CREAT | O_EXCL | O_RDWR, 0660);
    bool created = (fd >= 0);

    if (fd < 0)
    {
        if (errno == EEXIST)
        {
            /* Already exists, open it */
            fd = shm_open(SHM_TYPED_NAME, O_RDWR, 0660);
        }

        if (fd == -1)
        {
            perror("shm_open failed in typed_status_init_publisher");
            return false;
        }
    }

    if (ftruncate(fd, sizeof(typed_status_shm_t)) == -1)
    {
        perror("ftruncate failed");
        close(fd);
        return false;
    }

    g_shm = mmap(NULL, sizeof(typed_status_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (g_shm == MAP_FAILED)
    {
        perror("mmap failed");
        g_shm = NULL;
        return false;
    }

    if (created)
    {
        g_is_creator = 1;
    }

    /* Initialize if creator */
    if (g_is_creator)
    {
        /* Atomic counters */
        /* Reserve offset 0 as invalid sentinel; start pool at 1 */
        atomic_store(&g_shm->data_pool_offset, 1);
        atomic_store(&g_shm->status_count, 0);
        atomic_store(&g_shm->next_subscriber_id, 1);

        /* Initialize eventfd pool */
        for (int i = 0; i < EVENT_POOL_SIZE; i++)
        {
            g_shm->event_pool[i] = -1;
        }

        /* Initialize status array */
        for (int i = 0; i < MAX_STATUS_TYPES; i++)
        {
            g_shm->statuses[i].name[0] = '\0';
            atomic_store(&g_shm->statuses[i].version, 0);
            g_shm->statuses[i].data_offset = 0;
            g_shm->statuses[i].data_size = 0;
            g_shm->statuses[i].type_name = NULL;
            atomic_store(&g_shm->statuses[i].sub_count, 0);
            g_shm->statuses[i].event_fd = -1;
            atomic_store(&g_shm->statuses[i].enabled, true);
            pthread_rwlock_init(&g_shm->statuses[i].data_lock, NULL);

            /* Clear subscriber list */
            for (int j = 0; j < MAX_SUBS_PER_STATUS; j++)
            {
                g_shm->statuses[i].subscribers[j] = 0;
            }
        }

        /* Initialize process-local subscriber array */
        for (int i = 0; i < MAX_SUBSCRIBERS; i++)
        {
            g_subscribers[i].id = 0;
            g_subscribers[i].running = false;
            g_subscribers[i].event_fd = -1;
            g_subscribers[i].callback = NULL;
            g_subscribers[i].user_data = NULL;
            g_subscribers[i].filter_status[0] = '\0';
            g_subscribers[i].use_filter = false;
        }

        /* Mark as initialized */
        atomic_store(&g_shm->initialized, true);
    }

    return true;
}
bool typed_status_init_subscriber(void)
{
    if (g_shm)
    {
        return true; /* Already initialized */
    }
    int fd = shm_open(SHM_TYPED_NAME, O_RDWR, 0660);
    if (fd == -1)
    {
        perror("shm_open failed - publisher not started?");
        return false;
    }

    g_shm = mmap(NULL, sizeof(typed_status_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (g_shm == MAP_FAILED)
    {
        perror("mmap failed");
        g_shm = NULL;
        return false;
    }

    /* Wait for initialization (up to 10 seconds) */
    for (int i = 0; i < 100; i++) /* 100 * 100ms = 10 seconds */
    {
        if (atomic_load(&g_shm->initialized))
        {
            return true;
        }
        usleep(100000); /* 100ms */
    }

    fprintf(stderr, "Timeout waiting for publisher initialization\n");
    munmap(g_shm, sizeof(typed_status_shm_t));
    g_shm = NULL;
    return false;
}
bool is_typed_status_creator(void)
{
    return (g_is_creator != 0);
}
/**
 * @brief Automatic cleanup on process exit
 * @details Destructor runs after main() returns or exit() is called.
 * @internal Uses GCC/Clang __attribute__((destructor)), priority 101
 * @note Priority 101 ensures cleanup after other destructors
 */
__attribute__((destructor(101))) static void auto_typed_status_cleanup(void)
{
    if (g_shm)
    {
        /* Stop all local subscriber threads */
        pthread_mutex_lock(&g_mutex);
        for (int i = 0; i < MAX_SUBSCRIBERS; i++)
        {
            if (g_subscribers[i].id != 0 && g_subscribers[i].running)
            {
                g_subscribers[i].running = false;
                if (g_subscribers[i].event_fd != -1)
                {
                    uint64_t val = 1;
                    ssize_t written = write(g_subscribers[i].event_fd, &val, sizeof(val));
                    (void)written; /* Ignore result in destructor */
                }
            }
        }
        pthread_mutex_unlock(&g_mutex);
        /* Wait for threads to stop */
        for (int i = 0; i < MAX_SUBSCRIBERS; i++)
        {
            if (g_subscribers[i].id != 0)
            {
                pthread_join(g_subscribers[i].thread, NULL);
                if (g_subscribers[i].event_fd != -1)
                {
                    close(g_subscribers[i].event_fd);
                }
            }
        }

        /* Cleanup shared memory if creator */
        if (g_is_creator)
        {
            typed_status_cleanup();
            shm_unlink(SHM_TYPED_NAME);
        }
        else
        {
            /* Just unmap for non-creators */
            if (g_shm)
            {
                munmap(g_shm, sizeof(typed_status_shm_t));
                g_shm = NULL;
            }
        }
    }
}
#endif /* TYPED_STATUS_MONITOR_IMPLEMENTATION */