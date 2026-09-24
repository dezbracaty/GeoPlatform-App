#include "CrashHandler.hpp"

#ifdef HAS_BREAKPAD

#include "Foundation/Log.h"
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

CrashHandler* CrashHandler::s_instance = nullptr;

CrashHandler& CrashHandler::instance() {
    if (!s_instance) {
        s_instance = new CrashHandler();
    }
    return *s_instance;
}

bool CrashHandler::initialize(const std::string& dumpPath) {
    m_dumpPath = dumpPath;

    // 确保 dump 目录存在
    QDir dir;
    if (!dir.exists(QString::fromStdString(dumpPath))) {
        if (!dir.mkpath(QString::fromStdString(dumpPath))) {
            LOG_ERROR("Failed to create crash dump directory: {}", dumpPath);
            return false;
        }
    }

#if defined(Q_OS_WIN)
    m_handler = std::make_unique<google_breakpad::ExceptionHandler>(
        std::wstring(dumpPath.begin(), dumpPath.end()),
        nullptr,  // filter
        dumpCallback,
        this,     // callback context
        google_breakpad::ExceptionHandler::HANDLER_ALL
    );
#elif defined(Q_OS_MAC)
    m_handler = std::make_unique<google_breakpad::ExceptionHandler>(
        dumpPath,
        nullptr,  // filter
        dumpCallback,
        this,     // callback context
        true,     // install handler
        nullptr   // port name
    );
#elif defined(Q_OS_LINUX)
    google_breakpad::MinidumpDescriptor descriptor(dumpPath);
    m_handler = std::make_unique<google_breakpad::ExceptionHandler>(
        descriptor,
        nullptr,  // filter
        dumpCallback,
        this,     // callback context
        true,     // install handler
        -1        // server fd
    );
#endif

    LOG_INFO("🛡️ CrashHandler initialized. Dump path: {}", dumpPath);
    return m_handler != nullptr;
}

#if defined(Q_OS_MAC)
bool CrashHandler::dumpCallback(const char* dump_dir,
                               const char* minidump_id,
                               void* context,
                               bool succeeded) {
    if (succeeded) {
        CrashHandler* handler = static_cast<CrashHandler*>(context);
        std::string dumpFile = std::string(dump_dir) + "/" + minidump_id + ".dmp";

        LOG_ERROR("💥 程序崩溃！Minidump 已保存到: {}", dumpFile);

        // 创建崩溃信息文件
        QString crashInfoPath = QString::fromStdString(dump_dir) + "/" + minidump_id + "_info.txt";
        QFile crashInfo(crashInfoPath);
        if (crashInfo.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&crashInfo);
            out << "Crash Information\n";
            out << "=================\n";
            out << "Time: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
            out << "Dump File: " << QString::fromStdString(dumpFile) << "\n";
            out << "\n";
            out << "To analyze this crash:\n";
            out << "1. Use minidump_stackwalk tool from Breakpad\n";
            out << "2. Or upload to crash analysis service\n";
            crashInfo.close();
        }

        // 调用用户回调
        if (handler->m_userCallback) {
            handler->m_userCallback(dumpFile);
        }
    }

    return succeeded;
}
#endif

#if defined(Q_OS_WIN)
bool CrashHandler::dumpCallback(const wchar_t* dump_path,
                               const wchar_t* minidump_id,
                               void* context,
                               EXCEPTION_POINTERS* exinfo,
                               MDRawAssertionInfo* assertion,
                               bool succeeded) {
    if (succeeded) {
        CrashHandler* handler = static_cast<CrashHandler*>(context);
        std::wstring wideDumpFile = std::wstring(dump_path) + L"\\" + minidump_id + L".dmp";
        std::string dumpFile(wideDumpFile.begin(), wideDumpFile.end());

        LOG_ERROR("💥 程序崩溃！Minidump 已保存到: {}", dumpFile);

        // 调用用户回调
        if (handler->m_userCallback) {
            handler->m_userCallback(dumpFile);
        }
    }

    return succeeded;
}
#endif

#if defined(Q_OS_LINUX)
bool CrashHandler::dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
                               void* context,
                               bool succeeded) {
    if (succeeded) {
        CrashHandler* handler = static_cast<CrashHandler*>(context);
        std::string dumpFile = descriptor.path();

        LOG_ERROR("💥 程序崩溃！Minidump 已保存到: {}", dumpFile);

        // 调用用户回调
        if (handler->m_userCallback) {
            handler->m_userCallback(dumpFile);
        }
    }

    return succeeded;
}
#endif

void CrashHandler::setCrashCallback(std::function<void(const std::string&)> callback) {
    m_userCallback = callback;
}

void CrashHandler::testCrash() {
    LOG_WARN("测试崩溃！即将触发一个故意的崩溃...");

    // 触发一个空指针解引用崩溃
    volatile int* p = nullptr;
    *p = 42;
}

CrashHandler::~CrashHandler() {
    m_handler.reset();
}

#endif // HAS_BREAKPAD