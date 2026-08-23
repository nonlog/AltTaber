#include <QApplication>
#include <windows.h>
#include <QTimer>
#include <qoperatingsystemversion.h>
#include <QStyleHints>
#include <QSettings>
#include "UpdateDialog.h"
#include "widget.h"
#include "utils/winEventHook.h"
#include "utils/Util.h"
#include "utils/TaskbarWheelHooker.h"
#include "utils/KeyboardHooker.h"
#include "utils/ComInitializer.h"
#include "utils/SingleApp.h"
#include "utils/SystemTray.h"
#include "utils/ScheduledTask.h"

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName("AltTaber");
    QCoreApplication::setApplicationVersion(ALTTABER_VERSION);
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    // One-shot elevated helper for startup-task maintenance. This intentionally runs before
    // SingleApp so a normal AltTaber instance can remain active while UAC starts this helper.
    const auto appArgs = a.arguments();
    if (appArgs.size() >= 4 && appArgs.at(1) == "--startup-task-helper") {
        if (!IsUserAnAdmin())
            return 20;
        const QString operation = appArgs.at(2);
        const QString taskName = appArgs.at(3);
        bool ok = false;
        if (operation == "create") {
            const bool elevated = appArgs.size() >= 5 && appArgs.at(4) == "elevated";
            ok = ScheduledTask::createTask(taskName, elevated, false);
        } else if (operation == "delete") {
            ok = ScheduledTask::deleteTask(taskName, false);
        }
        return ok ? 0 : 21;
    }
    SingleApp singleApp("AltTaber-MrBeanCpp");
    if (singleApp.isRunning()) {
        if (!singleApp.sendCommand("show-settings"))
            qWarning() << "Another AltTaber instance is running, but its settings command was unavailable";
        return 0;
    }
    if (!singleApp.startCommandServer([](const QString& command) {
            if (command == "show-settings")
                QTimer::singleShot(0, [] { sysTray.showSettings(); });
        })) {
        qWarning() << "AltTaber settings command server was not started";
    }

    // 其实Qt内部已经初始化了，这里是保险起见
    ComInitializer com; // 初始化COM组件 for 主线程
    qDebug() << qt_error_string(S_OK); // just for fun

    qDebug() << "isUserAdmin" << IsUserAnAdmin();
    qDebug() << "System Version" << QOperatingSystemVersion::current().version();
    sysTray.applyVisibilityFromConfig();
    UpdateDialog::verifyUpdate(a); // 验证更新
    // Keep Qt widgets in sync with the Windows app theme. The switcher itself also reads this
    // registry value directly, so native controls and our custom-painted cards use one theme.
    QSettings personalize(
        R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
        QSettings::NativeFormat);
    const bool useLightTheme = personalize.value("AppsUseLightTheme", 1).toInt() != 0;
    QApplication::styleHints()->setColorScheme(useLightTheme ? Qt::ColorScheme::Light
                                                              : Qt::ColorScheme::Dark);
    qApp->setQuitOnLastWindowClosed(false);
    auto* winSwitcher = new Widget;
    winSwitcher->prepareListWidget(); // 优化：对ListWidget进行预先初始化，首次执行`setCurrentRow`特别耗时(472ms)

    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        unhookWinEvent();
    });

    KeyboardHooker kbHooker(winSwitcher);
    TaskbarWheelHooker tbHooker;
    QObject::connect(&tbHooker, &TaskbarWheelHooker::tabWheelEvent,
                     winSwitcher, &Widget::rotateTaskbarWindowInGroup, Qt::QueuedConnection);
    // QueueConnection is important, ensure async, avoiding blocking the hook process
    QObject::connect(&tbHooker, &TaskbarWheelHooker::leaveTaskbar,
                     winSwitcher, &Widget::clearGroupWindowOrder, Qt::QueuedConnection);

    setWinEventHook([winSwitcher](DWORD event, HWND hwnd) {
        // 某些情况下，Hook拦截不到Alt+Tab（如VMware获取焦点且虚拟机开启时，即便focus在标题栏上）
        // （GPT建议RegisterRawInputDevices，不知道有没有效果，感觉比较危险）
        // 此时需要通过监控前台窗口检测系统的任务切换窗口唤出，并弹出本程序
        // 一旦焦点脱离VMWare，Hook就能正常工作，接下里就可以正常拦截Alt+Tab
        // 而再次连续按下TAB（Alt按住的情况下），也不会出现反复弹出系统任务切换窗口的情况（如果不进行Hook拦截就会这样，所以两种方法结合使用）
        if (event == EVENT_SYSTEM_FOREGROUND/* && hwnd == GetForegroundWindow()*/) { // 前台窗口变化 TODO 记录窗口关闭事件
            // 使用[原生]Alt+TAB呼出任务切换窗口时，会触发两次EVENT_SYSTEM_FOREGROUND事件
            // 第二次是在目标窗口已经切换到前台后触发的，非常诡异
            // ^1 可以用 GetForegroundWindow() || isAltPressed 来二次确认 （或者过滤相邻相同hwnd）

            // 对于Follower启动的CMD，由于Follower先行隐藏，会导致焦点先回落到上个窗口，再到新窗口 （但是用Win键打开的cmd没事）
            // 但是此时GetForegroundWindow()还是上个窗口，可能是更新不及时，所以 ^1-1 处的方案不可行
            // 问题不大，本程序hook了Alt+Tab，已经不会出现两次Event了
            winSwitcher->notifyForegroundChanged(hwnd, Widget::WinEvent);
            auto className = Util::getClassName(hwnd);
            // ForegroundStaging貌似是辅助过渡动画
            // 检测 Alt 按下，防止误判 Win+Tab (任务视图)
            const auto windowTitle = Util::getWindowTitle(hwnd);
            const bool isTaskSwitcher = className == "ForegroundStaging"
                                        || (className == "XamlExplorerHostIslandWindow"
                                            && windowTitle == "Task Switching");
            if (hwnd == GetForegroundWindow() && Util::isKeyPressed(VK_MENU) && isTaskSwitcher) { // 任务切换窗口
                // Newer Windows 11 builds can foreground XamlExplorerHostIslandWindow directly.
                // Require its shell-specific "Task Switching" title to avoid confusing it with
                // ordinary taskbar preview islands that use the same window class.
                qDebug() << "任务切换 detected!" << className;
                int t = 0;
                do {
                    // 等待Windows的任务切换窗口完全获取焦点（显示），再弹出本程序抢夺焦点，否则可能会被抢回去，导致需要retry
                    // retry会导致一个问题：（VMWare中）Alt+Tab唤出AltTaber导致retry后，AltTaber显示时会同时显示Windows中的最后一个焦点窗口（例如资源管理器）
                    // ！但是，这个问题只有在Release模式+管理员权限下才会出现，Debug模式下不会出现，离谱
                    Sleep(10);
                    // 貌似如果不是本进程第一个窗口的话，这招无法前置，比如你在这里new Widget
                    winSwitcher->requestShow();
                    t++;
                    if (t > 1)
                        qDebug() << "Retry" << t;
                    Sleep(10);
                } while (!winSwitcher->isForeground() && t < 5);
            }
        }
    });

    qInfo() << "@WinSwitcher started!";
    return QApplication::exec();
}
