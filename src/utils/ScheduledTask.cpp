#include "utils/ScheduledTask.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <windows.h>
#include <shellapi.h>

namespace {
QString quoteCommandLineArg(QString value) {
    value.replace("\"", "\\\"");
    return '"' + value + '"';
}

QString xmlUnescape(QString value) {
    value.replace("&quot;", "\"");
    value.replace("&apos;", "'");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    value.replace("&amp;", "&");
    return value;
}

bool runSchtasks(const QStringList& args, QByteArray* stdoutData = nullptr,
                 QByteArray* stderrData = nullptr) {
    QProcess process;
    process.start("schtasks.exe", args);
    if (!process.waitForStarted()) {
        qWarning() << "Failed to start schtasks.exe" << process.errorString();
        return false;
    }
    process.waitForFinished(-1);
    const QByteArray stdOut = process.readAllStandardOutput();
    const QByteArray stdErr = process.readAllStandardError();
    if (stdoutData)
        *stdoutData = stdOut;
    if (stderrData)
        *stderrData = stdErr;
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok)
        qWarning() << "schtasks.exe failed" << args << process.exitCode()
                   << QString::fromLocal8Bit(stdErr);
    return ok;
}

QString decodeOutput(const QByteArray& bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        const auto* data = reinterpret_cast<const char16_t*>(bytes.constData() + 2);
        return QString::fromUtf16(data, (bytes.size() - 2) / 2);
    }
    return QString::fromLocal8Bit(bytes);
}

QString queryTaskXml(const QString& taskName) {
    QByteArray output;
    if (!runSchtasks({"/query", "/tn", taskName, "/xml"}, &output))
        return {};
    return decodeOutput(output);
}

struct TaskInfo {
    QString command;
    QString runLevel;
};

TaskInfo readTaskInfo(const QString& taskXml) {
    TaskInfo info;
    if (taskXml.isEmpty())
        return info;

    static const QRegularExpression commandRe(
        R"(<Command>\s*([^<]*?)\s*</Command>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression runLevelRe(
        R"(<RunLevel>\s*([^<]*?)\s*</RunLevel>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    const auto commandMatch = commandRe.match(taskXml);
    if (commandMatch.hasMatch())
        info.command = xmlUnescape(commandMatch.captured(1).trimmed());
    const auto runLevelMatch = runLevelRe.match(taskXml);
    if (runLevelMatch.hasMatch())
        info.runLevel = runLevelMatch.captured(1).trimmed();
    return info;
}

QString normalizedPath(QString path) {
    path = path.trimmed();
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2);
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool commandMatchesCurrentApp(const QString& command) {
    if (command.isEmpty())
        return false;
    const QString actual = normalizedPath(command);
    const QString expected = normalizedPath(qApp->applicationFilePath());
    const bool matches = actual.compare(expected, Qt::CaseInsensitive) == 0;
    if (!matches)
        qWarning() << "Scheduled task path mismatch" << actual << expected;
    return matches;
}
} // namespace

bool ScheduledTask::runElevatedSelf(const QStringList& args) {
    const std::wstring executable = QDir::toNativeSeparators(qApp->applicationFilePath()).toStdWString();
    QStringList quotedArgs;
    for (const auto& arg: args)
        quotedArgs << quoteCommandLineArg(arg);
    const std::wstring parameters = quotedArgs.join(' ').toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = nullptr;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED)
            qWarning() << "Elevation request cancelled by user";
        else
            qWarning() << "Failed to launch elevated AltTaber helper" << error;
        return false;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = ERROR_GEN_FAILURE;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (exitCode != 0)
        qWarning() << "Elevated AltTaber startup helper failed" << exitCode;
    return exitCode == 0;
}

bool ScheduledTask::createTask(const QString& taskName, bool asAdmin, bool requestElevation) {
    if (requestElevation) {
        return runElevatedSelf({"--startup-task-helper", "create", taskName,
                                asAdmin ? "elevated" : "normal"});
    }

    // Let Task Scheduler bind the logon trigger to the current user. This avoids fragile
    // hand-authored XML while still producing InteractiveToken + HighestAvailable on Windows.
    const QString runLevel = asAdmin ? QStringLiteral("highest") : QStringLiteral("limited");
    const QString executable = QDir::toNativeSeparators(qApp->applicationFilePath());
    return runSchtasks({"/create", "/tn", taskName,
                        "/tr", executable,
                        "/sc", "onlogon",
                        "/rl", runLevel,
                        "/f"});
}

bool ScheduledTask::taskExists(const QString& taskName) {
    QByteArray output;
    return runSchtasks({"/query", "/tn", taskName}, &output);
}

bool ScheduledTask::queryTask(const QString& taskName) {
    const auto info = readTaskInfo(queryTaskXml(taskName));
    return commandMatchesCurrentApp(info.command);
}

bool ScheduledTask::queryTaskRunsElevated(const QString& taskName) {
    const auto info = readTaskInfo(queryTaskXml(taskName));
    return commandMatchesCurrentApp(info.command) &&
           info.runLevel.compare(QStringLiteral("HighestAvailable"), Qt::CaseInsensitive) == 0;
}

bool ScheduledTask::deleteTask(const QString& taskName, bool requestElevation) {
    if (!taskExists(taskName))
        return true;
    if (requestElevation)
        return runElevatedSelf({"--startup-task-helper", "delete", taskName});
    return runSchtasks({"/delete", "/tn", taskName, "/f"});
}
