# src/feature_engineering.py
import pandas as pd
import numpy as np

def extract_features(df_raw, window_ms=2, fs=10000):
    step = int(window_ms / 2 * fs / 1000)
    feats, times, labels = [], [], []

    for i in range(0, len(df_raw) - step, step):
        seg = df_raw.iloc[i:i + step]
        Vin  = seg["Vin_V"].mean()
        Iin  = seg["Iin_A"]
        Vout = seg["Vout_V"]
        Iout = seg["Iout_A"]
        Temp = seg["Temp_C"]

        dIdt      = np.gradient(Iin, edge_order=1).mean()
        Vout_drop = Vin - Vout.mean()
        ripple    = np.sqrt(np.mean((Vout - Vout.mean())**2))
        eff       = (Vout.mean() * Iout.mean()) / (Vin * Iin.mean())
        dEff_dT   = np.gradient(eff * np.ones(len(seg)), Temp, edge_order=1).mean() if len(seg) > 1 else 0

        feats.append([dIdt, Vout_drop, ripple, eff, dEff_dT])
        times.append(seg["time_s"].mean())
        labels.append(seg["fault_label"].max())

    df = pd.DataFrame(feats,
                      columns=["dI_dt","Vout_droop","ripple_RMS","efficiency","dEff_dT"])
    df["time_s"] = times
    df["fault_label"] = labels
    return df