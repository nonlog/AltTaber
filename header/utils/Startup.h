#ifndef WIN_SWITCHER_STARTUP_H
#define WIN_SWITCHER_STARTUP_H

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <shlobj_core.h>

#include "ScheduledTask.h"

class Startup {
public:
    enum class Mode {
        Disabled,
        Normal,
        Elevated
    };

    Startup() = delete;

    static Mode mode() {
        if (ScheduledTask::queryTaskRunsElevated(SCHTASK_NAME))
            return Mode::Elevated;
        if (isOn_reg())
            return Mode::Normal;
        return Mode::Disabled;
    }

    static bool isOn() {
        return mode() != Mode::Disabled;
    }

    static bool isElevatedOn() {
        return mode() == Mode::Elevated;
    }

    static bool setMode(Mode wantedMode) {
        const bool needElevation = !IsUserAnAdmin();

        if (wantedMode == Mode::Elevated) {
            // Create the replacement first. If the UAC prompt is cancelled, preserve any existing
            // normal startup entry rather than silently disabling startup altogether.
            if (!ScheduledTask::createTask(SCHTASK_NAME, true, needElevation))
                return false;
            off_reg();
            return mode() == Mode::Elevated;
        }

        if (ScheduledTask::taskExists(SCHTASK_NAME)) {
            if (!ScheduledTask::deleteTask(SCHTASK_NAME, needElevation))
                return false;
        }

        if (wantedMode == Mode::Normal) {
            on_reg();
            return mode() == Mode::Normal;
        }

        off_reg();
        return mode() == Mode::Disabled;
    }

    static bool on() {
        return setMode(Mode::Normal);
    }

    static bool off() {
        return setMode(Mode::Disabled);
    }

    static bool toggle() {
        return isOn() ? off() : on();
    }

    static bool set(bool enabled) {
        return enabled ? on() : off();
    }

private:
    static QString applicationPath() {
        return QDir::toNativeSeparators(QApplication::applicationFilePath());
    }

    static void on_reg() {
        QSettings reg(REG_AUTORUN, QSettings::NativeFormat);
        reg.setValue(REG_APP_NAME, applicationPath());
    }

    static void off_reg() {
        QSettings reg(REG_AUTORUN, QSettings::NativeFormat);
        reg.remove(REG_APP_NAME);
    }

    static bool isOn_reg() {
        QSettings reg(REG_AUTORUN, QSettings::NativeFormat);
        const auto appPath = applicationPath();
        const auto path = reg.value(REG_APP_NAME);
        if (path.isValid() && path.toString() != appPath)
            qWarning() << "REG: AutoRun path mismatch:" << path.toString() << appPath;
        return path.toString().compare(appPath, Qt::CaseInsensitive) == 0;
    }

private:
    inline static const auto REG_AUTORUN =
        R"(HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run)";
    inline static const auto REG_APP_NAME = "AltTaber.MrBeanCpp";
    inline static const auto SCHTASK_NAME = "AltTaber Startup";
};

#endif // WIN_SWITCHER_STARTUP_H
