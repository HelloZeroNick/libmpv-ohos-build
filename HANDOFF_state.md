# 交接状态（2026-11-21 12:0x）

## 正在等待的事

GitHub Actions 构建 libmpv，**run id `44863416219`**，repo `HelloZeroNick/libmpv-ohos-build`，
分支 `feat-ohos-surface-geometry`，由 PR #1 的 `pull_request` 触发。

- 启动：2026-11-21 03:53:59Z（≈11:53 CST）
- 观察时点 12:16 CST 仍在 `Bundle` 步骤（历史耗时 22–28 分钟）
- 后台监听任务：`bash-6617b7e0569c4ae6`（跑 `/tmp/watch_ci.sh`，每 30s 轮询，最长 60 分钟）
- 查询命令：
  ```
  gh run view 44863416219 -R HelloZeroNick/libmpv-ohos-build --json status,conclusion
  gh run view 44863416219 -R HelloZeroNick/libmpv-ohos-build --json jobs -q '.jobs[].steps[] | "\(.name): \(.status) \(.conclusion)"'
  ```
- 日志只在 job 结束后可取（live log 会返回 BlobNotFound）。
- `workflow_dispatch` 走不通：远端注册的 workflow schema 是旧版，
  `pgo` 传 `off` 会 422「not in the list of allowed values」。改用 PR 触发。

## 已完成的代码改动（全部已推送）

| 仓库 | 分支 | 提交 | 内容 |
|---|---|---|---|
| `HelloZeroNick/FFmpeg` | `feat-ohos` | `ab0a4e8b0e` | OHCodec 输出 buffer 未归还计数（挂 `OHCodecDecRef`，超 32 告警一次，close 时报剩余+峰值） |
| 同上 | 同上 | `c5e5091ffa` | cherry-pick p1nbored 两个 HDR 提交（`7258ab2983`、`13da1a8d3d`）：前缀 SEI 提取 HDR Vivid/HDR10+ 动态元数据 + HDR10 静态元数据(SEI 137/144) |
| `HelloZeroNick/mpv` | `feat-ohos-surface-geometry` | `bf91958903` | ① `VOCTRL_GET_DISPLAY_FPS` 用 `OH_NativeDisplayManager_GetDefaultDisplayRefreshRate()`；② `eglSwapBuffers` 返回值检查 + 失败按原 `EGLConfig` 重建 surface（`egl_config` 移入 priv） |
| `HelloZeroNick/media-kit` | `support-pilinara` | `8d83f874` | ① 尺寸变更 2% 相对容差；② 250ms 节流（合并最后一次，不丢弃）；③ `_dispose` 在 `VideoOutputManager.Dispose` 后等 16ms 让 raster 线程跑 `OnTextureUnregistered`；④ listener 主体抽成 `_applyVideoParamsSize()`，节流重放重新进 `lock.synchronized` |
| `HelloZeroNick/libmpv-ohos-build` | `feat-ohos-surface-geometry` | `a0b9e3c` | FFmpeg 源改指 `HelloZeroNick/FFmpeg` `feat-ohos`；`mov_text`→`movtext`；demuxer 去掉不存在的 `ssa` |

远端提交已核对：
- FFmpeg `feat-ohos` = `ab0a4e8b0e209c3171d6835414e42bad2ceaf2bb`
- mpv `feat-ohos-surface-geometry` = `bf91958903eeb29ce827ccb83d415c6c5eb7e592`
- media-kit `support-pilinara` = `8d83f874ef83fc267289ebfbf2faedbedf608b9a`

## 本地未提交

`D:\github\PiliNara`：
- `lib/utils/storage_pref.dart` — `videoSync` 默认值 `display-resample` → `audio`（`dart analyze` 通过）
- `pubspec.lock` — `media_kit` 与 `media_kit_video` 已解析到 `8d83f874`；`media_kit_libs_*` 仍在 `fa73cc8c`
- **未提交**。`ohos/build-profile.json5`、`pubspec.yaml` 是用户在此之前就有的改动，不要一起提交。

## 构建成功后的后续步骤（TODO）

1. 下载 artifact（`libmpv_aarch64.zip`），或直接用 workflow 的 `Create release`
   （workflow 里 `release` 输入为 true 才会建 release；本次 dispatch 走的是 PR，未设 release，
   所以只有 artifact）。
2. 算 zip 的 SHA256。
3. 改 `media_kit_libs_ohos/ohos/src/main/cpp/CMakeLists.txt`：
   - `LIBMPV_URL` 指向新 release
   - `LIBMPV_SHA` 换成新值
   （当前是 `20261031` / `d588be72...`，media-kit HEAD `df4f39bd` 已 bump 到 `20261115`）
4. 推 media-kit，更新 PiliNara lock（`dart pub upgrade media_kit_*`），再 `dart analyze`。
5. 提交 PiliNara 的 `storage_pref.dart` + `pubspec.lock`。
6. 构建 HAP 上机验证。

## 关键风险

**libavcodec 62 → 63**（release/5.1 路线 → 合了 release/9.1）。mpv 0.41 的 meson 只要求
`libavcodec >= 60.31.102`，满足；静态检查过 `avcodec_open2`/`av_frame_get_buffer`/
`avcodec_send_packet`/`avcodec_receive_frame` 仍存在。但若 Bundle 在 mpv 阶段失败，
先看是否 9.1 移除了某个 API；退路是把两个 HDR 提交 + buffer 计数回移到 `p1nbored/feat-ohos-hdr`
（libavcodec 62）那条线上。

`ffmpeg.sh` 里组件名已针对 9.1 校验过（decoder/demuxer/parser/bsf/protocol/encoder/muxer 全部 OK，
除 `png` muxer 两版本都不存在、属无害告警）。

## 本次改动的依据（GPU 泄漏根因，已写入报告）

- `D:\github\log21\REPORT.md` — 10:07 击杀事实与时间轴
- `D:\github\log21\APP_SIDE_FIXES.md` — 应用侧逐项排查与优先级
- 强化证据：GPU 8 分钟 48MB→3.27GB（69 倍）；主体是 `host vulkan descriptor set` 952MB/1537 个、
  external memory 573MB、image 289MB；击杀时 PSS 仅 405MB，说明常规内存监控看不见。
- 上一轮我关于 `vsync=1ns` 导致帧调度失控的结论**是错的**：`vo_get_vsync_interval()` 在
  `vsync_interval>1` 不成立时返回 -1，`video.c` 的 `vsync <= 0` 会直接早退，
  `display_sync_active` 保持 false。已在会话中更正。
