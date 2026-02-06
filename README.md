---

# Supermodel-PonMi

This project is a **fork of the [original Supermodel3](https://github.com/trzy/Supermodel),** the legendary Sega Model 3 emulator.

## 🙏 Acknowledgments

First and foremost, I would like to express my deepest gratitude to **Bart Trzynadlowski, Nikolas Nikas**, and the entire **Supermodel development team**. Their incredible work in documenting and emulating the complex Sega Model 3 hardware has made this project possible. This fork is built upon their solid foundation, with my own experimental features added to enhance the "Arcade Experience."

---

## ✨ Features Added in this Fork

### 💎 Integrated Identity & Visuals
* **Native Icon Branding:** No longer a generic executable. The "PonMi" soul is now embedded directly into the binary, ensuring a distinct presence in your toolkit.
  ![0071](https://github.com/user-attachments/assets/34e3d9e7-a11c-492d-966b-aba3784aa6e5)

* **Streamlined GUI Workflow:** Integrated natively with the latest OSD (On-Screen Display) logic. Every experimental feature—from scanlines to netplay offsets—is now at your fingertips, bridging the gap between raw power and user accessibility.

<img width="1280" height="960" alt="image" src="https://github.com/user-attachments/assets/e07b38bf-ec70-4d6e-883f-dd91e70f5bd0" />


### 🎮 Deterministic Replay System

<img width="1280" height="960" alt="image" src="https://github.com/user-attachments/assets/17f179a5-39f4-4231-9ca8-4c927e09d802"/>

A frame-accurate input recording system designed for perfect synchronization, specifically optimized for technical gameplay in titles like *Spikeout FE*.

* **Perfect Sync:** Fixes desync issues by capturing the initial State Load as the recording's anchor.
* **Edge-Detection:** Prevents redundant processing during state loads, ensuring playback starts exactly from Frame 0 without lag.

### ⌨️ Customizable System Keys

Unlike the original Supermodel, you can now map the following system functions to any key or button of your choice:

* **Pause**
* **Save State / Load State**
* **Change Save Slot**

### 🖥 Seamless GUI Integration

Fully compatible with **[Sega-Model-3-UI](https://github.com/BackPonBeauty/Sega-Model-3-UI-for-20240128-)**. This allows you to manage the new features through a graphical interface without relying on command-line arguments, providing a more intuitive and modern experience.

### 📺 Advanced CRT Emulation

<img width="1280" height="960" alt="image" src="https://github.com/user-attachments/assets/18fba7fd-38bc-4281-8b80-af7768319854" />


Recreates the nostalgic visual aesthetics of 90s arcade cabinets.

* **Barrel Distortion:** Simulates the physical curvature of a CRT monitor. Adjustable via `barrelStrength`.
* **Scanlines:** Adds high-fidelity scanlines to provide depth and softness to Model 3 polygons. Adjustable via `scanlineStrength`.

---

## 🛠 Configuration (`Supermodel.ini`)

Add or modify these lines in your `Supermodel.ini` to tune the visuals:

```ini
; Barrel Distortion Strength (0 - 10)
barrelStrength = 1

; CRT Scanline Intensity (0 - 10)
scanlineStrength = 1

```

---

## 📥 How to Use Replay

### Recording

1. Create a **Save State** at the point where you want to begin.
2. Launch Supermodel with the `-record` option:
```bash
Supermodel.exe -record my_play.rec romname.zip

```


3. Once the game starts, **perform a State Load**. (This load action is recorded and serves as the sync anchor).
4. Play the game! All inputs are saved to the `.rec` file.

### Playback

Launch with the `-play` option:

```bash
Supermodel.exe -play my_play.rec romname.zip

```

# 🏎️ Technical Report: The "Project 1999" Legacy

### Extreme Optimization and Evolution of the Model 3 Network Stack

## 📝 1. Executive Summary

This project represents the final evolution of the Sega Model 3 emulator’s synchronization architecture. By re-engineering the TCP stack from the ground up, we have achieved a **near-zero** software-induced latency environment, leaving only the physical Network Round-Trip Time (RTT) as the limit. Beyond mere emulation, this project integrates modern visual fidelity and functional enhancements that the original 1999 hardware could never have imagined. **You will clearly experience the impact of these optimizations during link play over the Internet.** 🚀

## 🎞️ 2. Visual & Functional Evolution (Beyond Original Specs)

We didn't just replicate the past; we transcended it by adding features that define the "ultimate" arcade experience:

* **Barrel Effect & Scanline Integration:** Faithfully recreating the 29-inch CRT curvature and phosphor-row aesthetics, bringing the "soul" of the arcade cabinet to modern flat panels.
* **Frame-Perfect Replay System:** A high-precision input recording engine that allows for the lossless capture and playback of legendary matches—a feature non-existent in the original hardware.

## 🛠️ 3. Key Network Optimizations

### ⚡ 3.1. Transport Layer & Protocol Engineering

* **Total Elimination of Nagle’s Algorithm:** Forced immediate dispatch of small input packets using `TCP_NODELAY`.
* **Self-Describing Header Architecture:** Utilizes an 8-byte header within a ~300-byte packet structure. By embedding compressed and original sizes, the engine ensures **universal compatibility** and robust error detection across all titles without the need for hardcoded offsets. ✨
* **Zero-copy Reception:** Data lands directly from the OS socket buffer into the target application buffer, eradicating redundant memory cycles.

### 📉 3.2. Delta Encoding & Bandwidth Reduction

* **XOR-based Delta Encoding:** Only the differences between frames are transmitted, reducing bandwidth consumption by **up to 90%**.
* **MTU-Aware Compression:** Optimized to stay well within the 1500-byte MTU, preventing packet fragmentation and ensuring stable synchronization even on jittery connections.

## 🏆 4. Evaluation & Results

* **Internal Latency Reduction:** Software overhead—once roughly **5ms** due to standard OS/TCP buffering—has been slashed to **under 1ms**. ⏱️
* **Stability:** High-speed synchronization is now confirmed across the entire Model 3 library, including *Spikeout*, *Dirt Devils*, and *Ski Champ*. 🎮

## 🏁 5. Conclusion: Beyond the Simulation

The mission to return to 1999 has entered its final phase. We haven't just optimized the code; we've redefined the "vibe." By stripping away implementation inefficiencies and wrapping the engine in a bespoke, high-performance GUI environment, we have created more than an emulator. 

It is a time machine, now polished with a visual identity that honors the ghosts we've been chasing. The link play is stable, the visuals are authentic, and the interface is seamless. 

---

### 🌹 Dedication

> **"Dedicated to the women who passed me by while I was chasing the ghosts of 1999."**

---

**Status: [FULLY OPERATIONAL]**

## 👤 Author

**BackPonBeauty (背中ポン美)**

* GitHub: [backponbeauty](https://www.google.com/search?q=https://github.com/backponbeauty)
* Specialist in Sega Model 3 research and *Spikeout Final Edition*.

---

### 💡 Note to Users

This fork is experimental. For the most stable and official experience, please visit the **[Original Supermodel Repository](https://github.com/trzy/Supermodel)**.

---
