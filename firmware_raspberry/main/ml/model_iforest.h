#pragma once
/* -----------------------------------------------------------------------
 *  REPLACE THIS FILE with your exported header from Python.
 *  This template only documents the expected symbols.
 *  If you keep this stub, the build will fail on purpose.
 * ----------------------------------------------------------------------- */

#error "Replace main/ml/model_iforest.h with your generated header (export_to_q15_header)."

/* Expected symbols from your exporter:

#define NUM_TREES        <int>
#define NUM_FEATURES     5
#define NUM_NODES        <int>

#define SCALE_MEAN       <float>   // dequant scale for scaler_mean_q15
#define SCALE_SCALE      <float>   // dequant scale for scaler_scale_q15
#define SCALE_THRESHOLDS <float>   // dequant scale for thresholds_q15

// Optional, if your exporter provides decision threshold:
#define MODEL_THRESHOLD_Q15 <int16_t>
#define SCALE_DECISION_TH   <float>

#define IFOREST_MAX_SAMPLES   <int>
#define IFOREST_C_MAXSAMPLES  <float>

extern const int16_t scaler_mean_q15[NUM_FEATURES];
extern const int16_t scaler_scale_q15[NUM_FEATURES];

extern const int16_t features_idx[NUM_NODES];      // -2 at leaves not used here; only internal nodes referenced
extern const int16_t thresholds_q15[NUM_NODES];
extern const int32_t children_left[NUM_NODES];     // -1 leaf
extern const int32_t children_right[NUM_NODES];    // -1 leaf
extern const int16_t node_samples[NUM_NODES];      // training samples at node
extern const int32_t tree_offsets[NUM_TREES];      // start index per tree in flattened arrays
*/
