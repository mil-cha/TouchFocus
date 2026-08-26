# TouchFocus INDI driver

Open-source INDI 2.1.7 focuser driver for the TouchFocus `focuserd` daemon.
The driver connects locally to `127.0.0.1:7625`; motor GPIO access remains
exclusively in `focuserd`.

## Build on StellarMate / Raspberry Pi

```bash
sudo apt install build-essential cmake pkg-config libindi-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
sudo cmake --install build
```

## Debian package

Build the installable package directly on StellarMate/Raspberry Pi so its
architecture matches the target system:

```bash
sudo apt install build-essential cmake pkg-config libindi-dev dpkg-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
cd build
cpack -G DEB
```

This creates `touchfocus-indi-driver_0.3.0-1_arm64.deb` on a 64-bit Raspberry
Pi. Install or upgrade it with dependency handling:

```bash
sudo apt install ./touchfocus-indi-driver_0.3.0-1_arm64.deb
```

Inspect an uninstalled package if required:

```bash
dpkg-deb --info touchfocus-indi-driver_0.3.0-1_arm64.deb
dpkg-deb --contents touchfocus-indi-driver_0.3.0-1_arm64.deb
```

The INDI registration is installed as
`/usr/share/indi/indi_touchfocus_focuser.xml`. The `indi_` filename prefix is
required for discovery by KStars/Ekos on StellarMate.

Restart the INDI server or StellarMate profile after installation. Select
**TouchFocus Focuser** from the focuser driver list.

If the historical registration file is still installed, remove it to avoid a
duplicate obsolete entry:

```bash
sudo rm -f /usr/share/indi/Focuserd.xml
```

The matching `reference/focuserd.py` must be installed and restarted before
using this driver because it supplies the non-blocking `GETSTATUS`, `GOTO`,
`MOVE`, `ABORT`, and temperature-compensation commands.

## Exposed functions

- absolute and relative movement in motor microsteps
- immediate abort
- position synchronization
- all nine daemon-backed TouchFocus presets (P1-P9), shown in steps and mm
- daemon-derived maximum travel
- DS18B20 temperature
- daemon-side temperature compensation enable, coefficient, and hysteresis

Temperature compensation is executed by `focuserd`, not the INDI process, so
it continues if the INDI client disconnects. Do not enable a second independent
temperature compensation controller in Ekos.
