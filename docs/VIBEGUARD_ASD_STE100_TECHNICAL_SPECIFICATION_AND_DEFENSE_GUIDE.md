# VIBEGUARD SYSTEM: TECHNICAL SPECIFICATION AND JUDGE DEFENSE GUIDE
## Written in Controlled Engineering Language (ASD-STE100 Principles)

> **Document Identifier:** VG-SPEC-STE100-2026-REV3  
> **Standard Reference:** Simplified Technical English (ASD-STE100 Specification Guidelines)  
> **System Name:** VibeGuard — Autonomous Rotordynamic Vibration Monitoring System  
> **Document Purpose:** Complete technical manual, mathematical foundation, and defense guide for academic evaluators and industrial expo judges.  

---

## 1. Description of the System

### 1.1 General Overview
The VibeGuard system is a low-cost, edge-computing condition monitor for rotating equipment. The system measures three-axis vibration acceleration, calculates dynamic statistical properties in real time, and identifies mechanical unbalance faults.

The system executes all data processing directly on an onboard microcontroller. The system does not require connection to an external computer or cloud server to make safety trip decisions.

```
+-----------------------------------------------------------------------------------+
|                            VIBEGUARD SYSTEM TOPOLOGY                              |
+-----------------------------------------------------------------------------------+

 [ Rotating Machine Rig ]
       |
       | Mechanical Vibration
       v
 [ ADXL345 Accelerometer ] <--- 4-Wire SPI Bus @ 1.0 MHz ---> [ ESP32 Dual-Core MCU ]
                                (800 Hz Data Ready Interrupt)         |
                                                                      |-- Real-Time DSP (160 ms)
                                                                      |-- Quadrature Tare
                                                                      |-- Threshold Evaluation
                                                                      |-- Flash Memory Storage
                                                                      |
                   +--------------------------------------------------+
                   |                                                  |
                   v                                                  v
        [ Local Hardware Output ]                           [ Full-Duplex Bridge ]
        - GPIO 25: Green LED (Normal)                       - USB Serial @ 115200 Baud
        - GPIO 26: Blue LED (Calibration)                   - WebSocket Server (Port 8765)
        - GPIO 27: Red LED (Alarm Strobe)                   - 60 FPS Web Digital Twin
```

### 1.2 System Architecture
The system contains four primary sub-systems:
1. **Mechanical Testbed Rig:** Transparent acrylic chassis, DC micro gear motor, 3D-printed eccentric rotor arm, and three calibrated physical imbalance masses.
2. **Sensor Acquisition Chain:** Tri-axial capacitive MEMS accelerometer connected through a 4-wire high-speed Serial Peripheral Interface (SPI).
3. **Edge Processing Core:** 32-bit dual-core microcontroller running a deterministic Real-Time Operating System (FreeRTOS) with digital filtering and non-volatile flash storage.
4. **Supervisory Digital Twin:** Local web interface that shows real-time vibration trend charts, an animated 3D machine model, directional energy distributions, and configuration controls.

---

## 2. Technical Specifications

### 2.1 Mechanical and Electrical Data
| Parameter | Value | Unit / Specification |
| :--- | :--- | :--- |
| **Monitored Motor** | N20 Micro Metal Gear Motor | 12 V DC, 600 RPM nominal |
| **Motor Drive Voltage** | 12.0 | V DC (Galvanically Isolated Rail) |
| **Motor Drive Current** | 0.35 (nominal) to 1.20 (stall) | A |
| **Microcontroller Unit** | ESP32-WROOM-32E (7Semi DevKit) | 32-Bit Xtensa LX6 Dual Core @ 240 MHz |
| **Instruction RAM** | 520 | KB SRAM |
| **Non-Volatile Storage (Flash)** | 4 | MB SPI Flash (NVS Partition) |
| **Sensing Transducer** | Analog Devices ADXL345 | 3-Axis Digital MEMS Accelerometer |
| **Measurement Range** | $\pm 16.0$ | $g$ (Full Resolution Mode) |
| **Scale Factor** | 3.90625 ($1/256$) | $\text{mg} / \text{LSB}$ |
| **Output Data Rate (ODR)** | 800 | Hz |
| **Communication Interface** | 4-Wire SPI (Mode 2: CPOL=1, CPHA=0) | 1.0 MHz Bus Clock |
| **Analysis Window Size ($N$)** | 128 | Samples per axis |
| **Analysis Window Duration ($T_w$)**| 160.0 | Milliseconds |
| **Edge Processing Latency** | $< 3.0$ | Milliseconds per window |
| **Total Hardware Cost** | 1,973.98 | Indian Rupees (INR) (~$24 USD) |

### 2.2 Pin Allocation Table
| Microcontroller Pin | Interface Signal | Connected Device Pin | Function |
| :--- | :--- | :--- | :--- |
| **GPIO 21** | SPI Chip Select (`CS`) | ADXL345 `CS` | Device Selection (Active LOW) |
| **GPIO 18** | SPI Serial Clock (`SCK`) | ADXL345 `SCL` | 1.0 MHz Clock Signal |
| **GPIO 19** | SPI Master-In (`MISO`) | ADXL345 `SDO` | Serial Accelerometer Data Output |
| **GPIO 23** | SPI Master-Out (`MOSI`)| ADXL345 `SDA` | Register Address & Configuration |
| **GPIO 4** | External Interrupt 1 | ADXL345 `INT1` | 800 Hz Data Ready Trigger |
| **GPIO 25** | Digital Output | Green Anode of RGB LED | Normal Healthy Operating State |
| **GPIO 26** | Digital Output | Blue Anode of RGB LED | Calibration & Boot Strobe State |
| **GPIO 27** | Digital Output | Red Anode of RGB LED | Alarm Strobe & Warning Mix State |

---

## 3. Mathematical Foundations and Digital Signal Processing (DSP)

The microcontroller executes five mathematical stages in every 160-millisecond window.

```
                      EDGE DSP MATHEMATICAL FLOW
  +-----------------------------------------------------------------+
  | 1. RAW BUFFER CAPTURE: 128 Samples @ 800 Hz (X, Y, Z axes)      |
  +-----------------------------------------------------------------+
                                  |
                                  v
  +-----------------------------------------------------------------+
  | 2. DC OFFSET & GRAVITY REMOVAL: Dynamic zero-centering          |
  |    x_tilde[k] = x[k] - (1/N) * SUM(x[k])                        |
  +-----------------------------------------------------------------+
                                  |
                                  v
  +-----------------------------------------------------------------+
  | 3. TOTAL 3D VECTOR RMS: Energy quantification                   |
  |    V_raw = sqrt( RMS_x^2 + RMS_y^2 + RMS_z^2 )                  |
  +-----------------------------------------------------------------+
                                  |
                                  v
  +-----------------------------------------------------------------+
  | 4. QUADRATURE TARE DECOUPLING: Structural noise rejection       |
  |    V_net = sqrt( max( 0, V_raw^2 - V_floor^2 ) )                |
  +-----------------------------------------------------------------+
                                  |
                                  v
  +-----------------------------------------------------------------+
  | 5. CORRIDOR HYSTERESIS EVALUATION: Debounced state decision     |
  |    NORMAL (Green)  <-->  WARNING (Yellow)  <-->  ALARM (Red)    |
  +-----------------------------------------------------------------+
```

### 3.1 Sampling Theorem and Windowing
The sampling frequency is:
$$f_s = 800\,\text{Hz}, \quad \Delta t = \frac{1}{f_s} = 1.25\,\text{ms}$$
In accordance with the Nyquist-Shannon Sampling Theorem, the maximum resolvable vibration frequency without aliasing is:
$$f_{\text{Nyquist}} = \frac{f_s}{2} = 400\,\text{Hz}$$
The nominal rotational frequency of the test motor is:
$$f_{\text{rot}} = \frac{600\,\text{RPM}}{60} = 10.0\,\text{Hz}$$
Because $f_{\text{Nyquist}} = 400\,\text{Hz} = 40 \times f_{\text{rot}}$, the system captures the fundamental rotational imbalance ($1\times$), harmonic shaft orders ($2\times$ to $10\times$), and gear-mesh contact modes.

The window length is $N = 128$ samples:
$$T_w = N \cdot \Delta t = 128 \times 0.00125\,\text{s} = 0.160\,\text{s} = 160\,\text{ms}$$
A window of 160 ms provides 1.6 complete shaft revolutions per calculation cycle.

---

### 3.2 Discrete DC Offset and Gravity Removal
An accelerometer measures dynamic vibration and static Earth gravity ($1.0\,g = 9.81\,\text{m/s}^2$). When the sensor is tilted, gravitational components enter the measurement axes. To measure only dynamic vibration, the system calculates the arithmetic mean for each axis:
$$\bar{x} = \frac{1}{N}\sum_{k=0}^{N-1} x[k], \quad \bar{y} = \frac{1}{N}\sum_{k=0}^{N-1} y[k], \quad \bar{z} = \frac{1}{N}\sum_{k=0}^{N-1} z[k]$$
The system then calculates dynamic acceleration deviations:
$$\tilde{x}[k] = x[k] - \bar{x}, \quad \tilde{y}[k] = y[k] - \bar{y}, \quad \tilde{z}[k] = z[k] - \bar{z}$$
This operation removes the static DC offset and the gravity component.

---

### 3.3 Root-Mean-Square (RMS) and 3D Vector Energy
The Root-Mean-Square value represents the destructive kinetic energy of the vibration. The system computes the RMS for each axis:
$$\text{RMS}_x = \sqrt{\frac{1}{N}\sum_{k=0}^{N-1} \tilde{x}[k]^2}, \quad \text{RMS}_y = \sqrt{\frac{1}{N}\sum_{k=0}^{N-1} \tilde{y}[k]^2}, \quad \text{RMS}_z = \sqrt{\frac{1}{N}\sum_{k=0}^{N-1} \tilde{z}[k]^2}$$
The total spatial vibration energy is the Euclidean Vector RMS ($V_{\text{raw}}$):
$$V_{\text{raw}} = \sqrt{\text{RMS}_x^2 + \text{RMS}_y^2 + \text{RMS}_z^2}$$
$V_{\text{raw}}$ is independent of sensor orientation. If an operator mounts the sensor at an angle, the total spatial vector energy remains mathematically invariant.

---

### 3.4 Peak Acceleration and Crest Factor
The system finds the maximum dynamic acceleration peak in the window:
$$P_k = \tilde{x}[k]^2 + \tilde{y}[k]^2 + \tilde{z}[k]^2, \quad \text{Peak} = \sqrt{\max_{0 \le k < N} (P_k)}$$
The Crest Factor ($CF$) is:
$$CF = \frac{\text{Peak}}{V_{\text{raw}}}$$
* **Physical Significance:** Pure sinusoidal rotational imbalance exhibits a Crest Factor near $\sqrt{2} \approx 1.414$. Bearing impacts, loose bolts, and mechanical teeth faults produce impulsive acceleration spikes, which increase the Crest Factor above $3.0$.

---

### 3.5 Physics-Based Quadrature Tare (Environmental Noise Decoupling)
When a machine operates on a table, the total measured acceleration contains:
1. Desired machine signal: $a_{\text{machine}}(t)$
2. Undesired ambient environmental vibration: $a_{\text{env}}(t)$

Because the environmental noise floor and the machine vibration originate from independent physical sources, their statistical cross-correlation is zero:
$$\mathbb{E}\left[a_{\text{machine}}(t) \cdot a_{\text{env}}(t)\right] = 0$$
The expected total measured variance (energy) is the sum of the individual variances:
$$\sigma_{\text{total}}^2 = \sigma_{\text{machine}}^2 + \sigma_{\text{env}}^2 \implies V_{\text{raw}}^2 = V_{\text{net}}^2 + V_{\text{floor}}^2$$
Therefore, linear subtraction ($V_{\text{raw}} - V_{\text{floor}}$) is mathematically incorrect. The mathematically correct formulation for true machine vibration is **Quadrature Tare Subtraction**:
$$V_{\text{net}} = \sqrt{\max\left(0, \; V_{\text{raw}}^2 - V_{\text{floor}}^2\right)}$$

```
                   QUADRATURE VECTOR SUBTRACTION
                   
                    /|  V_raw (Measured Vector)
                   / |
                  /  |
         V_net   /   |  V_floor (Table Noise Floor)
      (Machine) /    |
               /     |
              +------+
                 90°
                 
              V_net = sqrt( V_raw^2 - V_floor^2 )
```

* **Operational Effect:** When the motor is turned OFF, $V_{\text{raw}} \approx V_{\text{floor}}$. The formula yields exactly **$0.00000\,g$**, which zeroes the baseline like an industrial weighing scale.

---

### 3.6 Centrifugal Imbalance Force Equation
Rotating mechanical mass unbalance creates an outward radial centrifugal force:
$$F_c = m_{\text{unbalance}} \cdot r \cdot \omega^2$$
Where:
* $m_{\text{unbalance}}$ = Unbalance mass ($0.0025\,\text{kg}$ for Stage 1, $0.0034\,\text{kg}$ for Stage 2).
* $r$ = Eccentric radius ($0.012\,\text{m}$ pitch on rotor arm FAB-01).
* $\omega$ = Rotational angular velocity ($\omega = 2\pi f = 2\pi \times 10.0\,\text{rad/s} \approx 62.83\,\text{rad/s}$).

**Theoretical Centrifugal Force Values:**
* **Stage 1 (Bolt Only):**
  $$F_{c1} = 0.0025\,\text{kg} \times 0.012\,\text{m} \times (62.83)^2 \approx 0.118\,\text{N}$$
* **Stage 2 (Bolt + Nut):**
  $$F_{c2} = 0.0034\,\text{kg} \times 0.012\,\text{m} \times (62.83)^2 \approx 0.161\,\text{N}$$

The dynamic acceleration measured at the sensor is proportional to this force:
$$a(t) = \frac{F_c}{M_{\text{effective}}} \sin(\omega t)$$
The system measures this sinusoidal acceleration increase and classifies the operating stage.

---

### 3.7 Single-Pass Streaming Accumulation (Welford Method)
During calibration, the system must compute the arithmetic mean across thousands of samples without overflowing memory. The microcontroller implements Welford’s single-pass algorithm ($O(1)$ memory complexity):
$$\mu_k = \mu_{k-1} + \frac{x_k - \mu_{k-1}}{k}$$
Where $k$ is the window counter, $x_k$ is the current window value, and $\mu_k$ is the updated running mean. The microcontroller computes the exact mathematical mean without storing raw arrays in memory.

---

### 3.8 Automatic Corridor and Hysteresis Calculations
The calibration suite records empirical values for Stage 0 ($V_0$), Stage 1 ($V_1$), and Stage 2 ($V_2$). The system calculates dynamic thresholds using midpoint corridors:
$$\text{Warn}_{\text{enter}} = V_0 + 0.50 \cdot (V_1 - V_0)$$
$$\text{Warn}_{\text{exit}}  = V_0 + 0.25 \cdot (V_1 - V_0)$$
$$\text{Alarm}_{\text{enter}} = V_1 + 0.50 \cdot (V_2 - V_1)$$
$$\text{Alarm}_{\text{exit}}  = V_1 + 0.25 \cdot (V_2 - V_1)$$

```
  VIBRATION ACCELERATION (g)
      ^
      |
      |   ALARM REGION (Red Strobe)
      +------------------------------------------ Alarm_enter (50% corridor)
      | - - - - - - - - - - - - - - - - - - - - - Alarm_exit  (25% corridor)
      |   WARNING REGION (Solid Yellow)
      +------------------------------------------ Warn_enter  (50% corridor)
      | - - - - - - - - - - - - - - - - - - - - - Warn_exit   (25% corridor)
      |   NORMAL HEALTHY REGION (Solid Green)
      +------------------------------------------ Stage 0 Baseline (V0)
      |
      +------------------------------------------ Tare Floor (0.00000 g)
      +--------------------------------------------------------------------> TIME
```

* **Hysteresis Band:** The physical difference between $\text{Warn}_{\text{enter}}$ and $\text{Warn}_{\text{exit}}$ prevents rapid relay chatter and optical LED flicker when the machine operates near threshold boundaries.

---

## 4. Mechanical Sub-System and Testbed Rig

### 4.1 Structure and Materials
* **Baseplate:** 5.0 mm thick laser-cut polymethyl methacrylate (acrylic sheet). Dimension: $250 \times 180\,\text{mm}$. Acrylic provides high internal damping and optical transparency for demonstration.
* **Motor Mount:** Stamped cold-rolled steel bracket bolted with M2 machine screws through vibration-damping synthetic washers.
* **Sensor Bracket (FAB-02):** Monolithic 90-degree bracket additively manufactured in 100% infill PETG. Features dual triangular gussets ($3.5\,\text{mm}$ upright flange, $4.0\,\text{mm}$ base flange) that shift structural resonant modes above $1.2\,\text{kHz}$, well beyond the $400\,\text{Hz}$ sensing window.

### 4.2 Calibrated Imbalance Masses
The system uses three repeatable unbalance test stages:
1. **Stage 0 (Bare Arm):** Rotor arm with zero added mass ($0.0\,\text{g}$). Represents a precision-balanced, healthy rotating machine.
2. **Stage 1 (Bolt Only):** Rotor arm with one standard M3 $\times 20\,\text{mm}$ zinc-plated steel bolt ($\text{Mass} = 2.48\,\text{g} \pm 0.05\,\text{g}$). Represents early-stage mass imbalance.
3. **Stage 2 (Bolt + Nut):** Rotor arm with M3 bolt and one M3 steel hex nut ($\text{Mass} = 3.39\,\text{g} \pm 0.05\,\text{g}$). Represents dangerous, severe imbalance requiring automated trip.

---

## 5. Electrical Safety and Power Distribution

```
                    POWER ISOLATION & SNUBBER SCHEMATIC
                    
     [ 12V 2A DC Adapter ]
              |
              +---[ Rocker Switch ]--->[ 1A Fuse ]--->+---------------------+
              |                                       |                     |
              |                                    +--+--+                  |
              |                                    |     | N20 Motor        |
              |                                    | (M) | (Inductive Load) |
              |                                    |     |                  |
              |                                    +--+--+                  |
              |                                       |   1N4007 Diode      |
              |                                       |   (Reverse Snubber) |
              |                                       |<---[-|<|--]+        |
              |                                       |            |        |
              +---------------------------------------+------------+--------+
              
              
     [ 5V USB Logic Rail ]
              |
              +--->[ ESP32 3.3V LDO Regulator ]--->[ 3.3V Clean Rail ]
                                                           |
                                                           +---> ADXL345 VDD
                                                           |     (Decoupled: 100uF + 100nF)
                                                           |
                                                           +---> Status RGB LED
```

1. **Galvanic Power Rail Separation:**
   * High-power inductive rail ($12\,\text{V}$ DC) supplies only the motor.
   * Low-power logic rail ($5\,\text{V}$ USB VBUS) supplies only the ESP32 and sensor.
   * Separate power rails prevent motor commutation voltage dips from causing microcontroller brownout resets.
2. **Inductive Snubber Diode:** A 1N4007 silicon diode is wired in reverse antiparallel across the motor terminals. When the motor turns off, the collapsing magnetic field discharges through the diode. This clamps dangerous reverse-EMF spikes below $1.1\,\text{V}$.
3. **Decoupling Capacitor Network:** A $100\,\mu\text{F}$ electrolytic capacitor (bulk energy storage) and a $100\,\text{nF}$ ceramic capacitor (high-frequency noise shunt) are installed on the $3.3\,\text{V}$ sensor rail.

---

## 6. Software Architecture and Digital Twin Interface

```
+-------------------------------------------------------------------------------+
|                       SOFTWARE & COMMUNICATION STACK                          |
+-------------------------------------------------------------------------------+

 [ Tier 1: Embedded Firmware Core (ESP32 C++) ]
   * 800 Hz Hardware Timer Interrupt Service Routine (ISR)
   * 128-Sample Circular Buffer with Hann Windowing
   * Fixed-Point & Single-Precision Vector Math
   * State Machine with Persistence Counters
   * Non-Volatile Flash Storage (Preferences API)
   * Serial Telemetry Formatter (Tagged Output @ 115200 Baud)
          |
          | USB Serial Data Stream (/dev/ttyUSB0)
          v
 [ Tier 2: Bridge Daemon (Node.js Express & WebSocket) ]
   * SerialPort Stream Parser (Newline Parser)
   * JSON Broadcast Dispatcher
   * WebSocket Server (ws://127.0.0.1:8765/ws)
   * Bi-Directional Command Interface (CAL_START, CAL_STOP, CAL_COMMIT, CAL_RESET)
          |
          | WebSocket JSON Packets
          v
 [ Tier 3: Supervisory Digital Twin (Vanilla HTML5 / ES6 Web App) ]
   * Real-Time Trend Graph (60 FPS HTML5 Canvas)
   * Directional Acceleration Energy Bars (X, Y, Z axes)
   * 3D Isometric Motor Rig Animated Twin
   * 4-Card Rapid Field Calibration Suite
   * Session Telemetry Exporter (CSV Download)
```

---

## 7. Operational Test Results and Verification

The system was evaluated across six hours of continuous testing on laboratory workbenches and classroom demonstration tables.

### 7.1 Quantitative Verification Table
| Measured Parameter | Motor OFF (Ambient) | Stage 0 (Bare Arm) | Stage 1 (Bolt Only) | Stage 2 (Bolt + Nut) |
| :--- | :---: | :---: | :---: | :---: |
| **Physical Added Mass** | $0.00\,\text{g}$ | $0.00\,\text{g}$ | $2.48\,\text{g}$ | $3.39\,\text{g}$ |
| **Raw Acceleration RMS ($V_{\text{raw}}$)** | $0.0129\,g$ | $0.2045\,g$ | $0.7812\,g$ | $1.0450\,g$ |
| **Tare Noise Floor ($V_{\text{floor}}$)** | $0.0129\,g$ | $0.0129\,g$ | $0.0129\,g$ | $0.0129\,g$ |
| **Net Calculated RMS ($V_{\text{net}}$)** | **$0.0000\,g$** | **$0.2041\,g$** | **$0.7811\,g$** | **$1.0449\,g$** |
| **Peak Acceleration** | $0.0280\,g$ | $0.4120\,g$ | $1.5200\,g$ | $2.3800\,g$ |
| **Crest Factor ($CF$)** | $2.17$ | $2.01$ | $1.95$ | $2.28$ |
| **Dominant Axis** | Z (Vertical) | Z (Vertical) | Y (Radial Shear) | Y (Radial Shear) |
| **Evaluated Condition** | **NORMAL** | **NORMAL** | **WARNING** | **ALARM** |
| **Physical RGB LED** | **Solid Green** | **Solid Green** | **Solid Yellow** | **Red Strobe (6.25 Hz)** |

### 7.2 Key Observations
1. **Dynamic Separation:** Net vibration increases from $0.2041\,g$ (Normal) to $0.7811\,g$ (Warning) and $1.0449\,g$ (Alarm). This provides clear $300\%$ signal separation between stages, completely eliminating boundary ambiguity.
2. **Zero False Alarms:** During motor-off conditions, table noise floor ($0.0129\,g$) was subtracted in quadrature, resulting in a flat $0.00000\,g$ baseline.
3. **Execution Determinism:** The DSP pipeline completed in $2.84\,\text{ms}$, well within the $160.0\,\text{ms}$ window budget.

---

## 8. Target Applications and Industrial Value

```
+-------------------------------------------------------------------------------+
|                       PRIMARY TARGET APPLICATIONS                             |
+-------------------------------------------------------------------------------+

 [ Small & Medium Enterprise ]    [ Power & Heavy Industry ]    [ Commercial Facilities ]
 - Machine Shop CNC Spindles     - Boiler Induced-Draft Fans   - Commercial HVAC Chillers
 - Conveyor Belt Drive Motors    - Centrifugal Water Pumps     - Cooling Tower Blowers
 - Woodworking Saw Mills         - Slurry Agitators            - Water Supply Booster Pumps
 - Agricultural Grain Mills      - Rotary Air Compressors      - Wastewater Aerator Drives
```

### 8.1 Industrial Pain Points Solved
1. **Affordable Protection for Unmonitored Assets:** Tier-2 and Tier-3 auxiliary machinery (fans, pumps, gearboxes) often run without vibration monitoring because commercial sensor systems cost more than the machine itself. VibeGuard provides continuous monitoring at 4% of industrial cost.
2. **Immunity to Resonant Mountings:** Machines installed on light steel frames or hollow concrete floors experience structural resonance. VibeGuard’s Quadrature Tare zeroes out mounting resonance without requiring recalibration of the baseline algorithm.
3. **True Edge Autonomy:** The system triggers protective relays directly from GPIO outputs without requiring local area network (LAN) infrastructure, cloud subscriptions, or PLC modules.

---

## 9. Economic Comparison: VibeGuard vs. Commercial Systems

| Feature / Metric | Commercial Industrial Sensor (e.g. SKF / Bently Nevada) | Generic IoT Vibration Node (Cloud-Dependent) | VibeGuard System (This Project) |
| :--- | :--- | :--- | :--- |
| **System Cost per Node** | ₹50,000 to ₹2,50,000 ($600 – $3,000) | ₹8,000 to ₹20,000 ($100 – $250) | **₹1,974 (~$24)** |
| **Sensing Technology** | Piezoelectric Crystal Transducer | Analog / I2C MEMS Breakout | **High-Speed SPI 3-Axis MEMS** |
| **Sampling Frequency** | 10 kHz to 50 kHz | 50 Hz to 100 Hz (Slow) | **800 Hz (Nyquist: 400 Hz)** |
| **Edge Decision Time** | Sub-millisecond (via rack PLC) | 2 to 10 Seconds (Cloud Latency) | **< 3 Milliseconds (Onboard)** |
| **Mounting Noise Subtraction**| Manual baseline offsets in software | None (Causes False Alarms) | **Physics-Based Quadrature Tare**|
| **Field Commissioning** | Requires certified vibration engineer | Fixed software thresholds | **15-Second Interactive Suite** |
| **Network Dependence** | Proprietary RS-485 / Industrial Fieldbus | Active Internet Required | **Zero Network Needed for Safety** |
| **Data Visualization** | Expensive Enterprise Desktop Software | Mobile App / Cloud Dashboard | **Zero-Dependency Web Digital Twin**|

---

## 10. Judge Defense Guide: Questions, Technical Answers, and Proofs

This section provides exact technical answers for evaluation committee questions.

---

### Question 1: "Why did you choose the ADXL345 MEMS sensor instead of an industrial piezoelectric accelerometer?"
> **Verbal Answer for Team:**  
> "Industrial piezoelectric accelerometers require high-voltage constant-current excitation circuits (IEPE/ICP at 24V), charge amplifiers, and high-resolution ADCs, which raises the cost above ₹50,000 per axis. The ADXL345 provides 13-bit full-resolution measurement with an internal analog-to-digital converter, user-selectable $\pm 16\,g$ range, and a dedicated 4-wire SPI interface at 1.0 MHz. Operating at an 800 Hz output data rate, its measurement bandwidth ($400\,\text{Hz}$) easily covers the fundamental rotational frequencies ($10\,\text{Hz}$ to $60\,\text{Hz}$) and first 10 harmonic orders of industrial machinery. It achieves this performance at less than 1% of the cost of a piezoelectric sensor."

---

### Question 2: "What is Quadrature Tare, and why is it better than simple linear subtraction?"
> **Verbal Answer for Team:**  
> "When a motor sits on an imperfect table, the vibration of the table ($V_{\text{floor}}$) and the motor ($V_{\text{machine}}$) are statistically uncorrelated physical noise sources. In physics, uncorrelated random vectors add orthogonally in quadrature:
> $$V_{\text{total}}^2 = V_{\text{machine}}^2 + V_{\text{floor}}^2$$
> If you simply subtract them linearly ($V_{\text{raw}} - V_{\text{floor}}$), you violate root-mean-square energy conservation and calculate an incorrect value. VibeGuard implements the true orthogonal difference:
> $$V_{\text{net}} = \sqrt{\max\left(0, \; V_{\text{raw}}^2 - V_{\text{floor}}^2\right)}$$
> When the motor is turned off, this formula brings the baseline reading to exactly $0.00000\,g$, preventing table resonance from creating false warning alarms."

---

### Question 3: "Why is your SPI bus configured to Mode 2 instead of Mode 0?"
> **Verbal Answer for Team:**  
> "The Analog Devices ADXL345 datasheet specifies SPI communication with Clock Polarity ($\text{CPOL} = 1$) and Clock Phase ($\text{CPHA} = 0$), which corresponds to SPI Mode 2. In this mode, the clock line remains HIGH during idle, and data is latched on the falling edge of the clock signal. Configuring the ESP32 hardware SPI peripheral to Mode 2 guarantees reliable data transfer at 1.0 MHz without dropped bits or bus contention."

---

### Question 4: "How does the system prevent false alarms caused by physical bumps or someone touching the table?"
> **Verbal Answer for Team:**  
> "We implement a dual-layer defense filter:
> 1. **Crest Factor Peak Validation:** Sudden physical shocks produce a high peak-to-RMS ratio ($\text{Crest Factor} > 3.5$). These transient spikes are identified as structural shock rather than rotational unbalance.
> 2. **State Persistence Debounce Counters:** A condition transition from Normal to Warning requires **three consecutive windows** ($\sim 480\,\text{ms}$) above the threshold. A transition from Warning to Alarm requires **two consecutive windows** ($\sim 320\,\text{ms}$). A single momentary bump only lasts 50 to 100 ms and is rejected by the persistence counter."

---

### Question 5: "How does the calibration persist when you disconnect the rig and take it to another room?"
> **Verbal Answer for Team:**  
> "The ESP32 microcontroller features dedicated Non-Volatile Storage (NVS) inside its 4 MB flash memory partition. When the operator clicks 'Save Thresholds to Flash', the firmware uses the Arduino `Preferences` API to write the calibrated values (`tare_floor`, `warning_enter`, `alarm_enter`, and stage references) directly into persistent flash sectors. During boot-up, the `loadProfile()` function automatically restores these values from flash memory into active RAM. The system retains complete calibration without needing an active computer connection."

---

### Question 6: "Why did you use 4-wire SPI instead of standard I2C for the accelerometer?"
> **Verbal Answer for Team:**  
> "I2C operates at a maximum standard clock speed of 400 kHz and requires open-drain pull-up resistors. In high-vibration industrial environments, electromagnetic interference (EMI) from motor brushes causes I2C bus lockups that halt the microcontroller. In contrast, 4-wire SPI operates at 1.0 MHz with push-pull CMOS driving and dedicated Chip Select lines, offering $2.5\times$ faster throughput and high noise immunity."

---

### Question 7: "What standard guidelines did you follow for Warning and Alarm limits?"
> **Verbal Answer for Team:**  
> "Our threshold logic is structured around **ISO 20816-1 / ISO 10816-3 (Mechanical Vibration — Evaluation of Machine Vibration by Measurements on Non-Rotating Parts)**. The standard divides machine operating states into four zones:
> * **Zone A/B (Healthy / Acceptable):** Represented by our Stage 0 (Bare Arm, Green LED).
> * **Zone C (Unrestricted Long-Term Operation Restricted / Warning):** Represented by our Stage 1 (Bolt Only, Yellow LED).
> * **Zone D (Danger / Immediate Damage Likely / Alarm):** Represented by our Stage 2 (Bolt + Nut, Red Strobe LED).
> Our field calibration suite computes exact midpoint corridors between these zones."

---

## 11. Quick Demonstration Script for the Expo Booth

Follow this 60-second live demonstration sequence when presenting to judges:

1. **Step 1: Baseline Normal Demonstration (15 Seconds)**
   * Turn the motor ON with the **Bare Arm (Stage 0)**.
   * Point to the dashboard: *“Here you see Vector RMS at $\sim 0.20\,g$. The physical LED is Solid Green, confirming ISO Zone A healthy operation.”*
2. **Step 2: Warning Fault Injection (15 Seconds)**
   * Turn motor OFF $\to$ Attach the **M3 Bolt (~2.5g, Stage 1)** $\to$ Turn motor ON.
   * Point to the dashboard and rig: *“Notice the vibration immediately increases to $0.78\,g$. After the 3-window persistence check, the physical LED turns Solid Yellow, and the web twin logs an Imbalance Warning.”*
3. **Step 3: Alarm Trip Injection (15 Seconds)**
   * Turn motor OFF $\to$ Add the **M3 Nut (~3.4g total, Stage 2)** $\to$ Turn motor ON.
   * Show the response: *“With severe unbalance, vibration crosses $1.04\,g$. The physical LED immediately transitions to a high-visibility 6.25 Hz Red Strobe, indicating an emergency shutdown condition.”*
4. **Step 4: Show Zero Tare Noise Floor (15 Seconds)**
   * Turn the motor OFF.
   * Show the dashboard trend graph: *“Notice that even though this table has ambient room vibration, our Quadrature Tare formula decouples the noise floor, bringing the graph to exactly $0.00000\,g$.”*

---

*End of Technical Specification and Defense Guide.*  
*Authored for VibeGuard Engineering Team Defense (2026).*
