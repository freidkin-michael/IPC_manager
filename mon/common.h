/**
 * @file common.h
 * @brief Common data structures for MON system
 */
#ifndef MON_COMMON_H
#define MON_COMMON_H

#include <stdint.h>

// DAC Configuration Status
typedef struct
{
    uint32_t gain;
    uint32_t frequency;
    uint16_t amplitude;
    uint8_t enabled;
    uint64_t timestamp;
} DacStatus;

// PHY Configuration Status
typedef struct
{
    uint32_t bandwidth;
    uint32_t carrier_freq;
    uint16_t tx_power;
    uint8_t modulation;
    uint64_t timestamp;
} PhyStatus;

// PRACH Configuration Status
typedef struct
{
    uint32_t preamble_format;
    uint32_t root_sequence;
    uint16_t zero_correlation_zone;
    uint8_t frequency_offset;
    uint64_t timestamp;
} PrachStatus;

#endif // MON_COMMON_H
