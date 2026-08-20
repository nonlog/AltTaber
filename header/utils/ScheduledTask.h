#ifndef SCHEDULEDTASK_H
#define SCHEDULEDTASK_H

#include <QString>
#include <QStringList>

class ScheduledTask {
    static bool runElevatedSelf(const QStringList& args);

public:
    ScheduledTask() = delete;

    static bool createTask(const QString& taskName, bool asAdmin = true,
                           bool requestElevation = false);
    static bool taskExists(const QString& taskName);
    static bool queryTask(const QString& taskName);
    static bool queryTaskRunsElevated(const QString& taskName);
    static bool deleteTask(const QString& taskName, bool requestElevation = false);
};

#endif // SCHEDULEDTASK_H
