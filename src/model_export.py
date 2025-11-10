# src/model_export.py
import joblib
import numpy as np

def to_q15(x, max_abs=None):
    if max_abs is None:
        max_abs = np.max(np.abs(x))
    if max_abs == 0:
        return np.zeros_like(x, dtype=np.int16)
    return np.int16(np.clip(x / max_abs * 32767, -32768, 32767))

def export_to_q15_header(iso, scaler, output_path='models/model_iforest.h', threshold=0.56):
    n_trees = len(iso.estimators_)
    all_thr  = [t.tree_.threshold for t in iso.estimators_]
    all_feat = [t.tree_.feature   for t in iso.estimators_]
    thr_flat = np.concatenate(all_thr)
    feat_flat = np.concatenate(all_feat).astype(np.int8)

    thr_q15   = to_q15(thr_flat,   np.max(np.abs(thr_flat)))
    mean_q15  = to_q15(scaler.mean_,  np.max(np.abs(scaler.mean_)))
    scale_q15 = to_q15(scaler.scale_, np.max(np.abs(scaler.scale_)))
    th_q15    = to_q15(np.array([threshold]), np.abs(threshold))[0]

    lines = [
        "/* =============================================== */",
        "/*   EclipseGuardian: SEL Detector (Q15 Model)     */",
        "/*   Auto-generated from Python model              */",
        "/* =============================================== */",
        "#ifndef MODEL_IFOREST_H",
        "#define MODEL_IFOREST_H",
        "",
        f"#define NUM_TREES {n_trees}",
        f"#define NUM_FEATURES {len(scaler.mean_)}",
        f"#define MODEL_THRESHOLD_Q15 {int(th_q15)}",
        "",
        f"static const int16_t scaler_mean_q15[NUM_FEATURES] = {{{', '.join(map(str, mean_q15))}}};",
        "",
        f"static const int16_t scaler_scale_q15[NUM_FEATURES] = {{{', '.join(map(str, scale_q15))}}};",
        "",
        f"static const int16_t thresholds_q15[{len(thr_q15)}] = {{{', '.join(map(str, thr_q15))}}};",
        "",
        f"static const int8_t features_idx[{len(feat_flat)}] = {{{', '.join(map(str, feat_flat))}}};",
        "",
        "#endif // MODEL_IFOREST_H"
    ]
    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"{output_path} generated!")