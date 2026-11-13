# 启动耗时测量（macOS）

先在完整源码宿主根目录走标准构建：

```sh
cmake --preset arm_release
cmake --build --preset arm_release
```

关闭正在运行的 GPlatform。以下工具仅退出自己创建的测量进程，保留正常保存的机型、配置和页面状态，不使用 Journal 回放模式。

普通启动到真实 Ready 屏障：

```sh
env -u GP_STARTUP_PROFILE python3 App/tests/startup/measure_startup.py \
  --application build-arm64-Release/GPlatform/GPlatform.app/Contents/MacOS/GPlatform \
  --output build-arm64-Release/Testing/startup-normal-new --runs 5
```

函数分段诊断：在同一命令前设置 `GP_STARTUP_PROFILE=1`。日志额外记录预设厂商加载、配置恢复和主窗口挂接的微秒级分段；普通启动不输出这些分段。

控件级诊断使用当前 Qt 安装随附的官方 `qmlprofiler`，以实际 Qt 路径替换示例工具路径：

```sh
python3 App/tests/startup/profile_startup.py \
  --application build-arm64-Release/GPlatform/GPlatform.app/Contents/MacOS/GPlatform \
  --qmlprofiler /path/to/Qt/macos/bin/qmlprofiler \
  --output build-arm64-Release/Testing/startup-qml-new
python3 App/tests/startup/analyze_startup_profile.py \
  --trace build-arm64-Release/Testing/startup-qml-new/startup.qtd \
  --output build-arm64-Release/Testing/startup-qml-new/analysis \
  --build-root build-arm64-Release
```

输出目录必须不存在。QML 录制包含连接握手和事件采集开销，不能用整段录制时间代替普通启动数据。分析同时输出包含子调用的 `inclusiveMs` 和扣除已录制子调用的 `selfMs`，只对后者排名；原生调用和系统等待可能计入对应 QML 事件的自身墙钟时间，这不是 CPU 时间。

分析以首次 `refreshStartupAccount` 调用截断启动之后的工作，校验事件嵌套及独立区间并集，避免重复累加。构造与完成事件的数量不是控件实例数量。App 受保护 QML 的行号按本次 build 的生成文件映射回源码；生成文件必须与轨迹对应，无法精确映射时保留空值。

所有样本保留，包括应用进入后台后等待首帧的样本。进程重新启动不等于系统冷启动；重启后首次启动须由操作者在重启后、打开 App 前使用 `--runs 1 --label post-reboot-first-launch` 单独采样，工具不清理系统缓存。
