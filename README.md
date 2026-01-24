---

# Supermodel-PonMi

This project is a **fork of the [original Supermodel3](https://github.com/trzy/Supermodel),** the legendary Sega Model 3 emulator.

## 🙏 Acknowledgments

First and foremost, I would like to express my deepest gratitude to **Bart Trzynadlowski, Nikolas Nikas**, and the entire **Supermodel development team**. Their incredible work in documenting and emulating the complex Sega Model 3 hardware has made this project possible. This fork is built upon their solid foundation, with my own experimental features added to enhance the "Arcade Experience."

---

## ✨ Features Added in this Fork

### 🎮 Deterministic Replay System

<img width="640" height="480" alt="image" src="https://github.com/user-attachments/assets/17f179a5-39f4-4231-9ca8-4c927e09d802"/>

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

<img width="640" height="480" alt="image" src="https://github.com/user-attachments/assets/18fba7fd-38bc-4281-8b80-af7768319854" />


Recreates the nostalgic visual aesthetics of 90s arcade cabinets.

* **Barrel Distortion:** Simulates the physical curvature of a CRT monitor. Adjustable via `barrelStrength`.
* **Scanlines:** Adds high-fidelity scanlines to provide depth and softness to Model 3 polygons. Adjustable via `scanlineStrength`.

---

## 🛠 Configuration (`Supermodel.ini`)

Add or modify these lines in your `Supermodel.ini` to tune the visuals:

```ini
; Barrel Distortion Strength (Recommended: 0.01 - 0.04)
barrelStrength = 0.02

; CRT Scanline Intensity (Recommended: 0.8)
scanlineStrength = 0.8

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

---

## 🚀 Roadmap

* [ ] **GUI Integration:** Control Recording/Playback directly from the UI.
* [ ] **Shader Presets:** Pre-configured settings for different cabinet styles.

## 👤 Author

**BackPonBeauty (背中ポン美)**

* GitHub: [backponbeauty](https://www.google.com/search?q=https://github.com/backponbeauty)
* Specialist in Sega Model 3 research and *Spikeout Final Edition*.

---

### 💡 Note to Users

This fork is experimental. For the most stable and official experience, please visit the **[Original Supermodel Repository](https://github.com/trzy/Supermodel)**.

---
