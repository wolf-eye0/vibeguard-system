# VibeGuard DSP Metrics, Kinematics & Instrumentation Guide
**Course: PBCST504 Microcontrollers | Project Review 1 Preparation**  
*Department of Computer Science & Engineering (Cyber Security)*

---

## 1. The Physical Mechanism: Why Does It Spin, Shake, and Go Up & Down?

### 1.1 The Source of Rotation
The system uses a **12V N20 DC micro metal gear motor**. When powered, the motor armature spins, and an internal miniature brass planetary/spur gearbox steps down the speed to a stable **600 RPM** (Rotations Per Minute).

### 1.2 The Source of Vibration: Unbalance Centrifugal Force
In a perfectly balanced machine, the center of mass aligns exactly with the center of rotation. No net radial force is created.
However, in our rig, we attach an **eccentric mass** ($m$) offset at radius ($r$) from the shaft center.

As the shaft spins at angular velocity $\omega$:
$$\omega = 2\pi f_0 = 2\pi \left(\frac{\text{RPM}}{60}\right)$$
At **600 RPM**:
$$f_0 = \frac{600}{60} = 10.0\text{ Hz}, \quad \omega = 2\pi(10.0) \approx 62.83\text{ rad/sec}$$

This eccentric mass experiences continuous circular acceleration, generating a **rotating centrifugal force vector ($F_c$)**:
$$F_c = m \cdot r \cdot \omega^2$$

### 1.3 Why Does the Sensor Go "Up and Down" and "Left and Right"?
Because the unbalance mass is spinning in a 2D circle perpendicular to the shaft:
- At **$0^\circ$**: The mass pulls horizontally to the right $\rightarrow$ Peak $+X$ acceleration.
- At **$90^\circ$**: The mass pulls vertically upwards $\rightarrow$ Peak $+Y$ acceleration.
- At **$180^\circ$**: The mass pulls horizontally to the left $\rightarrow$ Peak $-X$ acceleration.
- At **$270^\circ$**: The mass pulls vertically downwards $\rightarrow$ Peak $-Y$ acceleration.

Mathematically, the physical accelerations measured by the ADXL345 sensor are:
$$a_x(t) = A \cos(\omega t)$$
$$a_y(t) = A \sin(\omega t) = A \cos\left(\omega t - 90^\circ\right)$$

This is why you see smooth, oscillating waves going up and down. $a_x(t)$ and $a_y(t)$ are pure sinusoidal waves with a **$90^\circ$ mechanical phase shift**!

---

## 2. The 4 Interactive Slider Bars (Plant & Fault Controls)

When you look at the top control bar of the 3D Digital Twin, you have 4 interactive sliders:

### 🎚️ Slider 1: Motor Speed (`RPM`)
* **Range**: 300 RPM to 1200 RPM (Default: **600 RPM**).
* **Physical Meaning**: Controls the actual rotational speed of the motor shaft.
* **Why it matters**: 
  * Changes the **fundamental rotational frequency** $f_0 = \text{RPM} / 60$. At 600 RPM, $f_0 = 10\text{ Hz}$. At 1200 RPM, $f_0 = 20\text{ Hz}$.
  * Notice that the centrifugal force scales with the **square of speed** ($\omega^2$). If you double the RPM from 600 to 1200, the vibration force quadruples ($4\times$)!

### 🎚️ Slider 2: Eccentric Imbalance Mass (`Mass (g)`)
* **Range**: 0.00 g to 5.00 g (Default: **3.50 g**).
* **Physical Meaning**: Simulates adding or removing an off-center bolt/weight on the motor shaft coupling.
* **Why it matters**:
  * At **0.0 g**: The motor is perfectly balanced. The vibration drops to the baseline noise floor (Normal / Green LED).
  * At **3.5 g**: Severe dynamic unbalance is introduced, creating heavy physical shaking (Alarm / Red LED).

### 🎚️ Slider 3: Fault Severity (`Severity (%)`)
* **Range**: 0% to 100% (Default: **70%**).
* **Physical Meaning**: Represents the mechanical coupling misalignment and radial offset percentage.
* **Why it matters**: Acts as an overall multiplier on the injected mechanical fault vector, allowing smooth demonstration of the transition between **Normal ($<0.35g$) $\rightarrow$ Warning ($0.35-0.70g$) $\rightarrow$ Alarm ($>0.70g$)**.

### 🎚️ Slider 4: Sensor Noise Floor (`Noise (g)`)
* **Range**: 0.005 g to 0.080 g (Default: **0.020 g**).
* **Physical Meaning**: Simulates real-world environmental noise: ADC quantization jitter, bearing surface micro-roughness, and test-bench acoustic floor.
* **Why it matters**: Demonstrates to the teacher that your DSP algorithms (DC removal, Hann windowing, and persistence state filtering) can extract the true machine signal even in the presence of stochastic noise without false triggering.

---

## 3. The 6 Core Real-Time DSP Telemetry Measurements

On the digital telemetry HUD, the ESP32 calculates 6 real-time metrics every processing window (256 samples / 320 ms):

### 1️⃣ Vector RMS Acceleration (`Vector RMS` in $g$)
* **Formula**:
  $$a_{\text{RMS}} = \sqrt{\text{RMS}_x^2 + \text{RMS}_y^2 + \text{RMS}_z^2}$$
* **What it means**: The **Root Mean Square (RMS)** measures the true continuous kinetic vibration energy across all 3 spatial dimensions combined into a single Euclidean invariant.
* **Why not simple average?** A vibration wave goes positive and negative; its simple average is zero. RMS squares each sample (making all numbers positive), averages them, and takes the square root.
* **Health Thresholds (ISO 10816 adapted)**:
  * **$< 0.35g$**: **Normal (Green)** — Healthy operating machine.
  * **$0.35g - 0.70g$**: **Warning (Amber)** — Developing unbalance or wear.
  * **$> 0.70g$**: **Alarm (Red)** — Hazardous vibration requiring immediate shutdown.

### 2️⃣ Peak Acceleration (`Peak Accel` in $g$)
* **Formula**:
  $$a_{\text{peak}} = \max(|x_i|)$$
* **What it means**: The maximum instantaneous amplitude measured during the 256-sample window.
* **Why it matters**: While RMS measures overall continuous energy, Peak Acceleration detects sudden shocks, loose mounting bolts, or mechanical impacts that happen for just a fraction of a millisecond.

### 3️⃣ Crest Factor (`Crest Factor`)
* **Formula**:
  $$\text{CF} = \frac{a_{\text{peak}}}{a_{\text{RMS}}}$$
* **What it means**: The ratio between the sharpest spike and the average continuous energy.
* **Diagnostic value**:
  * For a smooth, pure sine wave (healthy motor unbalance), $\text{CF} \approx \sqrt{2} \approx 1.414$.
  * If a gear tooth is chipped or a bearing raceway is pitted, sharp spikes occur while RMS remains low, causing the Crest Factor to spike to **$3.0 - 6.0+$**. It is an early-warning indicator of mechanical impacts.

### 4️⃣ Kurtosis (`Kurtosis`)
* **Formula**:
  $$\text{Kurtosis} = \frac{\frac{1}{N}\sum_{i=1}^N (x_i - \bar{x})^4}{\sigma^4}$$
* **What it means**: A statistical measure of the "peakedness" or spikiness of the vibration distribution.
* **Diagnostic value**:
  * For Gaussian random background noise, Kurtosis is **$\approx 3.0$**.
  * A healthy spinning motor is close to 1.5–3.0.
  * When structural cracks or bearing faults develop, high-energy transient spikes drive Kurtosis well above **$4.0$**.

### 5️⃣ Dominant / Peak Frequency (`Dominant Freq` in Hz)
* **What it means**: The exact frequency in the vibration spectrum that contains the highest mechanical energy.
* **At 600 RPM**: The dominant peak appears at **$10.0\text{ Hz}$** ($1\text{X}$ fundamental).
* **Diagnostic value**: If the teacher asks *"How do you know it's unbalance and not gear mesh?"*, you answer:
  * **Mass unbalance** always occurs at **$1\text{X}$ motor speed ($10\text{ Hz}$)**.
  * **Shaft misalignment** produces strong **$2\text{X}$ ($20\text{ Hz}$)** harmonics.
  * **Gear mesh faults** occur at high frequencies (teeth count $\times$ RPM).

### 6️⃣ Velocity RMS (`Velocity RMS` in $\text{mm/s}$)
* **Formula**:
  $$v_{\text{RMS}} = \frac{a_{\text{RMS}} \cdot 9.81}{2\pi f_0} \times 1000$$
* **What it means**: Acceleration measures rate of change of speed; velocity measures physical speed of vibration displacement in millimeters per second ($\text{mm/s}$).
* **Why it matters**: Standard industrial vibration specifications (such as ISO 10816-1 and ISO 10816-3) define machine vibration severity zones in velocity ($\text{mm/s}$) because velocity correlates directly with fatigue stress in machine frames.

---

## 4. The 3 Visual Display Panels Explained

### 📈 Panel 1: The 3-Axis Live Oscilloscope (800 Hz)
* **What you are looking at**: A real-time **time-domain graph** showing raw acceleration signals versus time:
  * **Red Line**: Radial Axis ($X$)
  * **Green Line**: Tangential Axis ($Y$)
  * **Blue Line**: Axial Axis ($Z$)
* **Parameters**:
  * **Sampling Rate**: $800\text{ Hz}$ (one sample collected every $1.25\text{ ms}$).
  * **Window Size**: 256 samples ($320\text{ ms}$ of continuous observation).
  * **DC Removal**: Before display, the static $1.0g$ Earth gravity bias is subtracted via in-place zero-mean centering, so the waveforms oscillate cleanly around $0.0g$.
* **What the motion means**: You see the Red ($X$) and Green ($Y$) waves oscillating up and down with the Green curve lagging $90^\circ$ behind Red, reflecting the circular motion of the motor shaft.

---

### ⚡ Panel 2: Hann-Windowed FFT Spectrogram
* **What you are looking at**: A **frequency-domain graph**.
  * The **Horizontal X-axis** represents Frequency ($0\text{ Hz}$ to $100\text{ Hz}$).
  * The **Vertical Y-axis** represents Vibration Amplitude ($g$).
* **Mechanism (Cooley-Tukey Radix-2 FFT)**:
  * Time-domain signals show a jumble of overlapping waves. The Fast Fourier Transform mathematically decomposes that complex wave into individual pure frequency tones (spectral bins).
* **Why the Hann Window?**
  * When chopping a continuous signal into a 256-sample block, abrupt edges create artificial high-frequency noise called **spectral leakage**.
  * The **Hann window** smooths the start and end of each block to zero using $w(n) = 0.5 - 0.5\cos\left(\frac{2\pi n}{N-1}\right)$, making spectral peaks sharp and clear.
* **Harmonic Markers**:
  * **$1\text{X}$ Marker (Cyan)** at **$10.0\text{ Hz}$**: The fundamental rotational frequency.
  * **$2\text{X}$ Marker** at **$20.0\text{ Hz}$**: Secondary harmonic indicating misalignment or structural asymmetry.
  * **$3\text{X}$ Marker** at **$30.0\text{ Hz}$**: Tertiary harmonic.

---

### 🌀 Panel 3: The Lissajous Shaft Orbit Plotter
* **What you are looking at**: An **orbital trajectory plot** showing the 2D path traced by the center-line of the rotating motor shaft.
  * **Horizontal Axis**: $X$-axis acceleration.
  * **Vertical Axis**: $Y$-axis acceleration.
* **Why does it trace a loop?**
  * Because $X = \cos(\omega t)$ and $Y = \sin(\omega t)$, plotting $X$ on the horizontal axis and $Y$ on the vertical axis traces out a **circle**!
* **Diagnostic Interpretation**:
  1. **Circular Orbit**: Normal balanced shaft or isotropic radial stiffness (equal stiffness in X and Y).
  2. **Elliptical Orbit**: Unbalance combined with asymmetric mounting stiffness (the bracket is stiffer in the vertical direction than the horizontal direction).
  3. **Distorted / Figure-8 / Looping Orbit**: Indicates severe mechanical looseness, bearing race rubbing, or structural resonance.

---

## 5. Quick Reference Cheat-Sheet for Review Questions

| Examiner Question | Your Winning Answer |
| :--- | :--- |
| *"Why did you use 800 Hz ODR?"* | Nyquist-Shannon theorem requires $f_s \ge 2 f_{\max}$. Our motor operates at 600 RPM ($10\text{ Hz}$). Harmonics up to $10\text{X}$ reach $100\text{ Hz}$. Sampling at $800\text{ Hz}$ gives an effective Nyquist bandwidth of $400\text{ Hz}$, providing plenty of headroom without aliasing while remaining easily processable by the ESP32 in real time. |
| *"Why subtract DC before computing RMS?"* | Accelerometers measure both dynamic vibration and static Earth gravity ($1.0g$ down). If DC is retained, static gravity falsely inflates the RMS metric by $1.0g$. In-place DC zero-mean centering isolates pure AC vibration dynamics. |
| *"What is the $K\text{-out-of-}M$ persistence filter?"* | Transient bumps (like tapping the bench) could cause false alarms. Our firmware requires $K$ out of $M$ consecutive analysis windows (e.g., 4 out of 5 windows, or 1.6 seconds) to exceed threshold before asserting the Amber or Red state. |
| *"What does the Lissajous orbit show?"* | It plots $a_x(t)$ versus $a_y(t)$ to visualize shaft center-line orbit precession in 2D space. A clean ellipse confirms unbalance; loops or banana shapes indicate bearing looseness or rubs. |
