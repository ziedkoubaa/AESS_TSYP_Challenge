#pragma once
/*
 * Minimal parameters for Raspberry Pi prototype
 * Tune these on the bench.
 */

// ===== Sampling & windows =====
#define FS_HZ               10000   // raw effective sample rate (Hz) feeding feats_push_raw()
#define WIN_MS              2       // feature window length (ms)
#define HOP_MS              1       // hop (ms) -> 50% overlap

// ===== Detection policy =====
#define DWELL_HITS          2       // consecutive windows required to trigger
#define HOLDOFF_MS          10      // power off (ms) to clear latch
#define VERIFY_MS           100     // post-restart verification (ms)

// ===== Rule thresholds (starting points) =====
#define THR_DIDT_A_PER_MS   0.8f    // A/ms
#define THR_DROOP_V         0.040f  // 40 mV
#define THR_RIPPLE_V        0.030f  // 30 mV
#define USE_RIPPLE          0       // 0 = ignore ripple_RMS for first proto

// ===== GPIO (libgpiod numbering = BCM line index on gpiochip) =====
#define GPIOCHIP_NAME       "gpiochip0"
#define PIN_REG_EN          5       // BCM line for regulator EN (set to your wiring)
#define PIN_EFUSE_EN        -1      // optional eFuse/load switch enable; -1 if unused

// ===== IF threshold fallback (only used if not exported by model_iforest.h) =====
#define IF_THRESHOLD_F_FALLBACK   0.56f
