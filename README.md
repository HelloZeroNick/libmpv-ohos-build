# libmpv-ohos-build

Build scripts of [libmpv](https://github.com/mpv-player/mpv) for ohos-arm64 (API 15+).

Scripts are compatible with macOS, Linux and WSL, Windows is not supported.

## Build Dependencies

- git
- make
- python3
- pkg-conf
- gperf
- meson

ohos sdk is automatically downloaded on Linux / WSL, but you need to manually download DevEco Studio on your mac.

## Build

```shell
chmod +x *.sh */*.sh
./bundle.sh
```

## Test

Host-side tests for the HarmonyOS HDR code. Run them after a build
(`./bundle.sh`): besides the sources `./download.sh` fetches, they use the
OHOS SDK headers and the configuration of the libmpv build:

```shell
./tests/run.sh
```
