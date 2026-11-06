import streamlit as st
import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots

# Page Configuration
st.set_page_config(
    page_title="VibeGuard Level-1: Digital Twin & Telemetry",
    page_icon="🛡️",
    layout="wide",
    initial_sidebar_state="expanded"
)

# Custom Styling (Cyber-Industrial Dark Theme)
st.markdown("""
<style>
    .reportview-container { background: #0e1117; }
    .main-header { font-size: 2.2rem; font-weight: 700; color: #00e5ff; margin-bottom: 0px; }
    .sub-header { font-size: 1.0rem; color: #888888; margin-bottom: 20px; }
    .metric-box { background: #1a1f2c; border-radius: 8px; padding: 15px; border-left: 4px solid #00e5ff; }
</style>
""", unsafe_allow_html=True)

st.markdown('<p class="main-header">🛡️ VibeGuard Level-1: Industrial Edge Digital Twin</p>', unsafe_allow_html=True)
st.markdown('<p class="sub-header">Model-Based Hardware Emulation | ESP32-DevKitC-32E + ADXL345 (4-Wire SPI) + 12V 600 RPM N20 Rig</p>', unsafe_allow_html=True)

# ==========================================
# Sidebar: Interactive Plant Controls
# ==========================================
st.sidebar.image("https://img.icons8.com/fluency/96/engine.png", width=70)
st.sidebar.title("🎛️ Rig Simulation Controls")

sim_state = st.sidebar.selectbox(
    "Plant Operating Mode",
    ["Normal Baseline", "Mechanical Unbalance Fault", "Bearing Loose Foot Shock", "SPI Hardware Disconnect"]
)

motor_rpm = st.sidebar.slider("Motor Speed (RPM)", min_value=300, max_value=900, value=600, step=30)
fundamental_freq_hz = motor_rpm / 60.0

imbalance_mass_g = st.sidebar.slider(
    "Eccentric Mass Severity (Grams)",
    min_value=0.0,
    max_value=5.0,
    value=0.0 if sim_state == "Normal Baseline" else 2.5,
    step=0.25
)

st.sidebar.markdown("---")
st.sidebar.markdown("### ⚙️ Edge DSP Configuration")
sampling_freq_hz = 800  # ADXL345 ODR
buffer_size = 256       # Radix-2 Window
warning_thresh = 0.35   # g-RMS
alarm_thresh = 0.70     # g-RMS

# ==========================================
# Physics & Signal Generation Engine
# ==========================================
t = np.linspace(0, (buffer_size - 1) / sampling_freq_hz, buffer_size)
omega = 2.0 * np.pi * fundamental_freq_hz

if sim_state == "SPI Hardware Disconnect":
    ax = np.zeros(buffer_size)
    ay = np.zeros(buffer_size)
    az = np.zeros(buffer_size)
else:
    # Baseline normal vibrations (1X minor residual + high-frequency gear meshing + noise)
    noise_x = np.random.normal(0, 0.025, buffer_size)
    noise_y = np.random.normal(0, 0.025, buffer_size)
    noise_z = np.random.normal(0, 0.025, buffer_size)

    # 1X fundamental + 2X / 3X harmonics + 1.0g static Z gravity
    ax = 0.06 * np.sin(omega * t) + 0.015 * np.sin(2 * omega * t) + noise_x
    ay = 0.06 * np.cos(omega * t) + 0.015 * np.cos(2 * omega * t) + noise_y
    az = 1.00 + 0.02 * np.sin(3 * omega * t) + noise_z

    if sim_state in ["Mechanical Unbalance Fault", "Bearing Loose Foot Shock"] or imbalance_mass_g > 0:
        # Dynamic unbalance acceleration proportional to m * r * omega^2
        severity_g = (imbalance_mass_g / 5.0) * 1.85
        ax += severity_g * np.sin(omega * t)
        ay += severity_g * np.cos(omega * t)
        az += (severity_g * 0.35) * np.sin(2 * omega * t)

    if sim_state == "Bearing Loose Foot Shock":
        shock_indices = np.random.choice(buffer_size, size=6, replace=False)
        ax[shock_indices] += np.random.uniform(1.2, 2.5, size=6)

# ==========================================
# Edge DSP Execution (Matching Sreehari's C++)
# ==========================================
# 1. DC Removal
ac_x = ax - np.mean(ax)
ac_y = ay - np.mean(ay)
ac_z = az - np.mean(az)

# 2. Vector RMS Magnitude
vec_mag = np.sqrt(ac_x**2 + ac_y**2 + ac_z**2)
rms_g = np.sqrt(np.mean(vec_mag**2))
peak_g = np.max(vec_mag)
crest_factor = (peak_g / rms_g) if rms_g > 0 else 0.0

# 3. Hann Windowing + FFT
hann = np.hanning(buffer_size)
windowed_x = ac_x * hann
fft_complex = np.fft.rfft(windowed_x)
fft_freqs = np.fft.rfftfreq(buffer_size, 1.0 / sampling_freq_hz)
fft_mags = (np.abs(fft_complex) / (buffer_size / 2))

# Find Dominant Peak Frequency
peak_idx = np.argmax(fft_mags[1:]) + 1
dominant_freq = fft_freqs[peak_idx]
dominant_mag = fft_mags[peak_idx]

# ==========================================
# Live KPI Metrics Row
# ==========================================
col1, col2, col3, col4, col5 = st.columns(5)

with col1:
    st.metric("3-Axis Vector RMS", f"{rms_g:.3f} g", delta=f"{rms_g - alarm_thresh:+.3f} g", delta_color="inverse")
with col2:
    st.metric("Peak Acceleration", f"{peak_g:.3f} g")
with col3:
    st.metric("1X Motor Freq", f"{fundamental_freq_hz:.1f} Hz", delta=f"{motor_rpm} RPM")
with col4:
    st.metric("Dominant Peak", f"{dominant_freq:.1f} Hz", delta=f"{dominant_mag:.3f} g")
with col5:
    if sim_state == "SPI Hardware Disconnect":
        st.error("🚨 STATE: FAULT / INVALID\n(SPI Comm Error)")
    elif rms_g >= alarm_thresh:
        st.error(f"🔴 STATE: ABNORMAL\n(Alarm > {alarm_thresh:.2f}g)")
    elif rms_g >= warning_thresh:
        st.warning(f"🟡 STATE: WARNING\n(Pre-Alarm > {warning_thresh:.2f}g)")
    else:
        st.success(f"🟢 STATE: NORMAL\n(Healthy < {warning_thresh:.2f}g)")

st.markdown("---")

# ==========================================
# 2-Column Main Display: 3D Plant & Oscilloscope
# ==========================================
row1_left, row1_right = st.columns([1, 1.2])

with row1_left:
    st.subheader("🌐 3D Digital Twin: Motor Rig Kinematics")
    
    # Generate 3D Motor Cylinder & Orbit Vector
    u = np.linspace(0, 2 * np.pi, 30)
    v = np.linspace(-1, 1, 15)
    U, V = np.meshgrid(u, v)
    cyl_x = 0.5 * np.cos(U)
    cyl_y = 0.5 * np.sin(U)
    cyl_z = 2.0 * V

    # Orbital trace of the unbalance vector
    orbit_scale = 0.3 + (rms_g * 0.5)
    orbit_x = orbit_scale * np.cos(u)
    orbit_y = orbit_scale * np.sin(u)
    orbit_z = np.full_like(u, 1.8)

    fig_3d = go.Figure()
    
    # Motor Housing Surface
    fig_3d.add_trace(go.Surface(
        x=cyl_x, y=cyl_y, z=cyl_z,
        colorscale='Viridis',
        showscale=False,
        opacity=0.7,
        name='Motor Body'
    ))

    # Eccentric Imbalance Orbit Ring
    orbit_color = '#FF0055' if rms_g >= alarm_thresh else ('#FFAA00' if rms_g >= warning_thresh else '#00FFAA')
    fig_3d.add_trace(go.Scatter3d(
        x=orbit_x, y=orbit_y, z=orbit_z,
        mode='lines+markers',
        line=dict(color=orbit_color, width=6),
        marker=dict(size=4),
        name='Shaft Orbit Vector'
    ))

    fig_3d.update_layout(
        scene=dict(
            xaxis=dict(range=[-1.5, 1.5], backgroundcolor="#0e1117", gridcolor="#222"),
            yaxis=dict(range=[-1.5, 1.5], backgroundcolor="#0e1117", gridcolor="#222"),
            zaxis=dict(range=[-2.5, 2.5], backgroundcolor="#0e1117", gridcolor="#222"),
            aspectratio=dict(x=1, y=1, z=1.2)
        ),
        height=380,
        margin=dict(l=10, r=10, t=10, b=10),
        template="plotly_dark"
    )
    st.plotly_chart(fig_3d, use_container_width=True)

with row1_right:
    st.subheader("📈 Real-Time 3-Axis Oscilloscope (DC-Removed)")
    
    fig_osc = go.Figure()
    fig_osc.add_trace(go.Scatter(x=t * 1000, y=ac_x, mode='lines', name='X-Axis (Radial)', line=dict(color='#00e5ff', width=1.8)))
    fig_osc.add_trace(go.Scatter(x=t * 1000, y=ac_y, mode='lines', name='Y-Axis (Tangential)', line=dict(color='#ff00ff', width=1.5)))
    fig_osc.add_trace(go.Scatter(x=t * 1000, y=ac_z, mode='lines', name='Z-Axis (Axial)', line=dict(color='#ffeb3b', width=1.2, dash='dot')))
    fig_osc.add_trace(go.Scatter(x=t * 1000, y=vec_mag, mode='lines', name='Vector Magnitude |a|', line=dict(color='#00ff66', width=2.2)))
    
    fig_osc.add_hline(y=alarm_thresh, line_dash="dash", line_color="#ff1744", annotation_text="Critical Alarm")
    fig_osc.add_hline(y=warning_thresh, line_dash="dot", line_color="#ff9100", annotation_text="Warning Threshold")
    
    fig_osc.update_layout(
        height=380,
        xaxis_title="Time Window (Milliseconds)",
        yaxis_title="Dynamic Acceleration (g)",
        template="plotly_dark",
        margin=dict(l=20, r=20, t=20, b=20),
        legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1)
    )
    st.plotly_chart(fig_osc, use_container_width=True)

# ==========================================
# Bottom Row: FFT Spectrogram & Shaft Orbital Lissajous
# ==========================================
row2_left, row2_right = st.columns([1.3, 1])

with row2_left:
    st.subheader("⚡ Fast Fourier Transform (FFT Energy Spectrum)")
    
    fig_fft = go.Figure()
    fig_fft.add_trace(go.Bar(
        x=fft_freqs, y=fft_mags,
        name='Spectral Magnitude',
        marker_color='#00e5ff',
        opacity=0.85
    ))
    
    # Harmonic Markers
    fig_fft.add_vline(x=fundamental_freq_hz, line_dash="dot", line_color="#00ff66", annotation_text=f"1X ({fundamental_freq_hz:.1f}Hz)")
    fig_fft.add_vline(x=2 * fundamental_freq_hz, line_dash="dot", line_color="#ffeb3b", annotation_text=f"2X ({2*fundamental_freq_hz:.1f}Hz)")
    fig_fft.add_vline(x=3 * fundamental_freq_hz, line_dash="dot", line_color="#ff00ff", annotation_text=f"3X ({3*fundamental_freq_hz:.1f}Hz)")

    fig_fft.update_layout(
        height=320,
        xaxis=dict(title="Frequency (Hz)", range=[0, 100]),
        yaxis=dict(title="Spectral Magnitude (g)", range=[0, max(1.5, peak_g * 1.1)]),
        template="plotly_dark",
        margin=dict(l=20, r=20, t=20, b=20)
    )
    st.plotly_chart(fig_fft, use_container_width=True)

with row2_right:
    st.subheader("🌀 X-Y Shaft Orbital Lissajous Pattern")
    
    fig_orbit2d = go.Figure()
    fig_orbit2d.add_trace(go.Scatter(
        x=ac_x, y=ac_y,
        mode='lines',
        line=dict(color=orbit_color, width=2.5),
        name='Shaft Path'
    ))
    
    fig_orbit2d.update_layout(
        height=320,
        xaxis=dict(title="X Acceleration (g)", range=[-2.5, 2.5]),
        yaxis=dict(title="Y Acceleration (g)", range=[-2.5, 2.5], scaleanchor="x", scaleratio=1),
        template="plotly_dark",
        margin=dict(l=20, r=20, t=20, b=20)
    )
    st.plotly_chart(fig_orbit2d, use_container_width=True)