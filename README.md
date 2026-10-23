# Qobuz Patcher

## Disclaimer

Neither this project nor its author is affiliated with or endorsed by QOBUZ or it's owner XANDRIE SA.

## Intro

An automated patcher for creating a Qobuz desktop app that runs natively, so not via proton or wine, on Linux (and other unsupported systems).

This project contains scripts to:

1. Download the Windows executable and extract its bundled Node project.
2. Rebuild the Windows Node project for other systems.
3. Replace the closed-source `juce.node` extension with open-source solutions.
4. Bundle it all back together.

The goal of the project is strictly to make the Qobuz desktop app work on Linux and nothing else.
~~Even though removing some of the non-artist-related telemetry is tempting, no.~~

This project **IS NOT and will never be a tool for pirating content** or any other illegal activity.

## Progress

- [x] automated Qobuz Windows app downloader and extractor
- [x] automated Qobuz electron app patcher
- [ ] `juce.node` replacement
  - [x] proof of concept (via electron, slightly to very buggy)
  - [ ] via electron
  - [ ] via miniaudio (active WIP, way better audio output than via electron)
- [ ] build script for package systems
  - [ ] Nix Packages
  - [ ] flatpack

## Development Notes

### Requirements

- `npm` with any version of `node`
- `7z` (for unpacking Qobuz.exe and the nupkg within)

### Setup

1. Prepare Qobuz app project: `npm run app:update`
2. Rebuild Qobuz app project for local device: `npm run app:rebuild`
3. Patch the Qobuz app project: `npm run app:patch -- -ap (electron,miniaudio)`
4. Run the Qobuz app: `npm run app:run`
5. If everything works: `npm run app:pack`

### Issues

#### Login

The Qobuz login uses the browser for the login and uses a deeplink (qobuzapp://) to pass the token to the app.
This approach requires starting a 2nd instance of the app,
which doesn't work well with development environments.

One working solution for this is to pack the Qobuz app and use the packed version for the login.
Afterwards the local files are setup and the development version directly run via `app:run` also gets past the login screen.

#### Taskbar Icon

The taskbar icon doesn't work yet,
just the miniture version for when the app in minified.
Fixing this is low priority.

### Audioplayer (juce.node) Structure

In the following, the structure of the native `juce.node` extension will be discussed, as this native extension has to be reimplemented to use the Qobuz desktop app on officially unsupported systems.
The functions can be seen by using a debugger,
the signatures were inferred from loggins the input arguments and output results,
and the argument names guessed.

`juce.node` provides a custom audio player-like environment, featuring the following functions:
- open(isNextTrack: bool, filePath: str, format: str) -> bool
- openStream(isNextTrack: bool, sampleData: bytes, format: str, sampleCount: int) -> bool
- close(free: bool)
- play() -> "" (empty string)
- pause()
- status() -> "Playing" | "Stopped" | "Blocked"
- trackInformation() -> {format: str, channel: int, sampleRate: int, bitsPerSample: int, lengthInSamples: int, duration: float}
- hasStreamFinished() -> bool
- releaseResources()
- volume(level: int) -> int
- offset(seconds: float | None) -> float
- lookAheadInSeconds(seconds: float)
- useDevice(name: str, type: str) -> "" (empty string)
- deviceInformation() -> {name: str, type: str}
- getAvailableDeviceList() -> list[{name: str, type: str}]
- setDeviceChangedListener(listener: (devices, device)->())
- clearNextTrack()
- switchToNext()
- initCacheRange(isNextTrack: bool, trackTimeRange: CacheRange, trackDataRange: CacheRange)
- newCacheRanges() -> CacheRange

With the contained `CacheRange` exposing the following functions:
- setCacheSize(size: T)
- addRange(start: T, end: T)
- contains(value: T) -> bool
- isComplete() -> bool
- getRangeCopy() -> list[{start: T, end: T}]
- isEmpty() -> bool

The `CacheRange` serves as a primary progress tracker shared between the native player and the stream handler in JavaScript.
- **`TrackDataCacheRange`** gets initialized (`setCacheSize`) and fed by JavaScript, telling the native extension which parts of the stream are already available.
- **`TrackTimeCacheRange`** gets initialized (`setCacheSize`) by the native extension instead, which sets the cache size to the duration of the related track. JavaScript still adds ranges into it, though.

None of these functions are directly related to existing JUCE classes, but they still use them under the hood (obviously).

####

## Usage of AI

AI was used to assist in the development of the scripts and code.

## LICENSE

This project is licensed under the **GPLv3** license to prevent commercial usage of this patcher.

_Note: This license applies exclusively to the scripts and source code of this project itself, and does not apply to third-party submodules or the external assets downloaded by these scripts._
