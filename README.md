# zelda3-2dscreen

A native Nintendo 3DS port of *The Legend of Zelda: A Link to the Past*, reverse-engineered into C. This port features dual-screen support, stable 60 FPS gameplay, and fully functional hardware-accelerated audio using the 3DS NDSP engine.

<img src="showcase.png" width="300" />
<br><em>*Screenshot taken from the original Android port.</em>

## Acknowledgements & Credits
* **snesrev** for the monumental effort of reverse-engineering the original game into the **[zelda3 C port](https://github.com/snesrev/zelda3)**.
* **samyost1** for the fantastic **[zelda3-android](https://github.com/samyost1/zelda3-android)** port, which introduced the dual-screen mechanics and Android support that served as the base for this project.
* **padinadrian** for their work on bringing the original C codebase to the 3DS platform in the **[padinadrian/zelda3-3ds](https://github.com/padinadrian/zelda3-3ds)** repository.

## Instructions

### 1. Build the game (.cia)
1. Place your legally owned original ROM of the game (`.sfc` extension) inside the `3ds/` folder of this project.
2. Rename your ROM file to exactly **`zelda3.sfc`**.
3. Open a terminal, navigate to the `3ds/` directory, and run the build script:
   ```bash
   ./build3ds.sh
   ```
4. The **`zelda3.cia`** file will be automatically generated in that same folder, ready to be installed.

### 2. Move files to your console
1. Transfer the `zelda3.cia` file to your console's SD card and install it using the FBI application.
2. Transfer the **`zelda3_assets.dat`** file (which was also generated in your `3ds/` folder) to your SD card in the following exact path: `SD:/3ds/zelda3/zelda3_assets.dat`.

### 3. Fixing Muted Audio (Important)
If you launch the game and hear no music or sound effects, your console needs to dump the audio firmware (DSP) to the SD card:
1. Open **FBI** on your console. Go to the main menu, scroll down to the bottom, and look for the **"Dump DSP"** option. Select it and wait for it to finish.
2. **If that option does not appear in your FBI:** In the `3ds/` folder of this repository, you will find the **[DSP1](https://github.com/zoogie/DSP1)** application by **zoogie** in two formats. Use the one you prefer:
   * **`DSP1.3dsx`**: Transfer it to your console and run it from the **Homebrew Launcher**.
   * **`DSP1.cia`**: Install it using FBI and run it from your console's HOME menu.
   Both options will dump the audio file automatically. (Once the problem is solved, you can delete the DSP1 application).

And that's it! You can now enjoy the game.

## Windows Building
To build it on Windows, install Git, Docker, and WSL. Open Git Bash in the `3ds` folder and run `MSYS_NO_PATHCONV=1` before running `./build3ds.sh`.
