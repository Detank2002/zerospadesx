# ZeroSpades X [![Build status](https://github.com/Detank2002/zerospadesx/actions/workflows/ci.yml/badge.svg)](https://github.com/Detank2002/zerospadesx/actions/workflows/ci.yml) [![Latest release](https://img.shields.io/github/release/Detank2002/zerospadesx.svg)](https://github.com/Detank2002/zerospadesx/releases/latest)

ZeroSpades X is [ZeroSpades](https://github.com/Fran6nd/zerospades) with VR:
play Ace of Spades in a headset on PC (OpenXR) or standalone on a Meta Quest 3,
on the same servers as everyone else. Everything else is ZeroSpades as Fran6nd
makes it; ZeroSpades X follows its `vulkan-2` branch.

[Download](https://github.com/Detank2002/zerospadesx/releases/latest) — Community: [BuildAndShoot](https://buildandshoot.com) - [aloha.pk](https://aloha.pk)

## What ZeroSpades X adds
* **VR on PC**: tick **VR Headset** in the launcher's Graphics tab (Vulkan, any
  OpenXR runtime: SteamVR, Meta Link, ...). Tracked head and hands, the gun in
  your hand, a wrist menu. See [VR.md](VR.md).
* **Meta Quest 3, standalone**: the `quest3.apk` from a release, sideloaded
  (`adb install -r ZeroSpadesX-<version>-quest3.apk`). It installs over the
  earlier ZeroSpades VR / ZeroSpades X builds and keeps your settings.
* **Training**, on your own machine, no server needed:
  * **Tutorial**: a course to learn the controls (VR or not): the range,
    digging, building and grenades. A ping shows where to go next, and a
    checklist and timer track your progress (with extra steps in a headset).
  * **Aim training**: 60-second scenarios in an arena of their own. Gridshot,
    Flick, Tracking, Strafe duel and Long range, with a rifle, SMG or shotgun.
    A countdown, the clock, kills, accuracy and score on screen, results at
    the end with headshots and time to kill, and your best scores kept.
    Targets can be outlined. Jump (A in VR) goes again.
  * Ammo, grenades and blocks never run out in either.
* **Everyone sees VR players**: on servers running `Server/vr_presence.py`,
  VR players' heads, hands and guns are drawn for every ZeroSpades X player,
  on a monitor or in a headset.
* **macOS** builds have no VR (there is no OpenXR runtime there), but they
  draw other players' VR like the rest.

### Updating
The game checks for a new release at startup. The Windows (x64, x86) and
Linux x64 packages update themselves: click **Update Now** (or the banner at
the top of the menu), keep playing in the menu while it downloads, then click
the banner to restart into the new version. Your settings, mods and demos are
kept. A package is installed only if the release's `SHA256SUMS` carries the
release key's signature (`SHA256SUMS.sig`) and the package matches its sum.
The game needs to be able to write to its own folder (on Linux, run it from
that folder); otherwise, and on macOS, Nix and the Quest, the banner opens the
release page to download it by hand.

Releases are tags (`v0.3.0`, ...). The `zerospadesx` branch is rebased onto
upstream's `vulkan-2` before each release, so its history is rewritten then;
build from a tag if you want a fixed point.

## What is OpenSpades?
[OpenSpades](https://github.com/yvt/openspades) is a compatible client of [Ace Of Spades](https://en.wikipedia.org/wiki/Ace_of_Spades_(video_game)) 0.75.

* Can connect to a vanilla/[pyspades](https://code.google.com/archive/p/pyspades)/[PySnip](https://github.com/NateShoffner/PySnip)/[piqueserver](https://github.com/piqueserver/piqueserver)/[SpadeX](https://github.com/SpadesX/SpadesX) server.
* Uses OpenGL/AL for better experience.
* Open source, and cross platform.

## Ok but what the heck is ZeroSpades?
ZeroSpades is a fork of OpenSpades with extra features and many improvements/fixes,
by Fran6nd and its contributors (the list below is theirs).

Some of the most important changes are:

* Demo recording/playback (credits to Fran6nd).
* Pie menu (credits to Fran6nd).
* In-game Hit detection debugger (credits to BR).
* Improved firstperson weapon animations (credits to PTrooper).
* More thirdperson animations.
* Weapon charms/keychains support.
* Configurable left-handed viewmodel.
* Viewmodel position presets (Default, Balanced, Minimal).
* Firstperson playermodels (torso & legs).
* Classic firstperson viewmodel.
* Dynamic playermodels based on weapon class.
* Dead player corpse and falling blocks physics.
* Configurable ragdoll removal.
* Physically-based lighting for models.
* Smooth interpolation for player orientations.
* Killstreak sounds (needs [Killsounds.pak](https://github.com/zerospades/zerospades-paks/raw/refs/heads/main/(SFX)%20Killsounds.pak))
* Extended block color palette.
* Configurable alive player counter.
* HUD hotbar displaying selectable tools.
* HUD health bar with damage animation.
* Configurable crosshair and scope.
* Configurable HUD color and safe zone.
* Player names while on spectator mode.
* Teammate names added to the minimap.
* Damage dealt to players shown as floating damage numbers.
* Client-side hit analyzer (showing hit player name, distance, and body part).
* Player statistics such as: hit accuracy, kill/death ratio, kill streak, number of melee/grenade kills, and blocks placed.
* Killfeed icons with kill indicators (non-scoped, in-air) and domination tracking.
* Multiple sensitivity scaling presets (Quake, Source, Valorant, etc.)
* Same fog tint/color as the classic client (openspades applies a [bluish tint](https://github.com/yvt/openspades/blob/v0.1.3/Resources/Shaders/Fog.vs#L27) to the fog).
* Demo recording compatible with [aos_replay](https://github.com/BR-/aos_replay) format.

-- And a lot of other things that I forgot to mention here.

## Demo Recording & Playback

ZeroSpades supports recording and playing back gameplay demos using a format compatible with [aos_replay](https://github.com/BR-/aos_replay).

### Recording

Record gameplay from the command line:

```bash
# Record demos to mydemo_1.dem, mydemo_2.dem, etc. (one per game/map)
openspades aos://server:port --record mydemo
```

Demos are saved with a numeric suffix for each game joined during the session.

### Playback

Play back demos directly in ZeroSpades:

```bash
# Play a demo file
openspades /path/to/demo.dem

# Or with the explicit flag
openspades --replay /path/to/demo.dem
```

Demos can also be played back using the external [aos_replay](https://github.com/BR-/aos_replay) tool.

## How to Build

### Prerequisites (all platforms)
```
git clone --recurse-submodules https://github.com/Detank2002/zerospadesx
cd zerospadesx
vcpkg/bootstrap-vcpkg.sh    # macOS/Linux
vcpkg\bootstrap-vcpkg.bat   # Windows
```
vcpkg dependencies are installed automatically during cmake configure.

---

### macOS — Apple Silicon
```bash
brew install molten-vk vulkan-headers glslang
mkdir build && cd build
cmake .. -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
  -D VCPKG_TARGET_TRIPLET=arm64-osx \
  -D VCPKG_OVERLAY_TRIPLETS=../cmake/triplets \
  -D CMAKE_OSX_ARCHITECTURES=arm64 \
  -D CMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -D CMAKE_CXX_STANDARD=17
cmake --build . --parallel
```

### macOS — Intel
Same as above but use:
```
  -D VCPKG_TARGET_TRIPLET=x64-osx \
  -D CMAKE_OSX_ARCHITECTURES=x86_64 \
```

---

### Windows (x64)
Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), then from a Visual Studio command prompt:
```bat
mkdir build && cd build
cmake .. -A x64 ^
  -D CMAKE_BUILD_TYPE=Release ^
  -D CMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -D VCPKG_TARGET_TRIPLET=x64-windows-static ^
  -D CMAKE_CXX_STANDARD=17
cmake --build . --config Release --parallel
```
For x86, replace `x64` with `Win32` and `x64-windows-static` with `x86-windows-static`.

---

### Linux — Ubuntu / Debian
```bash
sudo apt-get install -y build-essential cmake ninja-build \
  libsdl2-dev libsdl2-image-dev libglew-dev libfreetype6-dev \
  libcurl4-openssl-dev libogg-dev libopus-dev libopusfile-dev zlib1g-dev
# Install Vulkan SDK from https://vulkan.lunarg.com/sdk/home
mkdir build && cd build
cmake .. -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_CXX_STANDARD=17
cmake --build . --parallel
```

### Linux — Nix
```bash
nix build
```

### Tests
The VR unit tests and the Training server's test, under CTest (no GPU or
headset needed):
```bash
cmake --build . --config Release --target zs_tests
ctest -C Release --output-on-failure
```

### Quest 3 APK
See [VR.md](VR.md), "The Quest build": an Android build of `libmain.so`, then
`android/build-apk.ps1` (Windows) or `android/build-apk.sh` (Linux, as CI does).

## Tested Platforms
* x86-64, EndeavourOS, Ryzen 7 7700X, RX9070XT, 32G RAM
* ARM64, macOS 26.4.1, Apple Silicon M4, Integrated GPU, 24 GB RAM
* riscv64, Ubuntu 24.04 LTS, SIFIVE P550, RX6700XT, 8G RAM
* riscv64, Ubuntu 25.10 LTS, QEMU RVA23, No GPU, 12G RAM

## Troubleshooting
For troubleshooting and common problems see [TROUBLESHOOTING](TROUBLESHOOTING.md).

## Credits and licensing
ZeroSpades X is built on [ZeroSpades](https://github.com/Fran6nd/zerospades)
by Fran6nd, which is a fork of [OpenSpades](https://github.com/yvt/openspades)
by yvt and its contributors. Please see the file named LICENSE.

Thanks to ATELSA for VR testing and ideas, and to Serius_Sim and siecvi for
the zombie models (from [zerospades-userskins](https://github.com/zerospades/zerospades-userskins)).

Note that other assets including sounds and models are not open source.
