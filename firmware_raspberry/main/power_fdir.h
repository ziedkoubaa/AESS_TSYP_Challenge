#pragma once
#include <stdbool.h>
#include "features_if.h"

/* Initialize GPIO lines (EN / eFuse) using libgpiod. */
bool power_init(void);

/* Hard cut (drop EN / open eFuse). */
void power_cut(void);

/* Soft restart (enable EN / close eFuse). */
void power_restart_soft(void);

/* One FDIR step (call at 1 ms cadence). */
void fdir_step(bool anomaly, const features_t* feats, float score);

/* Inject your own ADC reads here (replace with real driver). */
void read_latest_raw(float* vin, float* iin, float* vout, float* iout, float* temp, float* ripple);
