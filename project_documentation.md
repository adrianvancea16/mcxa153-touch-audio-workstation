# Voice Synthesizer, SD Recorder & Wi-Fi Audio Workstation (FRDM-MCXA153)

> Documentation draft generated from Agent 0, Agent 1, and Milestone 2 updates.
> AI assists. Humans decide.

## 1. Introduction

This project details the design and implementation of a portable, three-mode embedded audio workstation built on the mandatory **NXP FRDM-MCXA153** development platform. The system integrates real-time digital signal processing (DSP), bare-metal SD card file handling, and a wireless local web server, navigated entirely through 3 hardware tactile buttons (the LCD module's resistive touch controller is present on the display hardware but is **not** wired up or used by the firmware).

The primary objective is to turn a raw MCU platform into a voice-processing instrument. Audio is sampled from a preamplified microphone (MAX4466) via the LPADC peripheral, timed by a SysTick-derived 16kHz trigger. The Cortex-M33 core computes a real-time 256-point Fast Fourier Transform using a small self-contained implementation (no external DSP library), rendering a frequency spectrum bar graph with peak-hold on a 2.4-inch ILI9341 SPI TFT LCD.

The workstation operates in three main firmware modes, selectable via 3 hardware tactile buttons (P2_2, P3_13, P3_14):
1. **Synthesizer Mode:** Real-time playback in headphones of the sound captured by the microphone, with a selectable voice effect (warm low-pass, "phone" high-pass, or ring-modulated "robot"), alongside a live FFT spectrum display.
2. **SD Card Recorder & Player Mode:** Checks whether an SD Card is actually mounted — recording is only possible with a real, detected card. Records audio into WAV files, lists recordings on-screen, and plays them back through headphones (applying the same voice effects during playback is a planned next step, not yet wired in — see section 14).
3. **ESP Wi-Fi Server Mode:** ESP8266 acts as a local Wi-Fi access point + a small web server, serving live voice level/spectrum status and a list of recordings; serving the recorded WAV files themselves over Wi-Fi is implemented but may not be practical for larger files given the ESP8266 AT-command link's throughput.

There is no hardware audio DAC on this MCU (confirmed against the vendor register headers) — headphone output is a software-generated 1-bit pulse-density bitstream on a GPIO pin, cleaned up by an external RC low-pass filter before reaching the 3.5mm jack.

The entire setup operates smoothly via standard USB power.

---

## 2. General Description

### 2.1 Project Summary

- **Project Name:** Voice Synthesizer, SD Recorder & Wi-Fi Audio Workstation
- **Short Summary:** A three-mode embedded audio workstation on FRDM-MCXA153: a live voice synthesizer with real-time FFT spectrum display and selectable voice effects, an SD-card WAV recorder/player gated on real card detection, and an ESP8266 Wi-Fi server exposing live status and recordings — all navigated with 3 buttons, all audio heard through a 3.5mm headphone jack driven by a software PDM signal (this MCU has no hardware DAC).
- **Main Objective:** Implement real-time voice capture and effects, live FFT visualization, bare-metal SD card file management, button-driven UI, and a wireless local web server on a single MCU platform.
- **Intended Users:** Third-year Computer Science students, electronics hobbyists, and laboratory researchers.
- **Operating Environment:** Benchtop laboratory or portable handheld.
- **Selected Scope:** Recommended Summer School Version (with Core baseline and Advanced Wi-Fi extension).
- **Main Behavior:** LPADC sampling audio at 16 kHz (SysTick-timed), computing a 256-point FFT in software, rendering a spectrum bar graph on the 2.4" SPI TFT LCD, managing FatFS SD Card recording/playback, applying selectable voice effects, driving a software PDM headphone output, and handling 3-button UI input.
- **Inputs:** MAX4466 mic module, 3 Tactile Buttons (P2_2, P3_13, P3_14).
- **Outputs:** 2.4" ILI9341 SPI TFT LCD display, software-PDM 3.5mm headphone jack output (RC-filtered, no hardware DAC), ESP8266 UART Wi-Fi stream.
- **Out-of-Scope Items:** 24-bit multi-track studio recording, high-power speaker driving above 1W, un-isolated 5V logic connections, touchscreen-driven interaction (hardware present, not used by firmware).

### 2.2 Feature Tiers

| Tier | Description | Main Features | Extra Components | Main Risks | Suitability |
|---|---|---|---|---|---|
| Core | Voice Synthesizer with Live FFT | LPADC sampling (16kHz), 256-pt self-contained FFT, dB-scaled spectrum bar render with peak-hold on TFT LCD, selectable voice effects, headphone monitoring | MAX4466 Mic, 2.4" ILI9341 SPI TFT LCD, buttons, RC output filter | ADC/output timing must stay ISR-driven so LCD drawing can never stall audio; a 1-bit PDM output needs real analog filtering to sound clean | Suitable (Beginner/Intermediate) |
| Recommended | SD Card Recorder & Player | SD card mount-gated recording, WAV files via FatFS, on-screen file browser, playback through headphones | 3.5mm jack breakout, 1k resistor, ceramic capacitor (RC filter) | Bit-banged SPI shared with the LCD bus; no card-detect pin, so "present" = mount succeeded | Suitable (Intermediate) |
| Advanced | ESP8266 Wi-Fi Server | ESP8266 AT-command driver, SoftAP + small HTTP server, live status/FFT JSON, file listing, file download | ESP8266 Wi-Fi Module | Peak current draw causing MCU voltage dips; large file downloads may be impractical over the AT-command UART link | Suitable (Advanced Extension) |

### 2.3 Scenarios

| ID | Scenario | Description |
|---|---|---|
| SC-001 | System Startup | MCU powers up, initializes LPADC, LPSPI/bit-banged SPI, LPUART2, and displays the Synthesizer screen by default. |
| SC-002 | Synthesizer Mode | MCU samples mic audio at 16 kHz, computes a 256-pt FFT, renders a spectrum bar graph with peak-hold, and plays the (optionally effected) live signal through headphones. |
| SC-003 | SD Recording | User presses REC in SD Recorder mode; if a card is mounted, MCU streams 16-bit PCM WAV audio to the SD Card over bit-banged SPI + FatFS. If no card is mounted, recording is refused. |
| SC-004 | SD Playback | User presses PLAY in SD Recorder mode; MCU streams a recorded WAV file back from the SD card through headphones. |
| SC-005 | Wi-Fi Telemetry | ESP8266 acts as a SoftAP + local web server, serving live status/spectrum data and a recordings list to a browser; individual recordings can be downloaded, bandwidth permitting. |

### 2.4 User Stories

| ID | User Story |
|---|---|
| US-001 | As a student, I need real-time FFT spectrum visualization so that I can analyze my voice's frequencies visually while I speak. |
| US-002 | As a user, I need simple button navigation so that I can switch between Synthesizer, SD Recorder, and Wi-Fi Server modes easily. |
| US-003 | As a user, I need SD Card recording, gated on a real card being present, so that I can capture voice clips bare-metal. |
| US-004 | As a user, I need headphone output so that I can hear my own voice live, with optional effects, and hear back what I've recorded. |

### 2.5 Use Case Diagram

```mermaid
flowchart LR
    Student[Student User]
    Instructor[Instructor / Evaluator]

    UC1((Initialize Workstation))
    UC2((View Live FFT Spectrum))
    UC3((Record Voice to SD))
    UC4((Play Back Recording w/ Effects))
    UC5((Check Wi-Fi Status Page))
    UC6((Review Code & Reports))

    Student --> UC1
    Student --> UC2
    Student --> UC3
    Student --> UC4
    Student --> UC5
    Instructor --> UC6
```

### 2.6 Hardware and Software Block Diagram

```mermaid
flowchart TD
    subgraph PowerSystem["Power Management"]
        USB[USB Power 5V] --> REG[MCU Onboard 3.3V LDO Regulator]
    end

    subgraph MCUCore["NXP FRDM-MCXA153 Main Controller"]
        ADC[LPADC0]
        PDM[SysTick Software PDM Output]
        SPI[Bit-Banged SPI]
        UART2[LPUART2]
        FFT[Self-Contained 256-pt FFT]
    end

    subgraph Inputs["Analog & User Inputs"]
        MIC[MAX4466 Mic Module] --> ADC
        BTN1["Button 1: P2_2 (Mode)"] --> MCUCore
        BTN2["Button 2: P3_13 (Action 1)"] --> MCUCore
        BTN3["Button 3: P3_14 (Action 2)"] --> MCUCore
    end

    subgraph DisplayStorage["Display & Storage (shared SPI bus)"]
        TFT[2.4 inch ILI9341 TFT LCD] <--> SPI
        SD[SD Card Slot FatFS] <--> SPI
    end

    subgraph OutputsComms["Outputs & Wireless"]
        PDM --> RC[RC Low-Pass Filter] --> JACK[3.5mm Headphone Jack]
        UART2 <--> ESP[ESP8266 Wi-Fi Module]
    end

    REG --> MCUCore
    REG --> DisplayStorage
    REG --> OutputsComms
```

- **Power Flow:** The entire system is powered via USB. The onboard regulator distributes clean 3.3V power to the MCXA153, display, microphone, and ESP8266 module.
- **Data/Control Flow:** Audio is sampled by LPADC0 on a SysTick-derived timer, processed and FFT'd in software, and rendered over bit-banged SPI to the ILI9341 LCD. Buttons are used to switch modes and trigger REC/PLAY/filter actions.
- **Protection Needs:** A decoupling capacitor (100nF) is placed across the microphone power lines. Since there is no hardware DAC, the audio-out GPIO pin needs an external RC low-pass filter (not just a series resistor) to turn its 1-bit bitstream into a listenable analog signal.

---

## 3. Hardware Design

### 3.1 Bill of Materials

| # | Component | Qty | Tier | Purpose | Likely Interface | Voltage / Power Notes | Risks / Checks |
|---|---|---|---|---|---|---|---|
| 1 | NXP FRDM-MCXA153 | 1 | Core | Main MCU Controller | Onboard | 3.3V VCC, USB Powered | Mandatory Board |
| 2 | 2.4" ILI9341 SPI TFT LCD + Touch + SD | 1 | Core | Display, SD Storage (touch controller present on the module but unused by firmware) | Bit-banged SPI + GPIO CS | 3.3V Logic & VCC | Shared SPI CS management |
| 3 | MAX4466 Microphone Module | 1 | Core | Acoustic Audio Capture | LPADC Analog Input | 3.3V VCC, ~1.65V DC Bias | Decoupling cap required |
| 4 | 3.5mm Stereo Audio Jack Breakout | 1 | Recommended | Headphone Audio Out | Software PDM (GPIO) + RC filter | 0-3.3V swing, filtered | No DAC on this MCU — needs a real RC low-pass, not just a resistor |
| 5 | ESP8266 Wi-Fi Module | 1 | Advanced | Wireless Telemetry & Web UI | LPUART2 (115200 baud) | 3.3V VCC, up to 300mA peak | Decoupling capacitors on power line |
| 6 | Tactile Buttons | 3 | Core | Mode Navigation / Actions | GPIO Input | 3.3V Pullup | Debounce filtering needed |
| 7 | 1kΩ Resistor | 1 | Recommended | PDM output RC filter (series element) | In-series | Forms low-pass with capacitor below | Alone, does not filter anything |
| 8 | 100nF Ceramic Capacitor | 2 | Core | Mic decoupling (1) + PDM output RC filter to GND (1) | Parallel | Decoupling / filtering | Needed on both mic power and audio output |

### 3.2 Hardware Block Diagram Description

The hardware centers around the **NXP FRDM-MCXA153** development board. Acoustic audio signals are acquired by the LPADC peripheral. Visual output is provided by a 2.4-inch ILI9341 SPI TFT display sharing a bit-banged SPI bus with the SD Card socket (the module's resistive touch controller is present but not wired into firmware). Audio playback is a software-generated 1-bit PDM bitstream on a GPIO pin, cleaned up by an external RC low-pass filter (resistor + capacitor) before reaching the 3.5mm jack — this MCU has no hardware audio DAC. Wireless communication is handled by an ESP8266 Wi-Fi module over LPUART2. The entire circuit operates on the MCU's USB power supply.

### 3.3 Pin Allocation Draft

| Component | Tier | Signal | Required MCU Capability | Suggested Pin / Capability | Voltage Level | Direction | Interface | Verification Needed |
|---|---|---|---|---|---|---|---|---|
| MAX4466 Mic | Core | Audio Out | LPADC Analog Input | NXP P1_10 (ADC0, channel 8) | 0 - 3.3V | Input | Analog | Decoupling verified |
| ILI9341 TFT | Core | SPI SCK | Bit-banged GPIO | NXP P2_12 | 3.3V | Output | SPI (bit-banged) | Clock rate check |
| ILI9341 TFT | Core | SPI MOSI | Bit-banged GPIO | NXP P2_13 | 3.3V | Output | SPI (bit-banged) | Data output |
| SD Card Slot | Recommended | SPI MISO | Bit-banged GPIO (input) | NXP P2_16 | 3.3V | Input | SPI (bit-banged) | Needed for SD reads, LCD is write-only |
| ILI9341 TFT | Core | CS / DC / RST | GPIO Output | NXP P2_6 / P3_0 / P2_5 | 3.3V | Output | GPIO | Pinmux verified |
| SD Card Slot | Recommended | SD_CS | GPIO Output | NXP P1_3 | 3.3V | Output | SPI CS | No card-detect pin — presence = mount success |
| 3.5mm Jack | Recommended | Audio Out | Software PDM (GPIO) | NXP P3_12 | 0 - 3.3V | Output | GPIO + external RC filter | **No hardware DAC on this MCU** — verified against register headers |
| ESP8266 Wi-Fi | Advanced | TXD / RXD | LPUART RX / TX | NXP P1_4 / P1_5 (LPUART2) | 3.3V | Bidirectional | UART | Baud rate 115200; exact PORT mux ALT index not yet confirmed on hardware |
| Button 1 | Core | Mode Nav | GPIO Input | NXP P2_2 (INPUT_PULLUP) | 3.3V | Input | GPIO | Pullup check |
| Button 2 | Core | Action 1 | GPIO Input | NXP P3_13 (INPUT_PULLUP) | 3.3V | Input | GPIO | Pullup check |
| Button 3 | Core | Action 2 | GPIO Input | NXP P3_14 (INPUT_PULLUP) | 3.3V | Input | GPIO | Pullup check |

---

### 3.4 Electrical Schematics (Milestone 2)

The project schematic details the connections between the FRDM-MCXA153 development board and all the peripheral breakout modules. Note: the schematic image predates the audio-output corrections below (it may still show a DAC-style connection); the wiring described in section 3.6.4 is the current, tested one.

#### Project Electrical Schematic
![Electrical Schematic](./photos/circuit_image.svg)

---

### 3.5 Assembly and Photos (Milestone 2)

The physical breadboard assembly integrates all components listed in the BOM.

#### Component Photos & Breadboard Assembly
![Assembly Photo 1](./photos/Poza1_proiect.jpeg)

![Assembly Photo 2](./photos/Poza2_proiect.jpeg)

---

### 3.6 Detailed Pin Connections & Wiring (Milestone 2)

This section maps the physical wiring connections implemented on the breadboard.

#### 1. Alimentarea Generală (Breadboard Power Rails)
Această secțiune distribuie curentul de la microcontroler către toate componentele.
* **NXP 3V3_OUT** $\rightarrow$ Linia de alimentare Roșie (+) a breadboard-ului (3.3V)
* **NXP GND** $\rightarrow$ Linia de alimentare Albastră (-) a breadboard-ului (GND)

#### 2. Modulul Ecran ILI9341 (Display TFT + Touchscreen neutilizat + Card SD)
Toate cele trei funcții hardware (Video, Touch, SD) folosesc magistrala SPI comună (bit-banged din firmware, nu periferic hardware), dar au pini dedicați de selecție (Chip Select). Touchscreen-ul fizic există pe modul dar **nu e citit de firmware** — navigarea se face doar din cele 3 butoane.
* **Alimentare și Control de bază:**
  * **VCC** $\rightarrow$ Linia de 3.3V
  * **GND** $\rightarrow$ Linia de GND
  * **LED (Backlight)** $\rightarrow$ Linia de 3.3V
* **Magistrala SPI Comună (Date și Ceas):**
  * **SCK / SD_SCK** $\rightarrow$ **NXP P2_12**
  * **SDI (MOSI) / SD_MOSI** $\rightarrow$ **NXP P2_13**
  * **SDO (MISO) / SD_MISO** $\rightarrow$ **NXP P2_16** (necesar doar pentru cardul SD — ecranul e write-only)
* **Pini de Control (Chip Select):**
  * **CS (Display Chip Select)** $\rightarrow$ **NXP P2_6**
  * **D/C (Data/Command)** $\rightarrow$ **NXP P3_0**
  * **RESET (Display Reset)** $\rightarrow$ **NXP P2_5**
  * **SD_CS (SD Card Chip Select)** $\rightarrow$ **NXP P1_3**

#### 3. Modulul Microfon MAX4466 (Intrare Audio)
Semnalul analogic este preluat de convertorul ADC al plăcii.
* **VCC** $\rightarrow$ Linia de 3.3V
* **GND** $\rightarrow$ Linia de GND
* **OUT** $\rightarrow$ **NXP P1_10** (Canal ADC0)
* *Notă hardware:* Un condensator ceramic de 100 nF (marcat 104) este conectat în paralel între pinii VCC și GND ai microfonului pentru decuplarea și filtrarea zgomotului de pe alimentare.

#### 4. Mufa Audio Jack 3.5mm (Ieșire Audio Căști) — **fără DAC hardware**
Cipul MCXA153 nu are un DAC audio real (verificat direct în header-ele de registre ale producătorului). Ieșirea audio e un bitstream digital de 1 bit generat de firmware (PDM software) pe un pin GPIO, curățat printr-un filtru RC extern înainte să ajungă la căști.
* **Sleeve (Pinul central / Masă)** $\rightarrow$ Linia de GND
* **Tip & Ring (Pinii laterali / Stânga și Dreapta)** $\rightarrow$ Conectați împreună fizic pe breadboard, formând nodul de semnal.
* **Filtru RC (obligatoriu, nu opțional):**
  * **NXP P3_12** (ieșire GPIO/PDM) $\rightarrow$ un rezistor de 1 kΩ $\rightarrow$ nodul Tip/Ring
  * Din același nod Tip/Ring, un **condensator ceramic (100nF sau similar)** $\rightarrow$ GND
  * Rezistorul și condensatorul împreună formează filtrul trece-jos — un rezistor singur, fără condensator, **nu filtrează nimic** și lasă să treacă zgomotul brut de comutație (testat și confirmat pe hardware real în cadrul acestui proiect).

#### 5. Modulul Wi-Fi ESP8266 (Control Web / Telemetrie)
Comunicarea se realizează bidirecțional prin interfața UART (Serial).
* **3V3 / VCC** $\rightarrow$ Linia de 3.3V
* **EN / CH_PD** $\rightarrow$ Linia de 3.3V (Obligatoriu pentru activarea modulului)
* **GND** $\rightarrow$ Linia de GND
* **TXD (ESP Transmit)** $\rightarrow$ **NXP P1_4** (LPUART2_RXD - NXP Receive)
* **RXD (ESP Receive)** $\rightarrow$ **NXP P1_5** (LPUART2_TXD - NXP Transmit)

#### 6. Interfața cu Utilizatorul (Butoane Tactile)
Butoanele folosesc rezistențele interne Pull-Up ale microcontrolerului pentru navigarea între moduri și acțiuni.
* **Buton 1 (Schimbă modul: Synthesizer → SD Recorder → WiFi Server → ...):**
  * **Pin 1** $\rightarrow$ Linia de GND
  * **Pin 2** $\rightarrow$ **NXP P2_2** (Configurat software ca INPUT_PULLUP)
* **Buton 2 (Acțiune 1 — schimbă filtrul de voce / REC / restart WiFi, în funcție de mod):**
  * **Pin 1** $\rightarrow$ Linia de GND
  * **Pin 2** $\rightarrow$ **NXP P3_13** (Configurat software ca INPUT_PULLUP)
* **Buton 3 (Acțiune 2 — freeze spectru / PLAY / info WiFi, în funcție de mod):**
  * **Pin 1** $\rightarrow$ Linia de GND
  * **Pin 2** $\rightarrow$ **NXP P3_14** (Configurat software ca INPUT_PULLUP)

---

## 4. Software Design

### 4.1 Development Environment

- **IDE & Toolchain:** MCUXpresso SDK, CMake, Ninja, GNU Arm Embedded Toolchain 14.2.
- **Key SDK Drivers:** `fsl_lpadc`, `fsl_lpspi` (vendored, currently unused — SPI is bit-banged), `fsl_lpuart`, `fsl_gpio`, `fsl_port`.
- **Libraries:** Self-contained 256-point FFT (no external DSP library), FatFS SD Card File System (vendored source), ESP8266 AT-command driver (hand-written).

### 4.2 Firmware Architecture

Bare-metal superloop, with the audio-critical path moved into a hardware timer interrupt so it can never be delayed by LCD drawing:
1. **Startup Sequence:** Initialize system clocks and pins, bit-banged LCD SPI driver, audio engine (ADC + output timer), SD card hardware, and the ESP8266 UART.
2. **Real-time audio (SysTick ISR):** A single SysTick interrupt drives both the headphone output modulator (a 1st-order PDM bitstream, updated at a high fixed rate) and, every Nth tick, the actual per-sample audio work — reading the LPADC, applying the selected voice effect, pushing samples into the FFT accumulation buffer and the SD-recording ring buffer. Running this in the ISR (rather than deferring it to the main loop, as an earlier version did) means the LCD's relatively slow bit-banged SPI drawing can never stall or click the audio output.
3. **Main Loop:**
   - Poll the 3 tactile buttons and dispatch mode/action changes.
   - Redraw the active mode's screen (Synthesizer spectrum bars, SD Recorder status, or Wi-Fi Server status), only touching pixels that actually changed.
   - Run the 256-point FFT transform on the latest accumulated audio frame (not needed in real time, so it's fine for this to happen here instead of the ISR).
   - Pump SD card record/playback chunks and the ESP8266 AT-command state machine.

### 4.3 Main Algorithms and Data Structures

- **Self-Contained Real FFT:** A 256-point iterative radix-2 FFT (Hann-windowed to reduce spectral leakage), computing magnitude and converting it to a dB scale before mapping to bar height — a fixed linear scale was tried first and pinned every bar to maximum for any normal speaking volume, since raw FFT magnitude scales with both frame size and amplitude.
- **Peak Hold:** Maps 128 usable frequency bins into 16 LCD spectrum bars (evenly, no bins dropped), with a peak-hold marker that decays slowly.
- **FatFS WAV Header Generator:** Writes a standard 44-byte RIFF/WAV header to the SD Card file before streaming raw 16-bit PCM audio, then rewrites the size fields on stop.
- **Voice Effects:** Simple IIR biquad low-pass ("warm") and high-pass ("phone") filters, plus a ring-modulation "robot" effect (multiplying the signal by a fixed-frequency square-wave carrier) — selectable in Synthesizer mode; not yet applied during SD playback (see section 14).
- **Software PDM Output:** A 1st-order accumulator/pulse-density modulator drives a GPIO pin at a high fixed rate, standing in for the audio DAC this MCU doesn't have; an external RC low-pass filter is required to turn that into a clean analog signal.

### 4.4 Functional Requirements Summary

| ID | Tier | Requirement | Priority | Verification | Acceptance Criterion |
|---|---|---|---|---|---|
| FR-001 | Core | The system shall use NXP FRDM-MCXA153 board as main controller. | Must | Inspection | MCU powers up and runs firmware |
| FR-002 | Core | The system shall sample audio via LPADC on a SysTick-derived 16 kHz trigger. | Must | Test | LPADC sample rate 16 kHz ± 1% |
| FR-003 | Core | The system shall compute a 256-point FFT in software and derive a dB-scaled magnitude spectrum. | Must | Test | Peak frequency accuracy ± 100 Hz |
| FR-004 | Core | The system shall render an FFT spectrum bar graph with peak-hold on the 2.4" TFT LCD, refreshed every FFT frame. | Must | Test | Bars visibly track live voice pitch/loudness |
| FR-005 | Recommended | The system shall record WAV audio files to SD Card via bit-banged SPI + FatFS, only when a card is actually mounted. | Should | Test | Recorded WAV plays back cleanly on PC; REC refused with no card |
| FR-006 | Recommended | The system shall provide 3-button navigation between Synthesizer, SD Recorder, and Wi-Fi Server modes. | Should | Demonstration | Pressing the mode button cycles all 3 screens |
| FR-007 | Recommended | The system shall apply a selectable real-time voice effect (warm/phone/robot) to the live mic signal, output via a software PDM signal + RC filter to a 3.5mm jack. | Should | Test | Selected effect is audibly different in headphones |
| FR-008 | Advanced | The system shall host a local Wi-Fi access point + web server via ESP8266 for live status, spectrum data, and a recordings list. | Could | Demonstration | Dashboard accessible over Wi-Fi, updating statistics |
| FR-009 | Advanced | The system shall play back SD recordings through headphones on request. | Should | Test | Selected file plays back audibly and stops at end-of-file |

### 4.5 Non-Functional Requirements Summary

| ID | Tier | Category | Requirement | Metric / Threshold | Verification |
|---|---|---|---|---|---|
| NFR-001 | Core | Power | All external modules shall operate at 3.3V logic level. | 3.3V ± 5% | Measurement |
| NFR-002 | Core | Timing | Per-sample audio processing shall run in the real-time ISR path and never depend on LCD draw time. | No audible glitch during screen redraw | Test |
| NFR-003 | Recommended | Reliability | SD Card file writing shall stream in small chunks rather than buffering a whole recording in RAM. | ~128-sample chunks | Inspection |

### 4.6 Test Plan Summary

| Test ID | Requirement | Tier | Test Type | Expected Result | Evidence |
|---|---|---|---|---|---|
| TC-001 | FR-001 | Core | Integration | MCU boots and logs debug text via LPUART0. | Serial Log |
| TC-002 | FR-003 | Core | Unit/DSP | A whistled/sung tone produces a visibly moving peak in the corresponding FFT bar. | FFT Graph Photo |
| TC-003 | FR-005 | Recommended | System | SD Card WAV file created and readable on host PC; REC button does nothing with no card inserted. | File Screenshot |
| TC-004 | FR-007 | Recommended | Integration | Headphone output reproduces live voice intelligibly; switching the voice effect audibly changes the tone. | Audio Recording |
| TC-005 | FR-008 | Advanced | Integration | Web browser connects to the ESP8266 SoftAP IP and displays live status/spectrum JSON. | Browser Screenshot |
| TC-006 | FR-009 | Advanced | Integration | Selecting a recording and pressing PLAY reproduces it through headphones. | Audio Recording |

### 4.7 Traceability Summary

| User Story | Requirement(s) | Test Case(s) | Evidence | Gap |
|---|---|---|---|---|
| US-001 | FR-002, FR-003, FR-004 | TC-002 | FFT Graph Photo | None |
| US-002 | FR-006 | TC-001 | Demo Video | None |
| US-003 | FR-005 | TC-003 | Saved WAV File | None |
| US-004 | FR-007, FR-009 | TC-004, TC-006 | Audio Output | Voice effects not yet applied during playback (see section 14) |

---

## 5. Risk Matrix

| ID | Category | Tier Affected | Severity | Probability | Impact | Mitigation | Human Approval Required |
|---|---|---|---|---|---|---|---|
| R-001 | Technical | Recommended | Medium | Medium | SPI bus contention between LCD and SD Card (both bit-banged, shared lines) | Use separate CS lines; keep transfers short | Yes |
| R-002 | Voltage/Power | Advanced | High | Medium | ESP8266 Wi-Fi TX peak current causing 3.3V voltage drop | Add dedicated decoupling capacitors on 3.3V rail | Yes |
| R-003 | Safety | Core | Medium | Low | Driving headphones directly from an unfiltered GPIO pin | RC low-pass filter is mandatory (not just a series resistor) — confirmed on real hardware in this project | Yes |
| R-004 | Timing | Core | Medium | Low | FFT computation blocking the audio path | FFT runs in the main loop, off the real-time ISR path, so it can never stall audio output | No |

---

## 6. Assumptions and Open Questions

### 6.1 Confirmed Facts

- Main board is NXP FRDM-MCXA153 with Cortex-M33 core.
- Display module is 2.4" ILI9341 SPI TFT LCD; it has a resistive touch controller and an SD card slot on board, but only the SD slot is used by firmware — touch is not read.
- Audio input via MAX4466 microphone preamplifier.
- **This MCU has no hardware audio DAC** (verified against the vendor register headers) — audio playback is a software 1-bit PDM signal through an external RC low-pass filter to the 3.5mm jack.
- Wi-Fi via ESP8266 UART module.
- Powered via standard USB.

### 6.2 AI Assumptions

- **A-001:** ILI9341 LCD and SD Card slot share a bit-banged SPI bus using separate CS GPIO pins.
- **A-002:** The RC filter (resistor + capacitor) on the audio-out pin is required for intelligible sound, not merely a protective series resistor — a resistor alone was tested and confirmed to pass the raw unfiltered bitstream through.

### 6.3 Open Questions

| ID | Question | Why It Matters | Owner | Status |
|---|---|---|---|---|
| Q-001 | Does concurrent SD Card writing cause audio glitching or frame drops? | Affects output stability in Recorder mode | Student / Instructor | Open |
| Q-002 | Is the LPUART2 PORT mux ALT index for P1_4/P1_5 correct on this silicon? | ESP8266 link won't come up otherwise | Student / Instructor | **Resolved in code** — both pins are ALT3; the previous ALT4 on P1_4 was CT1_MAT2, a timer output. See section 15. Not yet confirmed against a working link. |
| Q-003 | Should voice effects apply during SD playback, not just live Synthesizer mode? | Matches the intended user experience | Student | Open — not yet implemented |

---

## 7. Human Review Checklist

- [x] Scope approved
- [x] Selected feature tier approved (Recommended Summer School Version)
- [x] FRDM-MCXA153 confirmed as mandatory board
- [x] FRDM-MCXA153 pinout checked
- [x] Voltage compatibility checked (3.3V logic throughout)
- [x] Current limits checked
- [x] Power budget checked
- [x] External modules checked (MAX4466, ESP8266)
- [x] Sensor/actuator interfaces confirmed
- [x] Firmware architecture approved (superloop + real-time audio ISR + self-contained FFT)
- [x] Timing and memory constraints reviewed
- [x] Test plan reviewed
- [x] Safety/privacy/security risks reviewed
- [x] AI assumptions accepted or rejected
- [x] Implementation allowed to start (Milestone 2 Approved)

---

## 8. Obtained Results (Milestone 2)

During Milestone 2, the physical circuit hardware was successfully connected and verified.
- **Schematic Design:** Schematic `circuit_image.svg` defines the wiring connections (predates the audio-output RC-filter correction — see section 3.4).
- **Hardware Montaj:** The circuit is fully assembled on a standard breadboard using the FRDM-MCXA153 and custom breakouts.
- **Power Delivery:** System boots reliably using standard USB power.
- **Pin Allocations:** Pin configurations are successfully tested and registered.
- **AI Chat Logs:** The hardware design, pin-allocation and safety session is exported as `Milestone2.json` (JSON chat-log export, uploaded to the platform).

---

## 9. Running Examples & Functional Results (Milestone 3)

Milestone 3 delivers the full application firmware (see `software/source/`) running on the assembled hardware from Milestone 2. The photos below capture the workstation operating live on the FRDM-MCXA153.

> **Which photos show which revision.** Sections **9.2–9.4** are from an earlier firmware/UI revision (32-band spectrum plus a separate oscilloscope pane, combined REC/SYNTH screen) — kept as evidence that the hardware and display path work end-to-end. Sections **9.5–9.8** show the **current** firmware: three separate mode screens (Synthesizer / SD Recorder / Wi-Fi Server), a 16-band spectrum with peak-hold and a labelled frequency axis, an input level meter, and the web dashboard.
>
> The Synthesizer screen is fully live in every build. The SD and Wi-Fi screens in 9.6–9.8 were photographed on a `DEMO_MODE` build and carry an on-screen amber label saying so — see **section 16** for exactly what that substitutes and what it leaves untouched.

### 9.1 Application Code
- **Firmware modules:** `audio_engine.c/.h` (mic sampling, self-contained radix-2 256-point FFT, selectable voice effects, software PDM audio output), `ui_display.c/.h` (ILI9341 driver + screen rendering), `sdcard_wav.c/.h` (FatFS WAV record/playback over bit-banged SPI), `wifi_esp.c/.h` (ESP8266 UART web server), tied together by a thin bare-metal superloop in `main.c`.
- **Build:** MCUXpresso SDK + CMake/Ninja, GNU Arm Embedded 14.2. The project compiles and links cleanly to the target ELF.

### 9.2 Spectrum Analyzer Mode (Live)
![Milestone 3 - Spectrum Analyzer running on hardware](./photos/Poza1_functionalitate.jpg)

The full breadboard assembly powered and running: the MAX4466 microphone feeds the LPADC, the firmware computes a 256-point FFT, and the ILI9341 renders the **SPECTRUM** screen — a live 32-band FFT bar spectrum with peak-hold, an oscilloscope pane, and on-screen touch controls (**MODE / REC / SYNTH**). The ESP8266 module, the three tactile buttons, the mic and the headphone jack (with its 1 kΩ series resistor) are all wired in, and the MCXA153 status LED is lit.

### 9.3 Spectrum / Oscilloscope UI Close-Up
![Milestone 3 - SPECTRUM screen close-up](./photos/Poza3_functionalitate.jpg)

Close-up of the running GUI showing live measurements read from the audio engine: **Peak: 780 Hz**, **Vpp: 323 mV**, **DC: 0.805 V**, and a rendered frame rate of **FPS: 60** — satisfying FR-004 (display update rate ≥ 55 FPS). The `FFT 32-BAND SPECTRUM` and `OSCILLOSCOPE` panes update in real time from the microphone input.

### 9.4 Display Bring-Up / Test Pattern
![Milestone 3 - ILI9341 display test pattern](./photos/Poza2_test.jpg)

Hardware test shot: the ILI9341 driver rendering a full RGB colour-bar test pattern, used during display bring-up to verify the SPI wiring, colour order and address-window logic before layering the UI on top.

### 9.5 Synthesizer Mode — Current UI

![Synthesizer screen running on hardware](./photos/Poza_syntetizator_1.jpg)

The redesigned Synthesizer screen on the ILI9341, photographed on the running board. Top to bottom: the title card with the active voice effect (`filter: OFF`), the horizontal **input level meter**, and the spectrum well — 16 bands with white peak-hold caps, amplitude ticks on both margins and the frequency axis labelled **0 / 2k / 4k / 6k / 8k** (Nyquist is 8 kHz at 16 kHz sampling). The footer maps the three tactile buttons to **MODE / FILTER / FREEZE**. Everything on this screen is measured from the microphone in real time — this mode uses no simulated data in any build.

Also visible: the FRDM-MCXA153, the ESP8266 module, the MAX4466 microphone, the three buttons and the RC-filtered headphone jack, all on the Milestone 2 breadboard.

![Synthesizer screen with the level meter in its red zone](./photos/Poza_syntetizator_2.jpg)

The same screen at a louder input. The level meter has crossed into its red zone and switched from cyan to coral, and the spectrum shows the expected low-frequency-dominant roll-off of a speaking voice with the peak-hold caps trailing above the live bars.

### 9.6 SD Recorder Mode

![SD Recorder screen, playback state](./photos/Poza_redare_audio.jpg)

The SD Recorder screen during playback: card status **DETECTED**, free space, the **PLAYING** state and the selected file `REC0001.WAV`, with the buttons mapped to **MODE / REC / PLAY**. The amber `demo - simulated card` line under the title marks that this build reports a simulated card — see section 16.

![Full system with headphones connected](./photos/Poza_sistem.jpg)

The complete workstation: MCU, breadboard, ESP8266, microphone, display and headphones connected to the RC-filtered PDM output. The recorder screen is in its **READY** state showing the selected recording and its position in the list (`1 / 3`).

### 9.7 Wi-Fi Server Mode

![Wi-Fi Server screen](./photos/Poza_server.jpg)

The Wi-Fi Server screen reporting the access point details the firmware configures — `ssid: MCXA153-AudioWS`, `ip: 192.168.4.1`, `state: AP UP` — plus a served-request counter, with the buttons mapped to **MODE / RESTART / LEVEL**. The amber `demo - simulated link` line marks the simulated state.

### 9.8 Web Dashboard (Bench Monitor)

![Bench Monitor dashboard](./photos/ScreenShot_Site.png)

`software/tools/dashboard.html` rendered in a browser. It mirrors the device: the same 16 spectrum bands with peak-hold, the input level, the active voice effect, and the recorder state with the recordings list and per-file download links. Hovering a band shows its exact range and magnitude — here **2500–3000 Hz, magnitude 52 / 255, peak 148**. The **CHART / TABLE** control switches to a tabular view of the same 16 bands so the data is not conveyed by the chart alone.

![Dashboard, recorder detail](./photos/Poza_recordings.png)

Recorder detail: elapsed time, remaining space, the fixed 31.25 kB/s write rate of this WAV format, and the three recordings each offering a direct `GET /download?file=` link.

![Dashboard next to the running board](./photos/Poza_site.jpg)

The dashboard open on a laptop beside the workstation. See section 16.1 for what the page reads when the board is and is not reachable.

### 9.9 AI Chat Logs (Milestone 3)
The AI pair-programming session covering the firmware module decomposition, the hand-rolled FFT, and the iterative hardware debugging (no on-chip DAC → delta-sigma output + RC filter, mic AGC/anti-alias, bit-banged SD, ESP8266 pin-mux caveat) is exported as `Milestone3.json` (JSON chat-log export, uploaded to the platform).

---

## 10. Conclusions

The project delivers a three-mode bare-metal audio workstation on the
FRDM-MCXA153: a live voice synthesizer with a real-time spectrum display, an
SD-card WAV recorder/player, and an ESP8266 access point serving status and
recordings over HTTP. All three are navigated with three tactile buttons and
share one bit-banged SPI bus.

**What is verified on hardware.** Synthesizer mode works end to end — the
microphone is audible and intelligible through the headphone jack, the
256-point FFT drives a 16-band spectrum with peak-hold, and the redesigned
screen (level meter, spectrum well, labelled frequency axis) is photographed
running in section 9.5. This mode uses no simulated data in any build.

**What is implemented but not yet confirmed.** The SD and Wi-Fi paths are
written and build clean, and two root causes that had blocked them were found
and fixed late in the project — the LPUART2 pin mux on P1_4 pointed at a timer
output rather than the UART receiver, and the SD bus was clocked far outside
the identification-phase limit. Neither fix has been exercised against the
hardware yet; sections 15 and 16 say so explicitly rather than implying
otherwise.

**The findings worth carrying forward.** Three constraints shaped the
implementation more than any design choice did:

1. **The MCU has no audio DAC.** Headphone output is a software 1-bit PDM
   bitstream on a GPIO pin, and it is only listenable because of the external
   RC filter. A higher-order noise-shaped modulator was tried and reverted —
   at this oversampling ratio it produced an input-independent tone.
2. **The MCU has no FPU.** Every float operation in the audio path is a
   library call; `__aeabi_fmul` alone is 116 instructions, and the per-sample
   path makes twenty such calls. That, not the algorithm, is what consumes the
   cycle budget.
3. **RAM, not CPU, caps the buffering.** The record ring wants to be as large
   as possible to absorb SD and UART stalls, but 24 KB of SRAM shared with a
   2 KB stack and a 1 KB heap sets the ceiling at 2048 samples.

**What would come next.** Building at `-O2` instead of `-O0` is the largest
free improvement available and was not done. Converting the biquad and level
smoothing to fixed-point would remove the twenty library calls per sample
without trading away carrier frequency. Both are quantified in section 16.3.

---

## 11. Download

Everything below is in this repository — there is no separate archive.

| Item | Location |
|---|---|
| Application firmware | [`software/source/`](./software/source/) |
| Build configuration | [`software/CMakeLists.txt`](./software/CMakeLists.txt), [`software/CMakePresets.json`](./software/CMakePresets.json) |
| Board support | [`software/frdmmcxa153/`](./software/frdmmcxa153/), [`software/board/`](./software/board/) |
| Electrical schematic | [`photos/circuit_image.svg`](./photos/circuit_image.svg) |
| Wiring tables | Section 3.3 (pin allocation) and 3.6 (detailed connections) |
| Web dashboard | [`software/tools/dashboard.html`](./software/tools/dashboard.html) |
| Photos and screenshots | [`photos/`](./photos/) |
| This document | `README.md`, duplicated as `project_documentation.md` |

### 11.1 Building

Requires the MCUXpresso SDK checkout, GNU Arm Embedded 14.2, CMake and Ninja.
Point `SdkRootDirPath` at your SDK, then:

```bash
export ARMGCC_DIR=/path/to/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
export SdkRootDirPath=/path/to/mcuxsdk
export PATH="$ARMGCC_DIR/bin:$PATH"

cd software
cmake --preset debug
cmake --build debug
```

The ELF lands in `software/debug/`, which is deliberately gitignored. A
`release` preset exists alongside `debug`; see section 16.3 for why building
with it is worthwhile.

To build against the real peripherals rather than the presentation build, set
`DEMO_MODE` to `0` in [`software/source/demo_mode.h`](./software/source/demo_mode.h)
first. Both configurations compile warning-clean with `-Werror`.

### 11.2 Running the web dashboard

Join the access point the board advertises (`MCXA153-AudioWS`, passphrase
`synth1234`), then open `software/tools/dashboard.html` in a browser. It polls
`http://192.168.4.1/status` once a second. Section 16.1 describes what it shows
when the board is not reachable.

### 11.3 Not included

No demo video, slide deck or ChangeLog has been produced for this milestone,
and the Milestone 2/3 AI chat logs are uploaded to the course platform rather
than committed here (they are excluded by `.gitignore`).

---

## 12. Project Journal

| Date | Work Completed | Problems / Risks | Next Steps | Author |
|---|---|---|---|---|
| 2026-07-20 | Initial project scoping and requirements engineering package created | SPI bus sharing risk identified | Set up MCUXpresso Config Tools for pins and clocks | Vancea Adrian |
| 2026-08-23 | Milestone 2 completed: electrical schematics generated, breadboard assembly done, connection mapping documented | SPI shared bus contention risk | Proceed with bare-metal firmware implementation | Vancea Adrian |
| 2026-09-03 | Firmware rewrite: real FFT-driven Synthesizer mode with selectable voice filters, real SD card recording/playback (FatFS), real WiFi/ESP8266 status+file server, mic quality pass (AGC, anti-alias, 2nd-order noise-shaped output). | See section 15 | Wire an RC filter on the audio output pin; verify LPUART2 pin-mux ALT value on real hardware | Claude (AI pair-programmer) |
| 2026-09-05 | Milestone 3 completed: firmware verified live on hardware (SPECTRUM screen @ 60 FPS, Peak 780 Hz); functionality photos added (section 9); firmware AI chat log exported (`Milestone3.json`), hardware AI chat log exported (`Milestone2.json`) | RC audio filter + ESP8266 pin-mux ALT still to confirm on hardware | Finalize documentation, wire audio-output RC filter, verify LPUART2 ALT values | Vancea Adrian |
| 2026-09-07 | Root-caused and fixed the two blockers: LPUART2 P1_4 was muxed to CT1_MAT2 (a timer output) instead of the UART receiver, and the SD bus was clocked far above the 100-400kHz identification limit. Redesigned the Synthesizer screen (level meter, spectrum well, frequency axis). Added the DEMO_MODE presentation build and the web dashboard. Enlarged the record ring to 128ms and cached SD free space. | Neither peripheral fix exercised on hardware yet; project still builds at -O0 | Flash with DEMO_MODE 0 and verify the SD and Wi-Fi paths; build with the release preset | Vancea Adrian + Claude (AI pair-programmer) |
| 2026-09-07 | On-hardware audio debugging (that Sep 5 test used an earlier firmware revision): confirmed no HW DAC, reverted the AGC and the 2nd-order noise-shaped output modulator mentioned above back to the original fixed-gain 1st-order accumulator after both regressed real playback, moved per-sample processing into the SysTick ISR (fixed audio clicks caused by LCD redraws), fixed a UI bug that redrew a status dot every loop iteration instead of on change, raised the PDM carrier rate for better filterability. Documentation (this file) rewritten to match the actual 3-mode, no-touch, no-DAC, no-CMSIS-DSP implementation, dropping the earlier touch-piano/XY-pad concept. | RC filter tuning is still an open, hands-on process — some residual carrier noise may be a hard limit of a GPIO+RC "DAC" on this chip | Get the SD playback + Wi-Fi paths fully working end-to-end on hardware; apply voice effects during SD playback too | Claude (AI pair-programmer) |

---

## 13. Bibliography / Resources

### Hardware Resources
- NXP FRDM-MCXA153 User Manual & Board Schematics
- NXP MCXA153 Reference Manual & Data Sheet
- ILI9341 TFT Display Controller Datasheet
- MAX4466 Microphone Amplifier Datasheet
- ESP8266 Specification Sheet / AT Command Set

### Software Resources
- NXP MCUXpresso SDK API Reference Documentation
- FatFS Generic FAT File System Module Documentation

---

## 14. Documentation Status

**MILESTONE 3 COMPLETED — APPLICATION FIRMWARE VERIFIED LIVE ON HARDWARE; ON-HARDWARE AUDIO DEBUGGING IN PROGRESS**

---

## 15. Implementation Notes (Firmware, updated 2026-09-07)

Details and caveats not worth cluttering the main spec above, but worth keeping track of:

- **SD card is bit-banged SPI, not the LPSPI0 hardware peripheral.** It shares the LCD's existing SCK/MOSI pins (`P2_12`/`P2_13`) and adds MISO (`P2_16`) and its own CS (`P1_3`, already documented in section 3.3). This was a deliberate simplification to avoid touching the already-working, hand-rolled LCD driver.
- **No Card-Detect pin exists on this wiring**, so "SD card present" is determined purely by whether `f_mount()` succeeds — re-attempted whenever the SD Recorder screen is opened.
- **ESP8266 pin mux (P1_4/P1_5 → LPUART2) is now `kPORT_MuxAlt3` on both pins** (was Q-002). The firmware previously used ALT4 on P1_4, which on that pin is `CT1_MAT2` — a CTIMER1 match *output*, so the MCU was driving the line the ESP8266 transmits on and could never receive a byte. The value is anchored to SDK-generated `pin_mux.c` files that mux these exact pins (`ctimer/simple_pwm` sets CT1_MAT2 on P1_4 to ALT4; `freqme` sets FREQME_CLK_IN1 on P1_5 to ALT1). Note the slash-separated `pin_signal` strings are *not* a dense mux table — `WUU0_INx` entries are wake-up inputs and consume no ALT slot. Fixed but **not yet confirmed against a working link on hardware**.
- **The Wi-Fi HTTP responses carry `Access-Control-Allow-Origin: *`**, without which a browser page served from anywhere else is refused before it can read the JSON.
- **Audio output modulator history:** a 2nd-order noise-shaped modulator was tried for better theoretical SNR, but at the oversampling ratio in use it produced an audible, input-independent tone (a known failure mode for higher-order 1-bit modulators at low oversampling) and was reverted to the original, simple 1st-order accumulator, which is unconditionally stable at any rate. The carrier rate has since been raised well beyond the original design specifically to give the external RC filter more room to attenuate it.
- **Real-time audio lives in the SysTick ISR, not the main loop.** An earlier version deferred per-sample processing to the main loop via a flag, matching a simpler design — that broke down once the LCD had real content to redraw (bit-banged SPI can block the main loop for tens of milliseconds), causing audible periodic clicks. Only the FFT transform itself (not needed in real time) still runs in the main loop.
- **Voice effects are not yet applied during SD playback** (tracked as Q-003 above) — currently they only affect the live Synthesizer path.
- **FFT is a small self-contained radix-2 implementation** in `audio_engine.c`, not an external DSP library — a 256-point transform is cheap enough to hand-roll and this avoided pulling in and configuring a large external component.

---

## 16. Demonstration Mode (`DEMO_MODE`)

The SD recorder and the Wi-Fi link are still being brought up on hardware. So the
firmware ships a presentation build, switched by a single constant in
`software/source/demo_mode.h`, that lets the board be demonstrated and
photographed with those two screens populated:

```c
#define DEMO_MODE 1   /* 0 = normal build, real peripherals only */
```

**What it substitutes.** With `DEMO_MODE` on, `sdcard_*` reports a mounted 2 GB
card and `wifi_esp_get_state()` reports `AP UP`. The simulated card behaves the
way a real one would: the elapsed clock runs while recording, free space ticks
down at this WAV format's true 31.25 kB/s, and stopping a take adds a file to
the list. `/files` mirrors that same simulated list, so the web view and the LCD
never disagree about which recordings exist.

**What it does not touch.** `diskio.c`, the FatFS file I/O in `sdcard_wav.c` and
the ESP8266 AT state machine are all untouched — the state machine keeps running
underneath, so a module that does come up serves real requests. The substitution
happens only at the status-query boundary the UI reads, and building with
`DEMO_MODE 0` restores the real behaviour exactly. Both configurations compile
clean.

**How it is labelled.** Both affected screens carry an amber
`demo - simulated card` / `demo - simulated link` line under the title, and the
HTML dashboard shows a `DEMO` lamp reading `simulated signal` whenever it is not
actually talking to the board.

> **Any photograph taken from a `DEMO_MODE 1` build shows simulated SD and Wi-Fi
> state, not working hardware.** The Synthesizer screen is unaffected — its
> spectrum, level meter and audio path are real in every build.

### 16.1 Bench Monitor (web dashboard)

`software/tools/dashboard.html` is a standalone page — open it directly in a
browser, no server needed. Joined to the board's access point it polls
`http://192.168.4.1/status` (5 Hz) and `/files`, and renders the same 16 bands
the LCD draws, the input level, recorder state and the file list with download
links. When the board is unreachable it renders a placeholder signal instead, so
the layout can still be reviewed.

> **The dashboard screenshots in this repository are a finished-state UI
> preview, not a captured measurement session.** A hosted copy of the page
> cannot reach a device on your own network, so it always shows the placeholder
> signal; only the standalone file, opened while joined to the access point,
> reads the board.

`/status` was also corrected to send the 16 **aggregated** bands rather than the
first 16 raw FFT bins — which had been only the bottom ~1 kHz of an 8 kHz
spectrum — and now includes `rec_ms`.

### 16.2 Real-path changes made alongside the demo build

These are fixes to the **actual** implementation, active with `DEMO_MODE` either
way:

- **Record ring buffer 512 → 2048 samples** (`audio_engine.h`). The producer
  side runs in the SysTick ISR and cannot wait, so the buffer has to cover the
  longest pause the main loop can take. Two stalls dominate: an SD card can
  disappear for 100 ms+ doing internal housekeeping mid-write, and `wifi_esp.c`
  transmits with `LPUART_WriteBlocking`, which parks the loop for ~27 ms per
  ~310-byte `/status` reply. The old 512 samples was 32 ms of headroom, so
  recording while a browser polled the board dropped audio. 2048 gives 128 ms.
  4096 (256 ms) was measured first and is the nicer number, but with
  `DEMO_MODE 0` it left only ~700 bytes of SRAM once the 2 KB stack and 1 KB
  heap are placed. **RAM, not the audio, is what caps this** — the real build
  now sits at 19.7 KB of 24 KB including stack and heap.
- **Free space is cached** (`sdcard_wav.c`). `f_getfree()` is not cheap: FatFS
  keeps a free-cluster count but every write invalidates it, and the next call
  walks the whole FAT — seconds, over bit-banged SPI on a 2 GB card. That value
  is read by the recorder screen *and* by every `/status` request, so uncached
  it would rescan the FAT on each poll, exactly while recording is writing. It
  is now refreshed on mount and when a recording closes; in between, the bytes
  the open file has written are subtracted, which is exact for fixed-rate PCM.
- **Dashboard polls at 1 Hz, not 5 Hz.** A `/status` reply is ~310 bytes; at
  115200 baud that is ~27 ms of shifting on its own, and every reply also costs
  an `AT+CIPSEND` handshake, a `SEND OK` and an `AT+CIPCLOSE` round trip.
  Polling harder than the link can answer only queues requests.

### 16.3 Known ceilings

Measured, not estimated — from the linked ELF:

- **The MCU has no FPU** (`-mfloat-abi=soft`); every float operation is a
  library call. `__aeabi_fmul` is 116 instructions, `__addsf3` is 120. The
  per-sample audio path makes **20 such calls**.
- **SysTick fires every 100 cycles** (96 MHz core, 960 kHz PDM carrier), and the
  per-sample work is roughly **2,100 instructions** — so it spills across many
  tick periods and the 960 kHz carrier is not actually held.
- The project currently builds at **`-O0`** on all 31 translation units, even
  though a `release` preset exists. Building optimised is the single largest
  available improvement and costs nothing.
- Converting the biquad and the level smoothing to **fixed-point** would remove
  those 20 library calls per sample without trading away carrier frequency
  (lowering the carrier would free more CPU, but it was deliberately raised
  192 k → 384 k → 960 k to move the tone out of the RC filter's passband).
- **WAV download over the AT link is slow by construction**: 512-byte chunks,
  each with a full `AT+CIPSEND` handshake. A 10-second recording is ~320 KB
  ≈ 640 chunks ≈ 1–2 minutes. Practical for short clips only.
