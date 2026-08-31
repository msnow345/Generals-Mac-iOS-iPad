# Command & Conquer Generals: Zero Hour — macOS, iOS & iPadOS

## About this fork

This fork builds on [ammaarreshi/Generals-Mac-iOS-iPad](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad),
which did the hard work of getting Zero Hour running on iOS at all. Everything in this section
is new here; the rest of this README describes the port it was forked from.

The theme is making the iPad version behave like a **native iOS app** rather than a desktop
RTS being poked with a finger — plus a set of engine fixes found along the way, several of
which affect macOS and Linux too.

### Touch input actually worked out to the wrong place

The first fix was not a feature. `DX8Wrapper::Pillarbox_Setup()` never populated its pixel
density value, because the only code that assigns it sits inside a fallback branch that does
not run when the present parameters are already valid. `Pillarbox_Get_Rect()` therefore
reported the viewport in backbuffer **pixels** instead of window **points**, and the
mouse-coordinate scaling collapsed to an identity mapping.

Every touch landed at roughly **58% of where your finger actually was**, so nothing was ever
under the cursor. It reads as "touch is completely broken" rather than as an offset, and it is
invisible on desktop, where windows are not high-DPI and the density of 1.0 happens to be
correct.

### The camera moves the map, not the view

- **1:1 direct manipulation.** The patch of ground under your fingers stays under your
  fingers. Each frame the cursor's previous and current positions are projected onto the
  terrain and the view shifts by the world-space difference, so it is correct at any zoom and
  under Zero Hour's tilted camera — where a pixel of vertical drag covers noticeably more
  ground than a horizontal one. This replaced the engine's right-drag scrolling, which is a
  *joystick*: it scrolls every frame at a speed set by the cursor's distance from where the
  button went down, and under a finger that reads as the camera accelerating away from you.
- **Inertia.** A flick coasts and decays at `0.998`/millisecond — UIScrollView's normal
  deceleration rate — and a touch catches it. Release velocity is measured over a real 60ms
  window from timestamped samples rather than a per-frame filter, because a filter lags and
  people ease off as they lift, which makes a genuine flick read as a stop. Coast length is
  linear in release speed with no threshold cliff, so a gentle drag ends with a small drift.
- **Pinch zoom is continuous and anchored** between your fingers. It previously emitted a
  mouse-wheel tick per 3% of finger travel, ratcheting the camera about the screen centre.
- **Pan and zoom work together.** The engine locked the gesture to whichever moved further
  first, so you could not pinch mid-drag without lifting. Zoom now has to out-pace the pan to
  engage, which stops a two-finger pan leaking zoom.
- **A new world-space scroll primitive** (`View::scrollByWorld`) sits underneath all of this.
  `scrollBy()` looks like it takes a world delta but does not — `W3DView` reinterprets it as
  device-space pixels on the unit-depth view plane, with an inverted Y axis.

### Placing buildings

Re-mapped onto the engine's own press-anchor-release model, so its placement arrow, wall-run
preview and validation all still work unchanged:

| Gesture | Result |
| --- | --- |
| Drag one finger | Carry the structure under your finger |
| Carry toward a screen edge | Map scrolls; the structure stays under your finger |
| Tap a second finger | Rotation on — aim with the carrying finger |
| Tap it again | Rotation off |
| Hold it (1.2s) | Cancel the build |
| Lift | Place it |

The ghost now appears in the middle of the view rather than wherever you last tapped (with a
mouse the pointer is already over the map; touch has no hover, so it used to appear under the
command bar). Rotation ignores the first ~10mm, because at the moment it engages your finger
is sitting exactly on the anchor and the facing would spin on sub-millimetre movement.

### Menus and lists

- **Lists and dropdowns scroll by dragging them**, pixel-accurately, and a tap still selects.
  Previously only the scrollbar thumb worked, which is a few pixels wide at this resolution.
- **Tap to skip movies.** They were escapable only with the ESC key, which an iPad does not
  have, so any cinematic had to be sat through in full.

### Frame rate

In-game rendering now runs at up to **120fps** instead of the 30fps cap in the 2003
`GameData.ini`, with game logic decoupled and held at 30Hz so simulation speed is unchanged.
Raising the cap alone causes the classic Generals speed-up, because logic otherwise ticks once
per rendered frame; the two have to change together. The cap is re-asserted per frame, since
`GlobalData` is re-parsed from INI on every map load and silently restores it otherwise.

Menu transitions and window slide animations are paced at 30Hz. They advance one step per
*call*, once per rendered frame, so uncapping made them run at four times speed.

### Engine fixes

- **No video played anywhere under DXVK** — the EA logo, mission cinematics, unit cameos, load
  screens and the score screen. `DX8Caps::Support_Texture_Format()` reports every format
  unsupported because its caps table is never populated, so `createVideoBuffer()` returned
  null and both `playLogoMovie()` and `playMovie()` silently returned. Ordinary textures were
  unaffected, which is why it looked like the intro had been deliberately removed. Affects
  macOS and Linux equally.
- **Four uninitialised-memory bugs**, reported by the [Android port](https://github.com/fadi-labib/Generals-Android)
  and verified here: `Pathfinder` freeing an indeterminate pointer during construction,
  `W3DBridgeBuffer` looping over an uninitialised count, `W3DSmudgeManager` releasing
  never-initialised members on its *first* init, and `_Get_DX8_Back_Buffer` wrapping a garbage
  stack pointer when DXVK returns success with no surface. Platform-neutral; desktop survives
  them only because its allocator tends to hand back zeroed pages.
- **N-patch tessellation state guarded to Windows.** DXVK does not implement
  `D3DRS_PATCHSEGMENTS` and logs a warning every time it is set — tens of thousands of lines
  per session, since the render-state cache is invalidated each frame while pillarboxed.
- **Auto-save when iOS suspends the app**, so a memory kill during a long session is no longer
  silent data loss.
- **Hidden the orphaned Custom Mission button**, which rendered as
  `MISSING: 'GUI:CustomMission'` — the 1.04 patch data defines the button but ships no string
  for it, and nothing in the engine drives it.
- **FPS counter and clock default off** on touch, where there is no convenient options file.

<img width="500" height="281" alt="IMG_3457_500" src="https://github.com/user-attachments/assets/aeaf6692-36e6-40c8-b9f8-8066d014ec4b" />

**Zero Hour running natively on Apple Silicon Macs, iPhone, and iPad** — campaign,
skirmish, and Generals Challenge, with touch controls built for RTS (tap-select,
drag-box, long-press deselect, two-finger scroll, pinch zoom). No emulation: this
is the real 2003 engine compiled for ARM64, rendering DirectX 8 →
[DXVK](https://github.com/doitsujin/dxvk) → Vulkan →
[MoltenVK](https://github.com/KhronosGroup/MoltenVK) → Metal.

Built on EA's GPL v3 source release, standing on a chain of community work —
[TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode),
[Fighter19's original Unix port](https://github.com/Fighter19/CnC_Generals_Zero_Hour), and
[fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX) — this fork adds the iOS/iPadOS
port and a set of engine fixes. See [Lineage & credits](#lineage--credits) for who built
what. The original GeneralsX README lives on the `upstream-main` branch.

**No game assets are included or distributed.** You need your own copy
([Steam](https://store.steampowered.com/app/2732960/), ~$5 on sale).

## What this port actually involved

"Porting" undersells how weird this journey was, so here's the honest shape of it.
The lineage below built the foundation: EA's source release, the community's
modernization, Fighter19's original Unix port, GeneralsX's macOS/Linux work.
What did *not* exist was any of this on iOS — and iOS is a hostile place for a
2003 Windows RTS:

- **The engine assumes a writable filesystem wherever it lives.** iOS apps live in a
  read-only, code-signed bundle. Every config write, cache, and save path had to be
  rerouted — and the working directory bootstrapped from the bundle itself.
- **The renderer speaks DirectX 8. The iPad speaks Metal.** In between: DXVK
  translating D3D8→Vulkan, MoltenVK translating Vulkan→Metal — and DXVK had never
  been built for iPhoneOS. That took a Meson cross-build and a patch to its Vulkan
  loader, because iOS confines `dlopen` to the app bundle ([`Patches/dxvk-ios.patch`](Patches/dxvk-ios.patch)).
- **iOS owns your process.** Open the app switcher and the OS seizes the Metal
  drawable *without backgrounding you* — draw one more frame and you're dead on
  resume. The whole render/sim loop learned to hold its breath.
- **An RTS needs a mouse.** SDL3 (from the lineage below) delivers raw touch events;
  the RTS semantics on top are new. Taps defer until the 2003 GUI has processed
  hover (or menu buttons never highlight), a drag has to decide "selection box or
  camera pan," long-press became right-click, and a cancelled touch must never
  ghost-click a rally point.
- **And then the bug hunts** — the best part. The minimap that rendered black
  because a 2003 texture-format fallback silently dropped the alpha channel. The
  EVA voice that went randomly mute because one zombie audio stream held a global
  "don't talk over speech" flag while chirping forever. Every one chased to root
  cause on a real device, fixed, and offered upstream.

**→ The war stories: [Porting Playbook §8 — the bug archaeology](docs/port/PORTING_PLAYBOOK.md#8-post-ship-bug-hunts-junejuly-2026--the-archaeology-section)**
**→ The complete engineering log: [docs/port/PORTING_PLAYBOOK.md](docs/port/PORTING_PLAYBOOK.md)**
**→ How to do this to another game: [docs/port/PORTING_PATTERNS.md](docs/port/PORTING_PATTERNS.md)**

Worth saying plainly: this was a **human + AI collaboration**. The engineering —
the C++, the cross-builds, the device debugging — was done by
[Claude Code](https://claude.com/claude-code) (Anthropic's Claude, Fable model),
directed and playtested by a human who described symptoms like *"the minimap is
black"* and *"I hear chirping"* and owned every decision. Neither half ships this
alone: one of us can't write C++, and the other can't hear the chirping.

## Quick start — macOS

Prerequisites (one time):

Download the MacOS Vulkan SDK from https://vulkan.lunarg.com/sdk/home and install locally.

```sh
# Toolchain
xcode-select --install
brew install cmake ninja meson pkgconf libpng ffmpeg
brew install --cask steamcmd

# vcpkg (full clone — a shallow clone breaks manifest baselines)
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg          # add to your shell profile

# LunarG Vulkan SDK (NOT the Homebrew cask) — https://vulkan.lunarg.com/sdk/home
export VULKAN_SDK=$HOME/VulkanSDK/<version>/macOS   # add to your shell profile
```

Clone, build, get assets, play:

```sh
git clone https://github.com/ammaarreshi/Generals-Mac-iOS-iPad.git GeneralsX
cd GeneralsX
./scripts/build/macos/build-macos-zh.sh     # checks deps, configures, builds
./scripts/build/macos/deploy-macos-zh.sh    # creates ~/GeneralsX/GeneralsZH + run.sh
./scripts/get-assets.sh <your_steam_username>   # fetches game data you own. Note that this will prompt you for your Steam Guard code, etc.
cd ~/GeneralsX/GeneralsZH && ./run.sh -win
```

## Quick start — iPhone / iPad

On top of the macOS prerequisites: full Xcode (signed into your Apple ID),
`brew install xcodegen`, and a (free or paid) Apple Developer team.

```sh
cd GeneralsX
git submodule update --init references/fbraz3-dxvk   # iOS DXVK is built from this + Patches/dxvk-ios.patch
./scripts/build/ios/fetch-moltenvk.sh                # pinned MoltenVK.framework (checksummed)
./scripts/build/ios/stage-fonts.sh                   # Liberation fonts, renamed as the game expects
cmake --preset ios-vulkan
cmake --build build/ios-vulkan --target z_generals
GX_TEAM_ID=<your-team-id> GX_BUNDLE_ID=com.you.generalszh \
    ./scripts/build/ios/package-ios-zh.sh --install  # assembles, signs, installs
```

Assets ship inside the app bundle (self-contained install); `--dev` skips the ~2.7 GB copy for fast code
iteration.

Note that you will need to register your device in XCode before the deployment.

### Finding your Apple Developer Account Team ID

If you have a paid Apple Developer account, you can find your team id in Xcode → Settings → Accounts. 

Altnernatively if you have a free Apple Developer account:

- Open Xcode on your Mac.
- Go to Settings then Accounts.
- Sign in with your Apple ID.
- Click Manage Certificates and add a new certificate.
- Open the Mac app Keychain Access and go to the login keychain.
- Under My Certificates, double click your Apple Development certificate.
- Your Team ID is the string of letters and numbers next to Organizational Unit

### Fixing Missing Vulcan Headers Error

When running `cmake --build build/ios-vulkan --target z_generals` you may see an error referring to missing Vulcan headers. This requires the path to the header include files to be set explicity using the commands below.

```
export C_INCLUDE_PATH=$VULKAN_SDK/include
export CPLUS_INCLUDE_PATH=$VULKAN_SDK/include
```

### Fixing Missing d3d11_4.h file not found Error

When running `cmake --build build/ios-vulkan --target z_generals` you may see an error referring to a missing d3d11_4.h file. 

You can check if the files are missing by running `find references/fbraz3-dxvk -name d3d11_4.h`. If nothing is returned, then this requires fixing the DXVK submodule using the commands below

```
git submodule update --init --recursive
```

### Fixing Bundle Identifier Errors

You may get a Bundle Identifier error when attemtping to run the XCode project from within XCode.

> Failed Registering Bundle Identifier
> The app identifier "me.ammaar.generalszh" cannot be registered to your development team because it is not available. Change your bundle identifier to a unique string to try again.

This can be fixed by changing the Product Bundle Identifier under `Build Settings` -> `Signing & Capabilities` -> `Bundle Identifier` in XCode to the another value - e.g. `me.myname.generalszh`. Save the project but do not attempt to run and deploy the app from XCode at this stage.

### Fixing ERROR: The specified device was not found

You may see ann error when you attempt to install the app using the `./scripts/build/ios/package-ios-zh.sh --install` command.

```
ERROR: The specified device was not found.
(Name: 13-inch)
```

This can be fixed by manually installing the app using `xcrun devicectl device install app --device "YOUR DEVICE NAME" GeneralsX/build/ios-package/GeneralsXZH.app`, where you device name can be obtained by running `xcrun xctrace list devices` (exluding the iOS version number)

### Fixing this application cannot be launched errors

After a number of days, if you are using a free Apple Developer account, your certificate will expire and you will need to rebuilt and re-upload the application.

This can be done by executing the steps below which will re-package and sign the application, after which we will install it using `xcrun devicectl device install app --device "YOUR DEVICE NAME" GeneralsX/build/ios-package/GeneralsXZH.app`

```
GX_TEAM_ID=<your-team-id> GX_BUNDLE_ID=com.you.generalszh \
    ./scripts/build/ios/package-ios-zh.sh
xcrun devicectl device install app --device "YOUR DEVICE NAME" GeneralsX/build/ios-package/GeneralsXZH.app
```



## Where things are

| Path | What it is |
|---|---|
| [`docs/port/PORTING_PLAYBOOK.md`](docs/port/PORTING_PLAYBOOK.md) | The complete engineering log of this port: every failure mode, root cause, fix — start with [§8, the bug archaeology](docs/port/PORTING_PLAYBOOK.md#8-post-ship-bug-hunts-junejuly-2026--the-archaeology-section): the black minimap, the silent EVA lines, and the chirp |
| `docs/port/PORTING_PATTERNS.md` | Generalized methodology for porting classic Windows games to Apple platforms |
| `docs/port/RELEASE_CHECKLIST.md` | Gate for public release |
| `scripts/get-assets.sh` | Steam asset fetcher (your own copy; app 2732960) |
| `scripts/build/macos/`, `scripts/build/ios/` | Build, deploy, packaging pipelines |
| `ios/` | XcodeGen signing-stub project + `ios/config/` (staged Options.ini, dxvk.conf) |
| `Patches/dxvk-ios.patch` | DXVK changes the iOS d3d8/d3d9 dylibs are built from (applied via the local-fork build) |

## Known issues

- Long sessions on iPad can be killed by iOS for memory (~3 GB+ resident); the app
  exits to the home screen with no dialog. Session logs (current + previous) are in
  the Files app under the game's folder. Under investigation.
- Backgrounding mid-game can occasionally crash on iOS — the lifecycle pause covers
  the common paths; a rare race remains. Save often.

## What's next: Renegade 👀

Generals had a chain of giants to stand on. **Command & Conquer: Renegade** — EA's
2002 FPS from the same GPL source release — has far less: no native macOS or iOS
build of the W3D engine has ever shipped (Mac players today go through Wine-based
compatibility layers). The [OpenW3D](https://github.com/w3dhub/OpenW3D) community
project has real cross-platform groundwork — a DXVK wrapper scaffold and SDL3 build
plumbing — with Mac/Linux on its roadmap, and that groundwork is exactly what we
built on.

Same methodology as this repo, much deeper water: OpenW3D's Win32 compat scaffold
expanded by ~3,000 lines (the engine calls raw Windows APIs for file finding,
keyboard state, COM), a case-sensitivity strategy for twenty thousand asset paths,
the DXVK/MoltenVK renderer bring-up, the audio/video stack, and FPS touch controls.
It's playable today — campaign, cinematics, mission scripts — on a Mac and an
iPhone. For scale: this Generals port added ~2,200 lines on top of GeneralsX;
Renegade needed ~6,700 on top of the Windows-only source.

Repo drops soon, with the OpenW3D lineage credited the way this repo credits its
chain. Same rules: GPL v3, bring your own copy, full engineering log.

## Lineage & credits

This port is the newest link in a long chain, and the earlier links did foundational
work that this repo inherits everywhere:

- **Westwood / EA Pacific** — the game; **EA** — the GPL v3 source release
- **[TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)** —
  the community mainline: build modernization, VC6→modern toolchain, and much of the
  cross-platform groundwork, including the FFmpeg video backend authored by
  **[feliwir](https://github.com/feliwir)** (of [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE)),
  who also authored the OpenAL audio device work this port's audio stack builds on
- **[Fighter19/CnC_Generals_Zero_Hour](https://github.com/Fighter19/CnC_Generals_Zero_Hour)** —
  the original Unix/64-bit port: SDL3 platform management, C++17
  filesystem/threading, Freetype/Fontconfig text rendering, and the DXVK approach
  this renderer path descends from
- **[fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX)** — the macOS/Linux port
  this fork builds on directly, integrating and extending the above
- **This fork** — the iOS/iPadOS port (arm64-ios cross-build, DXVK-on-iOS, touch
  controls, app lifecycle, packaging) and engine fixes, offered upstream
- **DXVK, MoltenVK, SDL, OpenAL Soft, FFmpeg, Liberation Fonts** — the load-bearing walls

Engine code **GPL v3** (EA's source release → the chain above → this fork). Game
assets: not included, not licensed here.
