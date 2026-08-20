#include "utils/ScheduledTask.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <QXmlStreamReader>

#include <windows.h>
#include <shellapi.h>

namespace {
QString xmlEscape(const QString& value) {
    return value.toHtmlEscaped();
}

QString quoteCommandLineArg(QString value) {
    value.replace('"', R"(\")");
    return '"' + value + '"';
}

bool runSchtasks(const QStringList& args, bool elevated, QByteArray* stdoutData = nullptr,
                 QByteArray* stderrData = nullptr) {
    if (!elevated) {
        QProcess process;
        process.start("schtasks.exe", args);
        if (!process.waitForStarted()) {
            qWarning() << "Failed to start schtasks.exe" << process.errorString();
            return false;
        }
        process.waitForFinished(-1);
        if (stdoutData)
            *stdoutData = process.readAllStandardOutput();
        if (stderrData)
            *stderrData = process.readAllStandardError();
        const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        if (!ok)
            qWarning() << "schtasks.exe failed" << args << process.exitCode()
                       << QString::fromLocal8Bit(process.readAllStandardError());
        return ok;
    }

    QStringList quoted;
    quoted.reserve(args.size());
    for (const auto& arg: args)
        quoted << quoteCommandLineArg(arg);
    const std::wstring parameters = quoted.join(' ').toStdWString();

    wchar_t systemDir[MAX_PATH]{};
    const UINT systemDirLen = GetSystemDirectoryW(systemDir, MAX_PATH);
    const QString schtasksPath = systemDirLen > 0
        ? QDir::toNativeSeparators(QString::fromWCharArray(systemDir) + "/schtasks.exe")
        : QStringLiteral("schtasks.exe");
    const std::wstring executable = schtasksPath.toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
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
            qWarning() << "ShellExecuteExW(schtasks.exe) failed" << error;
        return false;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = ERROR_GEN_FAILURE;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (exitCode != 0)
        qWarning() << "Elevated schtasks.exe failed with exit code" << exitCode;
    return exitCode == 0;
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
    if (!runSchtasks({"/query", "/tn", taskName, "/xml"}, false, &output))
        return {};
    return decodeOutput(output);
}

QPair<QString, QString> readTaskCommandAndRunLevel(const QString& taskXml) {
    QString command;
    QString runLevel;
    QXmlStreamReader xml(taskXml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QStringLiteral("Command"))
            command = xml.readElementText().trimmed();
        else if (xml.name() == QStringLiteral("RunLevel"))
            runLevel = xml.readElementText().trimmed();
    }
    if (xml.hasError())
        qWarning() << "Failed to parse scheduled task XML" << xml.errorString();
    return {command, runLevel};
}
}

/// Query Author and User ID via PowerShell for schtasks.exe XML.
QPair<QString, QString> ScheduledTask::queryAuthorUserId() {
    const QString command = R"(
        $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
        $author = $identity.Name
        $sid = $identity.User.Value
        Write-Output "$author`n$sid"
    )";
    QProcess process;
    process.start("powershell.exe", QStringList() << "-NoProfile" << "-Command" << command);
    process.waitForFinished(-1);

    const auto output = process.readAllStandardOutput();
    const auto list = QString::fromLocal8Bit(output).replace("\r\n", "\n").split('\n', Qt::SkipEmptyParts);
    if (list.size() != 2) {
        qWarning() << "Failed to query current user identity for scheduled task" << list;
        return {};
    }
    return {list.at(0), list.at(1)};
}

// Start at user logon. XML is used because schtasks.exe's command-line form defaults to a
// below-normal task priority; this keeps AltTaber at normal interactive priority.
QString ScheduledTask::createTaskXml(const QString& exePath, const QString& description,
                                     bool asAdmin, int priority) {
    const QString isoTime = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const auto [author, userId] = queryAuthorUserId();
    if (author.isEmpty() || userId.isEmpty())
        return {};

    return QString(R"xml(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
    <RegistrationInfo>
        <Date>%1</Date>
        <Author>%2</Author>
        <Description>%3</Description>
    </RegistrationInfo>
    <Triggers>
        <LogonTrigger>
            <Enabled>true</Enabled>
        </LogonTrigger>
    </Triggers>
    <Principals>
        <Principal id="Author">
            <UserId>%4</UserId>
            <LogonType>InteractiveToken</LogonType>
            <RunLevel>%5</RunLevel>
        </Principal>
    </Principals>
    <Settings>
        <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
        <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
        <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
        <AllowHardTerminate>false</AllowHardTerminate>
        <StartWhenAvailable>false</StartWhenAvailable>
        <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
        <IdleSettings>
            <StopOnIdleEnd>false</StopOnIdleEnd>
            <RestartOnIdle>false</RestartOnIdle>
        </IdleSettings>
        <AllowStartOnDemand>true</AllowStartOnDemand>
        <Enabled>true</Enabled>
        <Hidden>false</Hidden>
        <RunOnlyIfIdle>false</RunOnlyIfIdle>
        <WakeToRun>false</WakeToRun>
        <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
        <Priority>%6</Priority>
    </Settings>
    <Actions Context="Author">
        <Exec>
            <Command>%7</Command>
        </Exec>
    </Actions>
</Task>
)xml").arg(xmlEscape(isoTime), xmlEscape(author), xmlEscape(description), xmlEscape(userId),
           asAdmin ? QStringLiteral("HighestAvailable") : QStringLiteral("LeastPrivilege"),
           QString::number(priority), xmlEscape(QDir::toNativeSeparators(exePath)));
}

bool ScheduledTask::createTask(const QString& taskName, bool asAdmin, bool requestElevation) {
    const auto xml = createTaskXml(qApp->applicationFilePath(),
                                   asAdmin ? "AltTaber startup as Administrator" : "AltTaber startup",
                                   asAdmin);
    if (xml.isEmpty())
        return false;

    QTemporaryFile file(QDir::tempPath() + "/AltTaber-schtasks-XXXXXX.xml");
    file.setAutoRemove(true);
    if (!file.open()) {
        qWarning() << "Failed to create temporary scheduled-task XML" << file.errorString();
        return false;
    }

    // Match the XML declaration and Windows Task Scheduler's preferred encoding.
    QByteArray encoded;
    encoded.reserve(2 + xml.size() * 2);
    encoded.append(char(0xFF));
    encoded.append(char(0xFE));
    const auto* utf16 = reinterpret_cast<const char*>(xml.utf16());
    encoded.append(utf16, xml.size() * 2);
    if (file.write(encoded) != encoded.size()) {
        qWarning() << "Failed to write scheduled-task XML" << file.errorString();
        return false;
    }
    file.flush();
    const QString xmlPath = QDir::toNativeSeparators(file.fileName());
    file.close();

    const bool ok = runSchtasks({"/create", "/tn", taskName, "/xml", xmlPath, "/f"}, requestElevation);
    return ok && queryTask(taskName) && (!asAdmin || queryTaskRunsElevated(taskName));
}

bool ScheduledTask::taskExists(const QString& taskName) {
    QByteArray output;
    return runSchtasks({"/query", "/tn", taskName}, false, &output);
}

bool ScheduledTask::queryTask(const QString& taskName) {
    const auto [command, runLevel] = readTaskCommandAndRunLevel(queryTaskXml(taskName));
    if (command.isEmpty())
        return false;

    const QString expected = QDir::toNativeSeparators(qApp->applicationFilePath());
    const bool matches = QDir::toNativeSeparators(command).compare(expected, Qt::CaseInsensitive) == 0;
    if (!matches)
        qWarning() << "Scheduled task path mismatch" << command << expected;
    Q_UNUSED(runLevel);
    return matches;
}

bool ScheduledTask::queryTaskRunsElevated(const QString& taskName) {
    const auto [command, runLevel] = readTaskCommandAndRunLevel(queryTaskXml(taskName));
    if (command.isEmpty())
        return false;
    const QString expected = QDir::toNativeSeparators(qApp->applicationFilePath());
    return QDir::toNativeSeparators(command).compare(expected, Qt::CaseInsensitive) == 0 &&
           runLevel.compare(QStringLiteral("HighestAvailable"), Qt::CaseInsensitive) == 0;
}

bool ScheduledTask::deleteTask(const QString& taskName, bool requestElevation) {
    if (!taskExists(taskName))
        return true;
    return runSchtasks({"/delete", "/tn", taskName, "/f"}, requestElevation);
}
