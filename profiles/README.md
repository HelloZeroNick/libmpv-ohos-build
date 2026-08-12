# PGO profiles

此目录存放 PGO 优化用的 profile 文件(与代码版本绑定)。

## 使用方式

1. 按 `PGO.md` 流程在真机采集并合并得到 `lib.profdata`
2. 将 `lib.profdata` 提交到本目录(**必须与 ffmpeg/mpv 源码版本同步更新**,源码变更后需重新采集)
3. CI 构建时选择 `pgo: use`,workflow 会自动读取 `profiles/lib.profdata`

## 文件说明

| 文件 | 说明 |
|---|---|
| `lib.profdata` | 合并后的 PGO profile,由 `llvm-profdata merge` 生成 |

> 注意:此文件可能较大(几十 MB),请确保提交时未被 .gitignore 排除。
