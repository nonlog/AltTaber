#ifndef WIN_SWITCHER_SINGLEAPP_H
#define WIN_SWITCHER_SINGLEAPP_H

#include <QSharedMemory>
#include <QDebug>
#include <QString>
#include <QLocalServer>
#include <QLocalSocket>
#include <functional>
#include <utility>

class SingleApp {
private:
    QSharedMemory sharedMemory; // auto detach in destructor
    QLocalServer commandServer;
    QString commandServerName;
    std::function<void(const QString&)> commandHandler;

    void handleCommandSocket(QLocalSocket* socket) {
        const auto command = QString::fromUtf8(socket->readAll()).trimmed();
        if (!command.isEmpty() && commandHandler)
            commandHandler(command);
    }

public:
    explicit SingleApp(const QString& key)
        : sharedMemory(key), commandServerName(key + "-Command") {
        // region Nothing: Just record for future
        // 确保清理可能存在的残留共享内存 for linux:
        // linux 应用程序崩溃后,共享内存段不会自动销毁,则该程序再次运行会出问题
        // Windows 会自动清理
        // if (sharedMemory.attach())
        //    sharedMemory.detach();
        // endregion https://blog.csdn.net/bloke_come/article/details/106319236
    }

    /// check if another instance is running
    bool isRunning() {
        if (sharedMemory.attach()) { // sharedMemory exists
            sharedMemory.detach();
            return true;
        }

        if (sharedMemory.create(1)) { // create sharedMemory (1 byte)
            qInfo() << "SingleApp: sharedMemory created";
            return false;
        } else {
            qWarning() << "fatal: SharedMemory create failed" << sharedMemory.errorString();
            return true; // 保守起见，认为已有实例在运行
        }
    }

    /// Starts a local, per-user command channel for a second launch of the app.
    bool startCommandServer(std::function<void(const QString&)> handler) {
        commandHandler = std::move(handler);
        commandServer.setSocketOptions(QLocalServer::UserAccessOption);
        if (!commandServer.listen(commandServerName)) {
            // A crashed process can leave a stale local-server name behind. The shared-memory
            // lock above proves that this is the primary process before it removes that name.
            QLocalServer::removeServer(commandServerName);
            if (!commandServer.listen(commandServerName)) {
                qWarning() << "SingleApp command server failed:" << commandServer.errorString();
                return false;
            }
        }

        QObject::connect(&commandServer, &QLocalServer::newConnection, &commandServer, [this] {
            while (auto* socket = commandServer.nextPendingConnection()) {
                // Read after the client closes the one-shot connection, so a fragmented named
                // pipe write cannot turn a complete command into a partial, ignored one.
                QObject::connect(socket, &QLocalSocket::disconnected, socket, [this, socket] {
                    handleCommandSocket(socket);
                    socket->deleteLater();
                });
            }
        });
        return true;
    }

    /// Ask the already-running instance to perform a local command.
    bool sendCommand(const QString& command) const {
        QLocalSocket socket;
        socket.connectToServer(commandServerName);
        if (!socket.waitForConnected(500)) {
            qWarning() << "SingleApp command connection failed:" << socket.errorString();
            return false;
        }
        socket.write(command.toUtf8());
        const bool written = socket.waitForBytesWritten(500);
        socket.disconnectFromServer();
        return written;
    }
};

#endif //WIN_SWITCHER_SINGLEAPP_H
