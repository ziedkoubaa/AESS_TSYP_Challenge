# src/inference.py
import joblib
import numpy as np

# Configuration
MODEL_DIR = "models/"
THRESHOLD = 0.56  # Anomaly threshold (adjust if needed)

# Load the model and scaler
iso = joblib.load(f"{MODEL_DIR}iforest_model.pkl")
scaler = joblib.load(f"{MODEL_DIR}feature_scaler.pkl")

def run_inference(features):
    """
    Run anomaly detection on input features.
    
    Args:
        features (list or np.array): [dI_dt, Vout_droop, ripple_RMS, efficiency, dEff_dT]
    
    Returns:
        dict: {'anomaly_score': float, 'is_anomaly': bool}
    """
    # Convert to numpy array and reshape for single sample
    X = np.array(features).reshape(1, -1)
    
    # Scale the features
    X_scaled = scaler.transform(X)
    
    # Get anomaly score (higher = more normal, lower = more anomalous)
    score = -iso.decision_function(X_scaled)[0]
    
    # Determine if it's an anomaly
    is_anomaly = score > THRESHOLD
    
    return {
        'anomaly_score': score,
        'is_anomaly': is_anomaly
    }

if __name__ == "__main__":
    print("Enter the 5 feature values separated by commas:")
    print("Format: dI_dt, Vout_droop, ripple_RMS, efficiency, dEff_dT")
    input_str = input("> ")
    
    try:
        features = [float(x.strip()) for x in input_str.split(',')]
        if len(features) != 5:
            raise ValueError("Exactly 5 values required.")
        
        result = run_inference(features)
        print("\nResult:")
        print(f"Anomaly Score: {result['anomaly_score']:.4f}")
        print(f"Is Anomaly: {'Yes' if result['is_anomaly'] else 'No'}")
    except ValueError as e:
        print(f"Error: {e}. Please enter valid numbers.")