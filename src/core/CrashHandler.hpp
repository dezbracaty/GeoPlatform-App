#pragma once

#include <string>
#include <memory>
#include <functional>
#include <QtGlobal> // For Q_OS_* macros

// Only include if Breakpad is enabled
#ifdef HAS_BREAKPAD

#if defined(Q_OS_WIN)
    #include "client/windows/handler/exception_handler.h"
#elif defined(Q_OS_MAC)
    #include "client/mac/handler/exception_handler.h"
#elif defined(Q_OS_LINUX)
    #include "client/linux/handler/exception_handler.h"
#endif

/**
 * @brief 崩溃处理器 - 使用 Google Breakpad 捕获崩溃信息
 *
 * 功能：
 * - 自动捕获程序崩溃
 * - 生成 minidump 文件
 * - 支持自定义回调
 * - 跨平台支持 (Windows/macOS/Linux)
 */
class CrashHandler {
public:
    /**
     * @brief 获取单例实例
     */
    static CrashHandler& instance();

    /**
     * @brief 初始化崩溃处理器
     * @param dumpPath 崩溃转储文件的保存路径
     * @return 是否初始化成功
     */
    bool initialize(const std::string& dumpPath);

    /**
     * @brief 设置崩溃后的回调函数
     * @param callback 回调函数，参数为 dump 文件路径
     */
    void setCrashCallback(std::function<void(const std::string& dumpPath)> callback);

    /**
     * @brief 手动触发一个崩溃（用于测试）
     */
    void testCrash();

    /**
     * @brief 获取崩溃转储路径
     */
    const std::string& getDumpPath() const { return m_dumpPath; }

private:
    CrashHandler() = default;
    ~CrashHandler();

    // 禁止拷贝和赋值
    CrashHandler(const CrashHandler&) = delete;
    CrashHandler& operator=(const CrashHandler&) = delete;

    // 崩溃回调函数（平台特定）
#if defined(Q_OS_WIN)
    static bool dumpCallback(const wchar_t* dump_path,
                           const wchar_t* minidump_id,
                           void* context,
                           EXCEPTION_POINTERS* exinfo,
                           MDRawAssertionInfo* assertion,
                           bool succeeded);
#elif defined(Q_OS_MAC)
    static bool dumpCallback(const char* dump_dir,
                           const char* minidump_id,
                           void* context,
                           bool succeeded);
#elif defined(Q_OS_LINUX)
    static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
                           void* context,
                           bool succeeded);
#endif

    // 成员变量
    std::unique_ptr<google_breakpad::ExceptionHandler> m_handler;
    std::function<void(const std::string&)> m_userCallback;
    std::string m_dumpPath;
    static CrashHandler* s_instance;
};

#else // !HAS_BREAKPAD

// 当 Breakpad 未启用时的占位符类
class CrashHandler {
public:
    static CrashHandler& instance() {
        static CrashHandler instance;
        return instance;
    }

    bool initialize(const std::string&) { return false; }
    void setCrashCallback(std::function<void(const std::string&)>) {}
    void testCrash() {}
    const std::string& getDumpPath() const {
        static std::string empty;
        return empty;
    }
};

#endif // HAS_BREAKPAD