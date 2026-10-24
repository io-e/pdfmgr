#include "Logging.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace pdfcore {

static QFile* s_logFile = nullptr;
static QMutex s_mutex;
static constexpr qint64 kMaxLogSize = 2 * 1024 * 1024; // 2 MB

QString logDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("logs");
}

static void rotateIfNeeded() {
    if (!s_logFile || s_logFile->size() < kMaxLogSize) return;
    s_logFile->close();
    QString path = s_logFile->fileName();
    QString prev = path + ".1";
    QFile::remove(prev);
    QFile::rename(path, prev);
    s_logFile->setFileName(path);
    s_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const char* level = "???";
    switch (type) {
    case QtDebugMsg:    level = "DBG"; break;
    case QtInfoMsg:     level = "INF"; break;
    case QtWarningMsg:  level = "WRN"; break;
    case QtCriticalMsg: level = "ERR"; break;
    case QtFatalMsg:    level = "FTL"; break;
    }
    QString line = QStringLiteral("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QLatin1StringView(level), msg);
    if (ctx.file) line += QStringLiteral(" (%1:%2)").arg(QLatin1StringView(ctx.file)).arg(ctx.line);
    line += '\n';

    QMutexLocker lock(&s_mutex);
    if (s_logFile && s_logFile->isOpen()) {
        rotateIfNeeded();
        s_logFile->write(line.toUtf8());
        s_logFile->flush();
    }
    fputs(qPrintable(line), stderr);

    if (type == QtFatalMsg) abort();
}

#ifdef Q_OS_WIN
static LONG WINAPI crashDumpHandler(EXCEPTION_POINTERS* ex) {
    QString dumpPath = QDir(logDir()).filePath(
        QStringLiteral("pdfmgr-crash-%1.dmp")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    HANDLE file = CreateFileW(reinterpret_cast<const wchar_t*>(dumpPath.utf16()),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ex;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void initLogging() {
    QDir dir(logDir());
    if (!dir.exists()) dir.mkpath(".");

    static QFile logFile(dir.filePath("pdfmgr.log"));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        s_logFile = &logFile;
        qInstallMessageHandler(messageHandler);
        qInfo() << "PDF Manager" << QCoreApplication::applicationVersion()
                << "started, log:" << logFile.fileName();
    }

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashDumpHandler);
#endif
}

}
