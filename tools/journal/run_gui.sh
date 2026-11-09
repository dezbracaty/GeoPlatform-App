#!/bin/bash

# 可选的 GUI 便捷启动器；主要入口仍是 journalsystem_gui.py。

echo "JournalSystem GUI Controller for GPlatform"
echo "====================================="
echo ""

# 切换到项目根目录
cd "$(dirname "$0")/../.." || exit 1

# 检查Python3是否安装
if ! command -v python3 &> /dev/null; then
    echo "错误: Python3 未安装，请先安装 Python3"
    exit 1
fi

# 图形界面依赖由使用者安装，不在启动时修改 Python 环境。
if ! python3 -c "import tkinter, customtkinter, PIL" 2>/dev/null; then
    echo "错误: 缺少 GUI 依赖。Python 需包含 Tk，并安装 CustomTkinter 与 Pillow。"
    echo "python3 -m pip install customtkinter==6.0.0 Pillow"
    exit 1
fi

# 显示使用说明。应用生命周期由用户现有启动方式负责。
echo "使用说明："
echo "1. 首先启动 GPlatform Journal 服务："
echo "   ./build-Release/GPlatform/GPlatform.app/Contents/MacOS/GPlatform --ghost-port 9999"
echo ""
echo "2. 然后运行此 GUI 控制器进行控制"
echo ""
echo "按 Enter 继续启动 GUI..."
read

# 启动GUI
exec python3 tools/journal/journalsystem_gui.py "$@"
