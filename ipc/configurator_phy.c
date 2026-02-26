/**
 * @file configurator_phy.c
 * @brief Single-threaded PHY configurator
 *
 * @details
 * This configurator handles PHY configuration requests on
 * queue_id=CMD_PHY_CONFIG(2). Runs single-threaded, processing commands
 * sequentially from dedicated queue pair. Registers in shared memory registry
 * as CONFIGURATOR type to enable health monitoring.
 *
 * Architecture:
 * - Queue ID: CMD_PHY_CONFIG (2)
 * - Processing Pattern: Single-threaded blocking dequeue loop
 * - Configuration Function: phy_config() - simulates PHY configuration
 * - Graceful Shutdown: SIGTERM/SIGINT signal handling
 *
 * Usage:
 * @code
 * ./configurator_phy
 * @endcode
 *
 * @author Michael Freidkin
 * @date 2026
 * @version 2.0
 */

#define _GNU_SOURCE // Must be before any includes

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
    log_msg(LOG_LEVEL_LOG, "PHY Configurator exiting");
}

bool phy_config(void)
{
    log_msg(LOG_LEVEL_DBG, "PHY Configurator executing PHY config");

    int random = rand() % 10;
    if (random == 0)
    {
        log_msg(LOG_LEVEL_DBG, "PHY Configurator: simulating slow operation (6s)");
        usleep(6 * 1000 * 1000);
        return true;
    }
    else if (random == 1)
    {
        usleep(500 * 1000);
        log_msg(LOG_LEVEL_ERR, "PHY Configurator: PHY config failed");
        return false;
    }

    usleep(500 * 1000);
    log_msg(LOG_LEVEL_DBG, "PHY Configurator: PHY config done");
    return true;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int queue_id = CMD_PHY_CONFIG;

    char name[64];
    snprintf(name, sizeof(name), "phy-configurator-%d", getpid());

    if (!register_configurator(name, queue_id))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register configurator");
        return 1;
    }

    log_msg(LOG_LEVEL_LOG, "PHY Configurator started (queue_id=%d)", queue_id);

    // Main loop
    while (!stop)
    {
        CommandMsg* cmd = get_event(queue_id);
        if (cmd->type == CMD_EMPTY)
        {
            // Timeout, no command available
            continue;
        }

        bool result = phy_config();
        mark_event_done(cmd, result);
    }

    return 0;
}
