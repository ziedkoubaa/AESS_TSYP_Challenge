#include "features_if.h"
#include "params.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* === Include your exported forest header ===
 * Replace ml/model_iforest.h with the header produced by your exporter.
 * That header should define (at minimum):
 *  - NUM_TREES, NUM_FEATURES (=5), NUM_NODES
 *  - scaler_mean_q15[], scaler_scale_q15[]     (Q15 arrays)
 *  - features_idx[], thresholds_q15[]          (flattened forest)
 *  - children_left[], children_right[], node_samples[], tree_offsets[]
 *  - SCALE_MEAN, SCALE_SCALE, SCALE_THRESHOLDS (floats)
 *  - MODEL_THRESHOLD_Q15, SCALE_DECISION_TH    (optional; to get float threshold)
 *  - IFOREST_MAX_SAMPLES, IFOREST_C_MAXSAMPLES (float const)
 */
#include "ml/model_iforest.h"

/* ---------- ring buffer for last few milliseconds ---------- */
#define BUF_MAX  ( (FS_HZ/1000) * (WIN_MS + 4) )  // slightly longer than window

typedef struct {
  float vin[BUF_MAX], iin[BUF_MAX], vout[BUF_MAX], iout[BUF_MAX], temp[BUF_MAX], ripple[BUF_MAX];
  uint16_t idx;
  uint16_t count;
} rawbuf_t;

static rawbuf_t rb;

static inline float clampf(float x, float a, float b){ return x < a ? a : (x > b ? b : x); }

void feats_reset(void){ memset(&rb, 0, sizeof(rb)); }

void feats_push_raw(float vin, float iin, float vout, float iout, float temp, float ripple)
{
  const uint16_t i = rb.idx;
  rb.vin[i] = vin; rb.iin[i] = iin; rb.vout[i] = vout; rb.iout[i] = iout; rb.temp[i] = temp; rb.ripple[i] = ripple;
  rb.idx = (rb.idx + 1) % BUF_MAX;
  if (rb.count < BUF_MAX) rb.count++;
}

/* Return start index for window of N samples ending at latest sample. */
static bool get_window(uint16_t samples, uint16_t* start_idx)
{
  if (rb.count < samples) return false;
  uint16_t end   = rb.idx;                      // next write pos
  uint16_t start = (end + BUF_MAX - samples) % BUF_MAX;
  *start_idx = start;
  return true;
}

static float mean_wrap(const float* a, uint16_t start, uint16_t n)
{
  float s=0; for (uint16_t k=0;k<n;k++) s += a[(start + k) % BUF_MAX];
  return s / (float)n;
}

bool feats_compute(features_t* out)
{
  const uint16_t win_samp = (uint16_t)((FS_HZ/1000) * WIN_MS);
  if (win_samp < 2) return false;

  uint16_t start=0;
  if (!get_window(win_samp, &start)) return false;

  const float dt_s = (float)win_samp / (float)FS_HZ;

  /* dI/dt on input current (A/ms) */
  const float i0 = rb.iin[start];
  const float i1 = rb.iin[(start + win_samp - 1) % BUF_MAX];
  out->dI_dt = (i1 - i0) / (dt_s * 1000.0f);

  /* Vout droop vs baseline = mean over last 5*window (bounded by buffer) */
  uint16_t base_n = win_samp * 5;
  if (base_n > rb.count) base_n = rb.count;
  const uint16_t base_start = (rb.idx + BUF_MAX - base_n) % BUF_MAX;
  const float vout_base = mean_wrap(rb.vout, base_start, base_n);
  const float vout_win  = mean_wrap(rb.vout, start,     win_samp);
  out->Vout_droop = (vout_base - vout_win);  // positive when drooping

  /* ripple_RMS: used only if USE_RIPPLE==1 (else 0.0) */
#if USE_RIPPLE
  out->ripple_RMS = mean_wrap(rb.ripple, start, win_samp);
#else
  out->ripple_RMS = 0.0f;
#endif

  /* efficiency = mean( (Vout*Iout)/(Vin*Iin) ) clipped */
  float s_eta = 0.0f;
  for (uint16_t k=0; k<win_samp; ++k){
    const uint16_t idx = (start + k) % BUF_MAX;
    const float pin  = fmaxf(rb.vin[idx] * rb.iin[idx], 1e-6f);
    const float pout = rb.vout[idx] * rb.iout[idx];
    s_eta += clampf(pout / pin, 0.0f, 1.2f);
  }
  out->efficiency = s_eta / (float)win_samp;

  /* dEff/dT: very light slope estimate using first/last quarter averages */
  const uint16_t q = win_samp/4 ? win_samp/4 : 1;
  float eff_a=0, eff_b=0, Ta=0, Tb=0;
  for (uint16_t k=0; k<q; ++k){
    const uint16_t ia = (start + k) % BUF_MAX;
    const uint16_t ib = (start + win_samp - 1 - k) % BUF_MAX;
    const float pA = fmaxf(rb.vin[ia]*rb.iin[ia],1e-6f);
    const float pB = fmaxf(rb.vin[ib]*rb.iin[ib],1e-6f);
    eff_a += clampf((rb.vout[ia]*rb.iout[ia])/pA, 0.0f, 1.2f);
    eff_b += clampf((rb.vout[ib]*rb.iout[ib])/pB, 0.0f, 1.2f);
    Ta += rb.temp[ia]; Tb += rb.temp[ib];
  }
  eff_a/=q; eff_b/=q; Ta/=q; Tb/=q;
  const float dT = (Tb - Ta);
  out->dEff_dT = (fabsf(dT) < 1e-6f) ? 0.0f : ((eff_b - eff_a) / dT);

  return true;
}

/* =================== Isolation Forest scoring =================== */

static inline float q15_to_real(int16_t q, float scale){ return ((float)q / 32767.0f) * scale; }

/* Pull StandardScaler params (real) from Q15 arrays in the exported header. */
static void scaler_get(float mean_out[], float std_out[])
{
  for (int i=0; i<NUM_FEATURES; ++i){
    mean_out[i] = q15_to_real(scaler_mean_q15[i],  SCALE_MEAN);
    std_out[i]  = q15_to_real(scaler_scale_q15[i], SCALE_SCALE);
    if (fabsf(std_out[i]) < 1e-12f) std_out[i] = 1.0f;
  }
}

/* Build z-scored feature vector in the expected model order:
   [dI_dt, Vout_droop, ripple_RMS, efficiency, dEff_dT] */
static void make_z(const features_t* f, float z[])
{
  float x[NUM_FEATURES];
  x[0] = f->dI_dt;
  x[1] = f->Vout_droop;
  x[2] = f->ripple_RMS;
  x[3] = f->efficiency;
  x[4] = f->dEff_dT;

  float mu[NUM_FEATURES], sd[NUM_FEATURES];
  scaler_get(mu, sd);
  for (int i=0; i<NUM_FEATURES; ++i) z[i] = (x[i] - mu[i]) / sd[i];
}

/* c(n) correction term (average path length in Binary Search Tree).
   We rely on IFOREST_C_MAXSAMPLES exported by the model for normalization. */
static inline float c_of(float n)
{
  if (n <= 1.0f) return 0.0f;
  const float gamma = 0.5772156649015329f;
  return 2.0f * (logf(n - 1.0f) + gamma) - (2.0f * (n - 1.0f) / n);
}

/* Traverse one tree, return path length with leaf-size correction. */
static float tree_path_length(int tree_idx, const float z[])
{
  int node = tree_offsets[tree_idx];
  int steps = 0;

  for (;;){
    const int feat  = features_idx[node];
    const int left  = children_left[node];
    const int right = children_right[node];

    if (left == -1 && right == -1) {        // leaf
      const int ns = (node_samples[node] <= 0) ? 1 : node_samples[node];
      return (float)steps + c_of((float)ns);
    }
    const float thr = q15_to_real(thresholds_q15[node], SCALE_THRESHOLDS);
    const float val = z[feat];
    node = (val <= thr) ? left : right;

    if (++steps > 1024) return (float)steps; // safety
  }
}

/* Aggregate path lengths across trees → anomaly score s(x)=2^{-E[h]/c(ms)} (higher ⇒ more anomalous). */
float iforest_score(const features_t* f)
{
  float z[NUM_FEATURES]; make_z(f, z);

  float sum_h = 0.0f;
  for (int t=0; t<NUM_TREES; ++t) sum_h += tree_path_length(t, z);

  const float Eh   = sum_h / (float)NUM_TREES;
  const float cmax = (float)IFOREST_C_MAXSAMPLES; // exported from training
  const float s    = powf(2.0f, -Eh / (cmax > 1e-9f ? cmax : 1.0f));
  return s;
}

/* IF threshold in float: prefer exported Q15 -> float; otherwise fallback macro. */
float iforest_threshold(void)
{
#ifdef MODEL_THRESHOLD_Q15
  return q15_to_real(MODEL_THRESHOLD_Q15, SCALE_DECISION_TH);
#else
  return IF_THRESHOLD_F_FALLBACK;
#endif
}

/* Simple rules: guard rails for safety — OR with the model. */
bool rules_triggered(const features_t* f)
{
  bool rule = false;
  if ((f->dI_dt > THR_DIDT_A_PER_MS) && (f->Vout_droop > THR_DROOP_V)) rule = true;
#if USE_RIPPLE
  if (f->ripple_RMS > THR_RIPPLE_V) rule = true;
#endif
  return rule;
}
