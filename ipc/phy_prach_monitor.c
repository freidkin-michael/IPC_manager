/**
 * @file phy_prach_monitor.c
 * @brief Multi-threaded monitor for PHY and PRACH queues
 *
 * @details
 * This monitor handles two independent queue pairs using separate threads:
 * - PRACH thread: queue_id=CMD_PRACH_CONFIG(3)
 * - PHY thread: queue_id=CMD_PHY_CONFIG(2)
 *
 * Each thread independently checks configurator registration before sending
 * commands. Uses global g_queue pointer shared between threads (read-only
 * access after initialization).
 *
 * Architecture:
 * - Threading Model: Two detached worker threads + main thread
 * - Send Intervals: 4 seconds (PRACH), 3 seconds (PHY)
 * - Command Timeout: 5 seconds per request
 * - Readiness Check: Per-thread is_configurator_ready() check at startup
 *
 * Thread Behavior:
 * - PRACH: Monitors configurator_dac_prach PRACH thread (queue 3)
 * - PHY: Monitors configurator_phy (queue 2)
 * - Independent operation: one thread can work while other waits for
 * configurator
 *
 * Graceful Shutdown:
 * - Signal handling sets global stop flag
 * - pthread_cancel for unresponsive threads
 * - Automatic cleanup via pthread_detach
 *
 * Usage:
 * @code
 * ./phy_prach_monitor
 * @endcode
 *
 * @author Michael Freidkin
 * @version 2.0
 */

#define _GNU_SOURCE // Must be before any includes

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
    log_msg(LOG_LEVEL_LOG, "PHY/PRACH Monitor exiting");
}

static void* prach_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PRACH_CONFIG; // PRACH uses its own queue

    while (!stop)
    {
        int result = send_and_wait(queue_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "PHY/PRACH Monitor: PRACH not available");
            sleep(2);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "PHY/PRACH Monitor: sent PRACH command");

        if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "PHY/PRACH Monitor: PRACH failed");
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "PHY/PRACH Monitor: PRACH completed successfully");
        }

        sleep(2);
    }

    return NULL;
}

static void* phy_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PHY_CONFIG; // PHY uses its own queue

    while (!stop)
    {
        int result = send_and_wait(queue_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "PHY/PRACH Monitor: PHY not available");
            sleep(1);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "PHY/PRACH Monitor: sent PHY command");

        if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "PHY/PRACH Monitor: PHY failed");
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "PHY/PRACH Monitor: PHY completed successfully");
        }

        sleep(1);
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    char name[64];
    snprintf(name, sizeof(name), "phy-prach-monitor-%d", getpid());

    // Register with queue_id=0 since this monitor manages multiple queues
    if (!register_monitor(name, 0))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register monitor");
        return 1;
    }

    log_msg(LOG_LEVEL_LOG,
            "PHY/PRACH Monitor started with PRACH (queue %d) and PHY (queue %d) "
            "threads",
            CMD_PRACH_CONFIG,
            CMD_PHY_CONFIG);

    // Start threads
    pthread_t prach_tid, phy_tid;
    if (pthread_create(&prach_tid, NULL, prach_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PRACH thread");
        return 1;
    }

    if (pthread_create(&phy_tid, NULL, phy_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PHY thread");
        stop = true;
        pthread_join(prach_tid, NULL);
        return 1;
    }

    // Wait for threads
    pthread_join(prach_tid, NULL);
    pthread_join(phy_tid, NULL);

    return 0;
}
