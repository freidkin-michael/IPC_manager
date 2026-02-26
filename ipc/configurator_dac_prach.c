/**
 * @file configurator_dac_prach.c
 * @brief Multi-threaded configurator for DAC and PRACH
 *
 * @details
 * This configurator handles two queue pairs simultaneously using separate
 * threads:
 * - DAC thread: queue_id=CMD_DAC_CONFIG(1)
 * - PRACH thread: queue_id=CMD_PRACH_CONFIG(3)
 *
 * Each thread registers independently in the process registry with distinct
 * names, enabling monitors to track both configurators.
 *
 * Architecture:
 * - Threading Model: Two worker threads + main thread
 * - Global State: Uses g_shared_queue from ipc_mailbox.h
 * - Configuration Functions: dac_config() and prach_config()
 * - Graceful Shutdown: SIGTERM/SIGINT with 1-second thread wakeup
 * - Automatic Cleanup: Library destructor handles unregistration
 *
 * Thread Registration:
 * - DAC: "dac-configurator-PID" on queue 1
 * - PRACH: "prach-configurator-PID" on queue 3
 *
 * Usage:
 * @code
 * ./configurator_dac_prach
 * @endcode
 *
 * @note Process unregistration and cleanup are fully automatic via
 *       __attribute__((destructor)) - no manual cleanup needed
 *
 * @author Michael Freidkin
 * @date 2026-02-09
 * @version 3.0
 */
#define _GNU_SOURCE // Must be before any includes
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
    log_msg(LOG_LEVEL_LOG, "DAC/PRACH Configurator exiting");
}

bool dac_config(void)
{
    log_msg(LOG_LEVEL_DBG, "DAC Configurator executing DAC config");

    int random = rand() % 10;
    if (random == 0)
    {
        log_msg(LOG_LEVEL_DBG, "DAC Configurator: simulating slow operation (6s)");
        usleep(6 * 1000 * 1000);
        return true;
    }
    else if (random == 1)
    {
        usleep(500 * 1000);
        log_msg(LOG_LEVEL_ERR, "DAC Configurator: DAC config failed");
        return false;
    }

    usleep(500 * 1000);
    log_msg(LOG_LEVEL_DBG, "DAC Configurator: DAC config done");
    return true;
}

bool prach_config(void)
{
    log_msg(LOG_LEVEL_DBG, "PRACH Configurator executing PRACH config");

    int random = rand() % 10;
    if (random == 0)
    {
        log_msg(LOG_LEVEL_DBG, "PRACH Configurator: simulating slow operation (6s)");
        usleep(6 * 1000 * 1000);
        return true;
    }
    else if (random == 1)
    {
        usleep(500 * 1000);
        log_msg(LOG_LEVEL_ERR, "PRACH Configurator: PRACH config failed");
        return false;
    }

    usleep(500 * 1000);
    log_msg(LOG_LEVEL_DBG, "PRACH Configurator: PRACH config done");
    return true;
}

static void* dac_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_DAC_CONFIG;
    // Register for both DAC and PRACH queues
    char name_dac[64];
    snprintf(name_dac, sizeof(name_dac), "dac-configurator-%d", getpid());

    if (!register_configurator(name_dac, CMD_DAC_CONFIG))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register DAC configurator");
        return NULL;
    }

    while (!stop)
    {
        CommandMsg* cmd = get_event(queue_id);
        if (cmd->type == CMD_EMPTY)
        {
            continue; // Timeout, no command available
        }

        bool result = dac_config();
        mark_event_done(cmd, result);
    }

    return NULL;
}

static void* prach_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PRACH_CONFIG;

    char name_prach[64];
    snprintf(name_prach, sizeof(name_prach), "prach-configurator-%d", getpid());
    if (!register_configurator(name_prach, CMD_PRACH_CONFIG))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register PRACH configurator");
        return NULL;
    }

    while (!stop)
    {
        CommandMsg* cmd = get_event(queue_id);
        if (cmd->type == CMD_EMPTY)
        {
            continue; // Timeout, no command available
        }

        bool result = prach_config();
        mark_event_done(cmd, result);
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Start threads
    pthread_t dac_tid, prach_tid;
    if (pthread_create(&dac_tid, NULL, dac_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create DAC thread");
        return 1;
    }

    if (pthread_create(&prach_tid, NULL, prach_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PRACH thread");
        stop = 1;
        pthread_join(dac_tid, NULL);
        return 1;
    }

    // Wait for threads
    pthread_join(dac_tid, NULL);
    pthread_join(prach_tid, NULL);

    return 0;
}
