# src/main.py
import os, time, glob, joblib, pandas as pd, numpy as np
from sklearn.ensemble import IsolationForest
from feature_engineering import extract_features
from model_export import export_to_q15_header

# -------------------------------------------------
# CONFIGURATION
# -------------------------------------------------
BUFFER_DIR          = "data/buffer/"
MODEL_DIR           = "models/"
MIN_HEALTHY_SAMPLES = 100
HEALTHY_SCORE       = 0.0
RETRAIN_EVERY_SEC   = 5                       # 5 sec for test
THRESHOLD           = 0.56
# -------------------------------------------------

print("Loading model...")
iso    = joblib.load(f"{MODEL_DIR}iforest_model.pkl")
scaler = joblib.load(f"{MODEL_DIR}feature_scaler.pkl")
print("Model loaded.")

def load_healthy_chunks():
    print("Scanning buffer for CSVs...")
    healthy = []
    for path in glob.glob(f"{BUFFER_DIR}*.csv"):
        print(f"  → Found: {os.path.basename(path)} (size: {os.path.getsize(path)} bytes)")
        if os.path.getsize(path) == 0:
            print("  → Empty file, skipping.")
            os.remove(path)
            continue

        try:
            df_raw = pd.read_csv(path)
            print(f"  → Loaded {len(df_raw)} rows")
        except Exception as e:
            print(f"  → ERROR reading CSV: {e}")
            os.remove(path)
            continue

        try:
            df_feat = extract_features(df_raw)
            print(f"  → Extracted {len(df_feat)} feature rows")
        except Exception as e:
            print(f"  → ERROR in feature extraction: {e}")
            os.remove(path)
            continue

        X = df_feat[["dI_dt","Vout_droop","ripple_RMS","efficiency","dEff_dT"]].values
        try:
            X_scaled = scaler.transform(X)
            scores = -iso.decision_function(X_scaled)
            mask = scores > HEALTHY_SCORE
            healthy_part = df_feat[mask]
            print(f"  → {len(healthy_part)} healthy rows kept (score > {HEALTHY_SCORE})")
            healthy.append(healthy_part)
        except Exception as e:
            print(f"  → ERROR in scoring: {e}")

        print(f"  → Deleting {os.path.basename(path)}")
        os.remove(path)

    if not healthy:
        print("No healthy data found in buffer.")
        return pd.DataFrame()
    return pd.concat(healthy, ignore_index=True)

def retrain():
    global iso, scaler
    print("Starting retrain...")

    all_files = glob.glob(f"{BUFFER_DIR}healthy_*.csv")
    if not all_files:
        print("No healthy_*.csv files found for retraining.")
        return False

    print(f"Found {len(all_files)} healthy files. Loading...")
    healthy = pd.concat([pd.read_csv(f) for f in all_files])
    print(f"Total healthy samples: {len(healthy)}")

    if len(healthy) < MIN_HEALTHY_SAMPLES:
        print(f"Not enough samples ({len(healthy)} < {MIN_HEALTHY_SAMPLES}). Skipping retrain.")
        return False

    X = healthy[["dI_dt","Vout_droop","ripple_RMS","efficiency","dEff_dT"]].values
    print(f"Feature matrix shape: {X.shape}")

    # ---- Online scaler update (FIX: axis=0) ----
    old_n = 10000
    new_n = len(X)
    old_mean, old_scale = scaler.mean_, scaler.scale_

    new_mean = (old_mean * old_n + X.mean(axis=0) * new_n) / (old_n + new_n)
    new_var  = (old_scale**2 * old_n +
                X.var(axis=0) * new_n +
                ((X.mean(axis=0) - old_mean)**2 * old_n * new_n) / (old_n + new_n))
    new_scale = np.sqrt(new_var / (old_n + new_n))

    scaler.mean_  = new_mean
    scaler.scale_ = new_scale
    print("Scaler updated.")

    # ---- Retrain model ----
    iso = IsolationForest(n_estimators=100, contamination=0.01, random_state=42)
    iso.fit(scaler.transform(X))
    print("Isolation Forest retrained.")

    # ---- Save ----
    joblib.dump(iso,    f"{MODEL_DIR}iforest_model.pkl")
    joblib.dump(scaler, f"{MODEL_DIR}feature_scaler.pkl")
    export_to_q15_header(iso, scaler, threshold=THRESHOLD)

    # ---- Timestamp ----
    with open(f"{MODEL_DIR}last_retrain.txt", "w") as f:
        f.write(str(int(time.time())))

    # ---- Clean ----
    for f in all_files:
        os.remove(f)

    print(f"RETRAINED with {len(X)} healthy samples")
    print(f"New model saved: {MODEL_DIR}model_iforest.h")
    return True

# -------------------------------------------------
# MAIN LOOP
# -------------------------------------------------
if __name__ == "__main__":
    os.makedirs(BUFFER_DIR, exist_ok=True)

    # Force first retrain
    if not os.path.exists(f"{MODEL_DIR}last_retrain.txt"):
        with open(f"{MODEL_DIR}last_retrain.txt", "w") as f:
            f.write(str(int(time.time()) - 3600))

    print("EclipseGuardian continuous-learning STARTED")
    print(f"   → Will retrain every {RETRAIN_EVERY_SEC} seconds (test mode)")

    while True:
        new_data = load_healthy_chunks()
        if len(new_data) > 0:
            ts = int(time.time())
            out_path = f"{BUFFER_DIR}healthy_{ts}.csv"
            new_data.to_csv(out_path, index=False)
            print(f"Saved {len(new_data)} healthy rows → {os.path.basename(out_path)}")

        last = int(open(f"{MODEL_DIR}last_retrain.txt").read().strip())
        if time.time() - last >= RETRAIN_EVERY_SEC:
            retrain()

        time.sleep(1)