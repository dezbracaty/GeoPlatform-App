#!/bin/bash

# GPlatform 自动截图脚本
# 用途：启动程序后自动截图，支持多种截图模式

set -euo pipefail

# ============================================
# 配置项
# ============================================

# The caller selects the product; no repository/build directory is assumed.
APP_PATH=""
APP_PID=""

# 默认配置
DELAY=5                           # 等待窗口显示的延迟时间（秒）
SCREENSHOT_DIR="$HOME/Desktop"    # 默认保存到桌面
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
SCREENSHOT_NAME="GPlatform_screenshot_$TIMESTAMP.png"
MODE="fullscreen"                 # 截图模式：fullscreen, auto, window, interactive
QUIT_AFTER=true                   # 截图后是否关闭程序

# ============================================
# 颜色输出
# ============================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ============================================
# 帮助信息
# ============================================

show_help() {
    cat << EOF
GPlatform 自动截图脚本

用法:
    $0 [选项]

选项:
    --app PATH                要启动的 .app 包或可执行文件（必填）
    -d, --delay SECONDS        等待时间（秒），默认: 5
    -o, --output PATH          输出文件路径，默认: ~/Desktop/GPlatform_screenshot_TIMESTAMP.png
    -m, --mode MODE            截图模式，默认: fullscreen
                               - fullscreen: 截取整个屏幕（推荐，无需交互）
                               - auto: 自动检测并截取 GPlatform 窗口
                               - window: 交互式选择窗口
                               - interactive: 交互式选择区域
    -k, --keep-running         截图后保持程序运行（默认会关闭）
    -h, --help                 显示此帮助信息

示例:
    # 使用默认配置
    $0 --app /path/to/GPlatform.app

    # 指定延迟时间和输出路径
    $0 --app /path/to/GPlatform.app -d 5 -o ~/screenshots/app.png

    # 使用交互式窗口选择
    $0 --app /path/to/GPlatform.app -m window

    # 截图后保持程序运行
    $0 --app /path/to/GPlatform.app --keep-running

EOF
}

# ============================================
# 参数解析
# ============================================

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --app)
                APP_PATH="${2:?Expected application path}"
                shift 2
                ;;
            -d|--delay)
                DELAY="$2"
                shift 2
                ;;
            -o|--output)
                SCREENSHOT_PATH="$2"
                SCREENSHOT_DIR=$(dirname "$SCREENSHOT_PATH")
                SCREENSHOT_NAME=$(basename "$SCREENSHOT_PATH")
                shift 2
                ;;
            -m|--mode)
                MODE="$2"
                shift 2
                ;;
            -k|--keep-running)
                QUIT_AFTER=false
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                log_error "未知参数: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# ============================================
# 检查前置条件
# ============================================

check_prerequisites() {
    if [[ "$APP_PATH" == *.app ]]; then
        APP_PATH="$APP_PATH/Contents/MacOS/GPlatform"
    fi
    # 检查程序是否存在
    if [[ ! -x "$APP_PATH" ]]; then
        log_error "找不到程序: $APP_PATH"
        log_info "请先使用所选 CMake preset 构建，并通过 --app 指定产物"
        exit 1
    fi

    # 检查截图目录是否存在
    if [[ ! -d "$SCREENSHOT_DIR" ]]; then
        log_warning "截图目录不存在，正在创建: $SCREENSHOT_DIR"
        mkdir -p "$SCREENSHOT_DIR"
    fi

    # 检查 screencapture 命令是否可用
    if ! command -v screencapture &> /dev/null; then
        log_error "screencapture 命令不可用（需要 macOS 系统）"
        exit 1
    fi

    log_success "前置检查通过"
}

# ============================================
# 启动程序
# ============================================

start_app() {
    log_info "启动 GPlatform..."
    "$APP_PATH" &
    APP_PID=$!

    log_success "程序已启动 (PID: $APP_PID)"

    # 等待窗口显示
    log_info "等待 $DELAY 秒以确保窗口完全显示..."
    sleep "$DELAY"

    # 确保默认 fullscreen 模式截到 GPlatform，而不是当前前台终端窗口。
    osascript -e 'tell application "GPlatform" to activate' >/dev/null 2>&1 || true
    sleep 1
}

# ============================================
# 截图函数
# ============================================

take_screenshot() {
    local output_path="$SCREENSHOT_DIR/$SCREENSHOT_NAME"

    log_info "准备截图（模式: $MODE）..."

    case $MODE in
        auto)
            # 尝试自动截取 GPlatform 窗口
            log_info "自动检测 GPlatform 窗口..."

            # 方法 1：尝试通过进程名查找窗口 ID
            WINDOW_ID=$(osascript -e 'tell application "System Events" to get id of (first window of (first process whose name is "GPlatform"))' 2>/dev/null || echo "")

            if [[ -n "$WINDOW_ID" ]]; then
                log_info "找到窗口 ID: $WINDOW_ID"
                screencapture -l "$WINDOW_ID" "$output_path"
            else
                log_warning "无法自动检测窗口，使用交互式选择"
                screencapture -w "$output_path"
            fi
            ;;

        window)
            # 交互式选择窗口
            log_info "请点击要截取的窗口..."
            screencapture -w "$output_path"
            ;;

        fullscreen)
            # 截取整个屏幕
            log_info "截取整个屏幕..."
            screencapture "$output_path"
            ;;

        interactive)
            # 交互式选择区域
            log_info "请框选要截取的区域..."
            screencapture -i "$output_path"
            ;;

        *)
            log_error "未知的截图模式: $MODE"
            exit 1
            ;;
    esac

    if [[ -f "$output_path" ]]; then
        log_success "截图已保存: $output_path"

        # 显示文件大小
        local file_size=$(du -h "$output_path" | cut -f1)
        log_info "文件大小: $file_size"

        # 在 Finder 中显示文件
        log_info "在 Finder 中打开截图..."
        open -R "$output_path"
    else
        log_error "截图失败！"
        exit 1
    fi
}

# ============================================
# 清理函数
# ============================================

cleanup() {
    if [[ $QUIT_AFTER == true ]] && [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
        log_info "关闭程序 (PID: $APP_PID)..."
        kill "$APP_PID" 2>/dev/null || true

        # 等待进程结束
        local count=0
        while kill -0 "$APP_PID" 2>/dev/null && [[ $count -lt 5 ]]; do
            sleep 0.5
            count=$((count + 1))
        done

        # 如果还没关闭，强制关闭
        if kill -0 "$APP_PID" 2>/dev/null; then
            log_warning "进程未响应，强制关闭..."
            kill -9 "$APP_PID" 2>/dev/null || true
        fi

        log_success "程序已关闭"
    else
        log_info "保持程序运行状态"
    fi
}

# ============================================
# 信号处理
# ============================================

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ============================================
# 主函数
# ============================================

main() {
    echo ""
    log_info "=========================================="
    log_info "GPlatform 自动截图脚本"
    log_info "=========================================="
    echo ""

    parse_args "$@"
    check_prerequisites
    start_app
    take_screenshot

    echo ""
    log_success "=========================================="
    log_success "截图完成！"
    log_success "=========================================="
    echo ""
}

# 运行主函数
main "$@"
