#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Feature vector computed per 2 ms window with 1 ms hop. */
typedef struct {
  float dI_dt;        // A/ms
  float Vout_droop;   // V
  float ripple_RMS;   // V (0.0 if USE_RIPPLE=0)
  float efficiency;   // 0..1.2 typical
  float dEff_dT;      // 1/°C
} features_t;

/* Ring-buffer interface: push raw samples at your effective sample rate (FS_HZ). */
void  feats_reset(void);
void  feats_push_raw(float vin, float iin, float vout, float iout, float temp, float ripple);

/* Compute features for current window. Returns true when a full window is available. */
bool  feats_compute(features_t* out);

/* Isolation Forest scoring (embedded re-implementation).
   Returns anomaly score comparable against the exported IF threshold (higher ⇒ more anomalous).
*/
float iforest_score(const features_t* f);

/* Return IF threshold (float). Uses the model’s exported value if available; otherwise fallback. */
float iforest_threshold(void);

/* Rule-based guard (OR with model) using simple thresholds + dwell. */
bool  rules_triggered(const features_t* f);
