# Easymic

**Easymic** is a lightweight and high-performance desktop application for global microphone control on Windows. Forget about memorizing hotkeys for individual apps like Teams, Discord, TeamSpeak, etc.. Easymic centralizes microphone management while ensuring 100% reliability in muting.

By intercepting **system-level microphone callbacks**, Easymic guarantees that your microphone is muted when you need it to be, preventing awkward moments during calls or streams. The app is designed for speed, efficiency, and simplicity, all while consuming minimal system resources.

Built with **Native WinAPI** in **C++**, Easymic leverages the full power of the Windows ecosystem to provide an unparalleled experience for managing your microphone.

---

## ✨ Features

- **Global Microphone Hotkey**  
  Set a custom hotkey (any keyboard or mouse button, or both) for toggling your microphone state.

- **Reliable System Mute**  
  Ensures your microphone is 100% muted by intercepting system-level callbacks — no more guessing!

- **Volume-Based Muting Mode**  
  Optionally set the microphone volume to 0 instead of muting system-wide, useful for applications like Teams that override system mute settings.

- **Status Indicator**  
  A convenient visual indicator displays the current microphone state:
    - **Muted** or **Unmuted**
    - **Sound Detection** (shows when your microphone is actively picking up sound).

- **Audible Feedback**  
  Enable an optional bell-like sound (similar to Discord) to notify you when the microphone state changes.

- **Ultra-Fast Performance**  
  Designed for speed, Easymic offers near-instant response times with negligible CPU and RAM usage.

- **Lightweight and Native**  
  Written in **C++** using the **Native WinAPI**, Easymic runs efficiently without unnecessary overhead. (Consumption of 1 - 2 MiB of RAM in the background)

- **Customizable Hotkeys**  
  Supports flexible configurations, including single-key and multi-device input for maximum convenience. (Combining mouse and keyboard)

---

## 🚀 Tech Stack

- **Programming Language:** C++
- **Framework:** Native WinAPI
- **Build System:** CMake

---

## 📸 Screenshot of the settings window
![img.png](screenshots/img.png)

---

## ❓️How to use

1. **Download the latest release from the [Releases](https://github.com/k1tbyte/Easymic/releases) page.**
2. **Extract the ZIP file to a folder.**
3. **Run the `Easymic.exe` file.**
4. Find it in the system tray (bottom right corner) and right-click and select `Settings` or double-click the icon to open the settings window.
5. Configure and click `Save` to apply the settings.
6. Enjoy!
---

## 📦 Building from source

1. **Clone the Repository**
   ```bash
   git clone https://github.com/k1tbyte/easymic.git
   cd Easymic
    ```
2. **Build the Project**
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B ./build
   cmake --build ./build
   ```
3. **Run the Application**
   ```bash
    ./build/Easymic.exe
    ```
---
## 📜 License
This project is licensed under the Apache-2.0 - see the [LICENSE](LICENSE.txt) file for details.
