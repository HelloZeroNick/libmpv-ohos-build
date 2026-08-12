# LTO / PGO 优化使用指南

本项目使用 **毕昇编译器 (BiSheng)** 构建 libmpv。毕昇基于 LLVM,支持增强版 LTO 和信号量触发的 PGO。

> 参考: [华为文档-毕昇编译器](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/bisheng-compiler)

---

## 1. LTO(链接时优化)

### 开启

```bash
LTO=1 ./bundle.sh
```

### 效果

- 所有 meson 项目(mpv/libplacebo/dav1d/libass 等)注入 `b_lto = true`
- ffmpeg、shaderc、make 项目注入 `-flto`
- 毕昇 LTO 相比开源 LLVM 在 inline 算法、指令预取、plt-inline 上有增强

### 注意

- `-flto` 默认 full 模式,编译时间显著增加
- 若某个组件 LTO 编译失败(如内联汇编冲突),可只对该组件去掉 `-flto`(例如把 ffmpeg.sh 的 `$LTO_CFLAGS` 删掉)

---

## 2. PGO(基于配置文件的优化)

PGO 需要三步:**插桩构建 → 真机采集 → 优化构建**。

### 2.1 插桩构建

```bash
PGO=instrument ./bundle.sh
```

产物 `libmpv/arm64-build/libmpv.so` 为插桩版,运行时会把执行计数器写入真机沙箱:
`/data/storage/el2/base/files`(真机映射为 `/data/app/el2/100/base/<bundleName>/files`)

**验证插桩是否成功**(有 `llvm_prf` 段即为成功):

```bash
/sdk/command-line-tools/sdk/default/hms/native/BiSheng/bin/llvm-objdump -h libmpv.so | grep llvm_prf
```

### 2.2 集成到 PiliPlus 并真机运行

1. 把插桩版 `libmpv.so` 替换到 PiliPlus OHOS 工程的 `ohos/entry/oh_modules/media_kit_libs_ohos/libs/arm64-v8a/libmpv.so`(或你实际使用的打包路径)
2. 构建 debug 包安装到真机
3. 播放**代表性场景**,覆盖实际使用路径:
   - B站点播 h264 / hevc(不同清晰度、倍速)
   - B站直播(http-flv / http-hls)
   - 拖动进度、暂停/继续
4. 建议每个场景运行 1~3 分钟,让热点充分执行

### 2.3 触发 profile 写入(信号量方式)

毕昇 PGO 通过 **SIGUSR2** 信号触发计数器落盘。两种方式:

#### 方式 A:hdc shell(最简单)

```bash
# 先获取 PiliPlus 进程 pid
hdc shell "ps -ef | grep piliplus"   # 或 hdc shell pidof <包名>
# 发送信号,触发写 profile
hdc shell "kill -USR2 <pid>"
```

每发送一次 SIGUSR2,会生成一个 `default*_profile` 文件(覆盖同名文件)。**建议在不同播放阶段多次发送**,每次发完立即拉取,避免被覆盖。

#### 方式 B:DevEco Studio lldb(Native debug)

1. DevEco 中配置 debug Type 为 **Native**,以 debug 模式启动应用
2. 在 lldb 中屏蔽信号量暂停(避免业务信号干扰):
   ```
   (lldb) process handle SIG* -s false
   ```
3. 播放目标场景后,发送信号:
   ```
   (lldb) process signal SIGUSR2
   ```

### 2.4 拉取 profile 并合并

```bash
# 拉取 profile 文件(应用沙箱 files 目录)
hdc file recv /data/app/el2/100/base/<bundleName>/files/default_*_profile ./profiles/

# 合并为一个 .profdata
/sdk/command-line-tools/sdk/default/hms/native/BiSheng/bin/llvm-profdata \
  merge --output=lib.profdata ./profiles/default_*_profile
```

> 注意:一次应用启动产生的 profile 是同名的(多次信号覆盖同名文件),若采集了多段场景,每段发完信号后**立即**用 `hdc file recv` 拉走,再继续下一段。

### 2.5 提交 profile 并优化构建

profile 与代码版本强绑定,**提交到 git 仓库的 `profiles/` 目录**(与 ffmpeg/mpv 源码同版本,避免 CI 与本地 profile 漂移):

```bash
# 合并后的 profile 放入仓库
mkdir -p profiles
cp lib.profdata profiles/lib.profdata
git add profiles/lib.profdata
git commit -m "chore: update PGO profile (ffmpeg/mpv <版本>)"
git push
```

然后两种方式优化构建:

#### 方式 A:CI(GitHub Actions)

workflow 选择 `pgo: use`,自动读取仓库 `profiles/lib.profdata`:

```bash
PGO=use:$GITHUB_WORKSPACE/profiles/lib.profdata ./bundle.sh
```

> 确保 `profiles/lib.profdata` 已提交且未被 `.gitignore` 排除(当前 `.gitignore` 仅排除 `libmpv/` 构建产物目录,不影响)。

#### 方式 B:本地

```bash
PGO=use:/path/to/your/repo/profiles/lib.profdata ./bundle.sh
```

---

## 3. LTO + PGO 组合

可同时使用:

```bash
# 插桩版(带 LTO)
LTO=1 PGO=instrument ./bundle.sh
# 采集后优化版(带 LTO)
LTO=1 PGO=use:/path/to/lib.profdata ./bundle.sh
```

---

## 4. 重要注意事项

| 事项 | 说明 |
|---|---|
| **profile 与代码版本绑定** | 源码变更(尤其 ffmpeg/mpv 更新)后必须重新采集,否则优化无效甚至报错 |
| **插桩版不要发布** | 插桩版体积更大、性能更差(计数器写入),仅用于采集 |
| **采样场景代表性** | 采集数据只优化"你播过的内容类型"。若采集全是 h264,hevc 分支可能被优化掉 |
| **多库共享目录** | ffmpeg/mpv/libass 等都写同一沙箱目录,按 pid 区分文件名;同 pid 内多库同名文件会互相覆盖 → **每段及时拉取** |
| **合并工具版本** | 必须用与构建工具链同版本的 `llvm-profdata`(BiSheng bin 下) |
| **profile 提交仓库** | `profiles/lib.profdata` 必须随代码版本一起提交,CI 的 `pgo: use` 直接读取该文件 |
| **LTO 失败排查** | 某组件报 LTO 相关错误时,临时去掉该组件的 `-flto` 验证 |
| **CI 采集** | CI 无法采集真机 profile,采集必须在本地(有真机 + hdc)完成,再把 `.profdata` 提交到仓库供 CI 使用 |

---

## 5. 快速核对清单

```bash
# 1. 插桩构建
PGO=instrument ./bundle.sh
llvm-objdump -h libmpv/arm64-build/libmpv.so | grep llvm_prf   # 应看到该段

# 2. 真机: 替换 .so → debug 包 → 播放场景 → kill -USR2 → hdc file recv(及时拉)

# 3. 合并
llvm-profdata merge --output=lib.profdata default_*_profile

# 4. 提交 profile 到仓库 (与代码版本同步)
mkdir -p profiles && cp lib.profdata profiles/lib.profdata

# 5. 优化构建 (CI 选 pgo:use 自动读仓库 profiles/lib.profdata)
PGO=use:/path/to/repo/profiles/lib.profdata ./bundle.sh
```
