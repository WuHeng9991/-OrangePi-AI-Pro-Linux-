# open-pipe-media-player

基于 OrangePi AI Pro 的 Linux 本地媒体播放器，使用 C、GTK3、GStreamer 与 CMake 构建。本项目聚焦“可运行、可演示、可讲解”的本地音视频播放器能力，适合作为嵌入式 Linux / 音视频方向的二次开发项目。

## 当前能力

- 打开单个本地媒体文件
- 打开目录并自动扫描常见媒体文件
- 播放 / 暂停 / 停止
- 上一首 / 下一首
- 播放进度显示与拖动跳转
- 状态提示与媒体信息展示
- 播放结束后自动切换到下一项
- 支持通过环境变量配置音频输出 sink 与 device

## 项目结构

- `src/main.c`：程序入口
- `src/app.c`：应用编排、播放列表、目录扫描、状态刷新
- `src/player.c`：播放/暂停/停止/seek 等控制接口
- `src/pipeline.c`：GStreamer playbin、音视频输出、bus 消息处理
- `src/ui.c`：GTK 界面、按钮回调、进度条交互
- `include/*.h`：对外声明与共享状态结构

## 依赖安装

Ubuntu / openEuler 类环境可先安装：

```bash
sudo apt-get install -y \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgtk-3-dev \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav \
  gstreamer1.0-gl \
  gstreamer1.0-gtk3 \
  gstreamer1.0-alsa \
  gstreamer1.0-tools
```

## 构建方法

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

构建产物：

```bash
./build/open-pipe-media-player
```

## 运行方式

### 1. 桌面终端直接运行

```bash
./build/open-pipe-media-player
```

### 2. SSH 复用桌面会话运行

```bash
env DISPLAY=:0 XAUTHORITY=/var/run/lightdm/root/:0 \
  ./build/open-pipe-media-player
```

## 音频输出配置

默认使用 `autoaudiosink`。如需手动指定：

```bash
OPMP_AUDIO_SINK=alsasink OPMP_AUDIO_DEVICE=default ./build/open-pipe-media-player
```

可用环境变量：

- `OPMP_AUDIO_SINK`：指定音频 sink，例如 `autoaudiosink`、`alsasink`
- `OPMP_AUDIO_DEVICE`：指定 sink 的 `device` 属性，例如 `default`
- `OPMP_PREFER_GTKSINK`：在支持硬件加速构建时，优先使用 `gtksink`

## 阶段四已完成内容

### 功能增强

- 新增媒体信息面板，展示文件名、状态、总时长
- 新增目录扫描入口
- 新增上一首 / 下一首
- 新增播放列表状态管理
- 新增 EOS 自动切歌
- 新增更友好的中文错误提示

### 展示完善

- 补充 README
- 补充阶段进度文档
- 补充演示脚本、录屏检查清单、简历项目表述

## 建议验证流程

1. 构建项目
2. 打开单文件，确认文件名 / 状态 / 时长展示正常
3. 打开目录，确认会按文件名排序加载媒体
4. 播放中测试上一首 / 下一首
5. 拖动进度条，确认可跳转并继续播放
6. 播放结束时确认自动进入下一项，最后一项停在“播放结束”
7. 切换音频 sink 配置，确认程序仍可启动并给出清晰提示

## 已知限制

- 当前目录扫描仅处理单层目录，不递归扫描
- 媒体信息第一版只展示文件名、状态、总时长，暂未解析更详细的 TAG 信息
- 复杂硬解链路与更多输出设备验证仍需在板端继续补测
