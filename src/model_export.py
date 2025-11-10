import joblib
import numpy as np
import math

def to_q15(x, max_abs=None):
    if max_abs is None:
        max_abs = np.max(np.abs(x))
    if max_abs == 0:
        return np.zeros_like(x, dtype=np.int16)
    return np.int16(np.clip(x / max_abs * 32767, -32768, 32767))

def export_to_q15_header(iso, scaler, output_path='models/model_iforest2.h', threshold=0.56):
    """
    Exports a Q15 header with everything needed for IsolationForest inference on MCU:
      - scaler mean/scale (Q15) + their dequant scales
      - concatenated node arrays: feature, threshold(Q15), children_left/right, node_samples
      - per-tree offsets
      - decision threshold in Q15 + its dequant scale
      - c(max_samples) constant
    Keeps the original signature unchanged.
    """
    # -----------------------------
    # 1) Gather forest structures
    # -----------------------------
    n_trees = len(iso.estimators_)
    trees = [t.tree_ for t in iso.estimators_]

    # Flatten arrays across all trees
    all_feat  = []
    all_thr   = []
    all_left  = []
    all_right = []
    all_nsamp = []
    tree_offsets = []

    node_base = 0
    for est in iso.estimators_:
        t = est.tree_
        n_nodes = t.node_count
        tree_offsets.append(node_base)

        # features: -2 means leaf (sklearn convention)
        all_feat.extend(t.feature.tolist())
        all_thr.extend(t.threshold.astype(np.float32).tolist())
        all_left.extend(t.children_left.tolist())
        all_right.extend(t.children_right.tolist())
        # use weighted_n_node_samples (float) -> cast to int
        all_nsamp.extend(t.weighted_n_node_samples.astype(np.int32).tolist())

        node_base += n_nodes

    feat_flat = np.asarray(all_feat,  dtype=np.int16)   # -2, 0..NUM_FEATURES-1
    thr_flat  = np.asarray(all_thr,   dtype=np.float32)
    left_flat = np.asarray(all_left,  dtype=np.int32)
    right_flat= np.asarray(all_right, dtype=np.int32)
    ns_flat   = np.asarray(all_nsamp, dtype=np.int16)   # fits typical values
    offsets   = np.asarray(tree_offsets, dtype=np.int32)

    # -----------------------------
    # 2) Prepare scaler + quantize
    # -----------------------------
    mean = scaler.mean_.astype(np.float32)
    scale = scaler.scale_.astype(np.float32)

    # dequant scales (store as macros in header)
    mean_scale  = float(np.max(np.abs(mean)))  or 1.0
    scale_scale = float(np.max(np.abs(scale))) or 1.0
    thr_scale   = float(np.max(np.abs(thr_flat))) or 1.0
    dec_scale   = float(abs(threshold)) or 1.0

    mean_q15  = to_q15(mean,  mean_scale)
    scale_q15 = to_q15(scale, scale_scale)
    thr_q15   = to_q15(thr_flat, thr_scale)
    th_q15    = int(to_q15(np.array([threshold], dtype=np.float32), dec_scale)[0])

    # -----------------------------
    # 3) c(max_samples) constant
    # -----------------------------
    # IsolationForest score uses c(max_samples) in 2^{-E[h]/c(ms)}
    def _c_of(n: int) -> float:
        if n <= 1: return 0.0
        gamma = 0.5772156649015329
        return 2.0 * (math.log(n - 1.0) + gamma) - (2.0 * (n - 1.0) / n)

    # sklearn exposes .max_samples_ after fit
    max_samples = int(getattr(iso, "max_samples_", 256))
    c_max = _c_of(max_samples)

    # -----------------------------
    # 4) Emit header
    # -----------------------------
    lines = [
        "/* =============================================== */",
        "/*   PowerSense: SEL Detector (Q15 Model)     */",
        "/*   Auto-generated from Python model              */",
        "/* =============================================== */",
        "#ifndef MODEL_IFOREST_H",
        "#define MODEL_IFOREST_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define NUM_TREES {n_trees}",
        f"#define NUM_FEATURES {len(scaler.mean_)}",
        f"#define NUM_NODES {len(thr_q15)}",
        "",
        "/* Q15 dequantization scales: real = (q15/32767.0f) * SCALE_* */",
        f"#define SCALE_MEAN {mean_scale:.9f}f",
        f"#define SCALE_SCALE {scale_scale:.9f}f",
        f"#define SCALE_THRESHOLDS {thr_scale:.9f}f",
        f"#define SCALE_DECISION_TH {dec_scale:.9f}f",
        "",
        "/* IsolationForest constants */",
        f"#define IFOREST_MAX_SAMPLES {max_samples}",
        f"#define IFOREST_C_MAXSAMPLES {c_max:.9f}f",
        "",
        f"#define MODEL_THRESHOLD_Q15 {th_q15}",  # decision threshold on anomaly_score
        "",
        "/* StandardScaler params (Q15) */",
        f"static const int16_t scaler_mean_q15[NUM_FEATURES] = {{{', '.join(map(str, mean_q15))}}};",
        f"static const int16_t scaler_scale_q15[NUM_FEATURES] = {{{', '.join(map(str, scale_q15))}}};",
        "",
        "/* Forest arrays (flattened across trees) */",
        "/* feature index per node; -2 denotes leaf */",
        f"static const int16_t features_idx[NUM_NODES] = {{{', '.join(map(str, feat_flat))}}};",
        "/* threshold per node (Q15, dequant with SCALE_THRESHOLDS) */",
        f"static const int16_t thresholds_q15[NUM_NODES] = {{{', '.join(map(str, thr_q15))}}};",
        "/* children indices per node (-1 for none) */",
        f"static const int32_t children_left[NUM_NODES]  = {{{', '.join(map(str, left_flat))}}};",
        f"static const int32_t children_right[NUM_NODES] = {{{', '.join(map(str, right_flat))}}};",
        "/* samples per node (int) for c(n) correction at leaves) */",
        f"static const int16_t node_samples[NUM_NODES] = {{{', '.join(map(str, ns_flat))}}};",
        "/* start index of each tree in the flattened arrays */",
        f"static const int32_t tree_offsets[NUM_TREES] = {{{', '.join(map(str, offsets))}}};",
        "",
        "#endif // MODEL_IFOREST_H"
    ]

    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"{output_path} generated!")
if __name__ == "__main__":
    # ------------------------------------------------------------
    # 9Export model to Q15 header for embedded use
    # ------------------------------------------------------------

    # Load trained parts
    pack = joblib.load("models/iforest_optimized_pack.pkl")
    pipe = pack["pipe"]
    iso = pipe.named_steps["iforest"]
    scaler = pipe.named_steps["scaler"]
    threshold = pack["threshold"]

    # Export header
    export_to_q15_header(iso, scaler, output_path="models/model_iforest2.h", threshold=threshold)

