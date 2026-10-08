import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import ast
from scipy.signal import butter, filtfilt

# --- 1. LOAD & NORMALIZE ---
df = pd.read_csv("csi_log.csv")

# parse back into an actual list
df['amplitudes'] = df['amplitudes'].apply(ast.literal_eval)

amp_matrix = np.array(df['amplitudes'].tolist())
print("amp_matrix shape:", amp_matrix.shape)  # sanity check: (num_packets, num_subcarriers)

# --- mask: drop DC spike + null/guard band ---
n_subcarriers = amp_matrix.shape[1]
mask = np.ones(n_subcarriers, dtype=bool)
mask[0:2] = False        # DC spike + adjacent
mask[28:36] = False      # null/guard band

# --- normalize: divide each packet by its own median ---
row_median = np.median(amp_matrix[:, mask], axis=1, keepdims=True)
norm_amp_matrix = amp_matrix / row_median

# --- 2. DENOISE STAGE 1: Rolling Median (Kill Outliers) ---
# Window of 15 at 50Hz = 0.3 seconds.
df_med = pd.DataFrame(norm_amp_matrix).rolling(window=15, min_periods=1, center=True).median()

# Fill NaN values at the edges so the Butterworth filter doesn't crash
df_med = df_med.bfill().ffill()
median_filtered_matrix = df_med.to_numpy()

# --- 3. DENOISE STAGE 2: Butterworth Low-Pass Filter ---
def butter_lowpass_filter(data, cutoff, fs, order=3):
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = butter(order, normal_cutoff, btype='low', analog=False)
    y = filtfilt(b, a, data, axis=0) 
    return y

fs = 50.0       # Sampling rate (50 Hz)
cutoff = 1.0    # 1 Hz cutoff (removes jitter, keeps slow human movement)

clean_norm_matrix = butter_lowpass_filter(median_filtered_matrix, cutoff, fs)

# --- 4. DENORMALIZE (THE FIX) ---
# Instead of multiplying by the spiky time-series row_median, 
# we multiply by the global median to restore the 40-60 amplitude scale safely.
global_scale = np.median(row_median)
final_clean_matrix = clean_norm_matrix * global_scale


# --- 5. PLOT: All Subcarriers Over Time ---
fig, axes = plt.subplots(1, 2, figsize=(15, 6))

cmap = cm.viridis 

for sc in range(n_subcarriers):
    # Skip the dropped subcarriers
    if not mask[sc]:
        continue 
        
    # Calculate color based on the subcarrier index
    color_val = sc / (n_subcarriers - 1) if n_subcarriers > 1 else 0
    line_color = cmap(color_val)
    
    # Plot Raw on the Left
    axes[0].plot(amp_matrix[:, sc], alpha=0.6, linewidth=1.0, color=line_color)
    
    # Plot Cleaned on the Right
    axes[1].plot(final_clean_matrix[:, sc], alpha=0.8, linewidth=1.5, color=line_color)

# Formatting Raw Plot
axes[0].set_title("Raw Amplitude over Time")
axes[0].set_xlabel("Packet Index (Time)")
axes[0].set_ylabel("Raw Amplitude")
axes[0].grid(True, alpha=0.3)

# Formatting Cleaned Plot
axes[1].set_title("Fully Cleaned Amplitude (Median + Butterworth)")
axes[1].set_xlabel("Packet Index (Time)")
axes[1].set_ylabel("Cleaned Amplitude")
axes[1].grid(True, alpha=0.3)

# Add Colorbar
sm = plt.cm.ScalarMappable(cmap=cmap, norm=plt.Normalize(vmin=0, vmax=n_subcarriers-1))
cbar = fig.colorbar(sm, ax=axes, orientation='vertical', fraction=0.02, pad=0.02)
cbar.set_label('Subcarrier Index')

plt.tight_layout()
plt.show()