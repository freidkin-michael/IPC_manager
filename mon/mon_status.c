/**
 * @file mon_status.c
 * @brief Status display utility for MON system
 */
#define TYPED_STATUS_MONITOR_IMPLEMENTATION
#include "mon.h"
#include "common.h"

int main()
{
    // Try to open existing shared memory (read-only)
    int fd = shm_open(SHM_TYPED_NAME, O_RDONLY, 0660);
    if (fd < 0)
    {
        printf("ERROR: Typed status shared memory not found.\n");
        printf("No publishers running?\n");
        return 1;
    }

    typed_status_shm_t* shm = mmap(NULL, sizeof(typed_status_shm_t), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED)
    {
        printf("ERROR: Failed to map shared memory\n");
        return 1;
    }

    if (!atomic_load(&shm->initialized))
    {
        printf("ERROR: Shared memory not initialized\n");
        munmap(shm, sizeof(typed_status_shm_t));
        return 1;
    }

    printf("================================================================================\n");
    printf("                    TYPED STATUS MONITOR SYSTEM                                 \n");
    printf("================================================================================\n");
    printf("\n");

    int32_t status_count = atomic_load(&shm->status_count);
    printf("Registered Statuses: %d\n", status_count);
    printf("Data Pool Usage:     %zu / %zu bytes (%.1f%%)\n",
           atomic_load(&shm->data_pool_offset),
           sizeof(shm->data_pool),
           100.0 * atomic_load(&shm->data_pool_offset) / sizeof(shm->data_pool));
    printf("\n");

    if (status_count == 0)
    {
        printf("No statuses registered yet.\n");
    }
    else
    {
        printf("%-20s %10s %10s %10s %12s %12s\n", "STATUS NAME", "VERSION", "SIZE", "SUBS", "PUBLISHED", "NOTIFIED");
        printf("--------------------------------------------------------------------------------------------\n");

        for (int i = 0; i < status_count; i++)
        {
            typed_status_item_t* status = &shm->statuses[i];

            uint32_t version = atomic_load(&status->version);
            int32_t sub_count = atomic_load(&status->sub_count);
            bool enabled = atomic_load(&status->enabled);
            uint64_t publish_count = atomic_load(&status->publish_count);
            uint64_t notify_count = atomic_load(&status->notify_count);

            printf("%-20s %10u %10zu %10d %12lu %12lu %s\n",
                   status->name,
                   version,
                   status->data_size,
                   sub_count,
                   publish_count,
                   notify_count,
                   enabled ? "" : "(disabled)");
        }
    }

    printf("\n");

    // Show detailed info for known types
    if (status_count > 0)
    {
        printf("=== Detailed Status Values ===\n\n");

        for (int i = 0; i < status_count; i++)
        {
            typed_status_item_t* status = &shm->statuses[i];

            printf("Status: %s\n", status->name);

            // Get data pointer from offset
            void* data_ptr = status->data_offset > 0 ? &shm->data_pool[status->data_offset] : NULL;

            // Try to detect type by name pattern
            if (strstr(status->name, "dac") && data_ptr && status->data_size == sizeof(DacStatus))
            {
                DacStatus* dac = (DacStatus*)data_ptr;
                printf("  Gain:      %u\n", dac->gain);
                printf("  Frequency: %u Hz\n", dac->frequency);
                printf("  Amplitude: %u\n", dac->amplitude);
                printf("  Enabled:   %s\n", dac->enabled ? "Yes" : "No");
                printf("  Timestamp: %lu\n", dac->timestamp);
            }
            else if (strstr(status->name, "phy") && !strstr(status->name, "prach") && data_ptr &&
                     status->data_size == sizeof(PhyStatus))
            {
                PhyStatus* phy = (PhyStatus*)data_ptr;
                printf("  Bandwidth:    %u Hz\n", phy->bandwidth);
                printf("  Carrier Freq: %u Hz\n", phy->carrier_freq);
                printf("  TX Power:     %u dBm\n", phy->tx_power);
                printf("  Modulation:   %u\n", phy->modulation);
                printf("  Timestamp:    %lu\n", phy->timestamp);
            }
            else if (strstr(status->name, "prach") && data_ptr && status->data_size == sizeof(PrachStatus))
            {
                PrachStatus* prach = (PrachStatus*)data_ptr;
                printf("  Preamble Format:        %u\n", prach->preamble_format);
                printf("  Root Sequence:          %u\n", prach->root_sequence);
                printf("  Zero Correlation Zone:  %u\n", prach->zero_correlation_zone);
                printf("  Frequency Offset:       %u\n", prach->frequency_offset);
                printf("  Timestamp:              %lu\n", prach->timestamp);
            }
            else
            {
                printf("  (Data not available or unknown format)\n");
            }
            printf("\n");
        }
    }

    munmap(shm, sizeof(typed_status_shm_t));
    return 0;
}
