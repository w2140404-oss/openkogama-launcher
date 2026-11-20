#include "languages.h"
#include "links.h"
#include "mainwindow.h"
#include "theme.h"
#include "updater.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMutex>
#include <QTimer>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

namespace {

QString startupLogPath;
QMutex startupLogMutex;
QtMessageHandler previousMessageHandler = nullptr;

// Open and flush each line so a later native crash retains the last phase.
void traceStartup(const QString &step)
{
    QMutexLocker lock(&startupLogMutex);
    QFile log(startupLogPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const QString line = QStringLiteral("%1 [pid=%2] %3\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(QCoreApplication::applicationPid()).arg(step);
    log.write(line.toUtf8());
    log.flush();
}

void logQtMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    traceStartup(QStringLiteral("Qt[%1][%2] %3").arg(int(type))
        .arg(QString::fromLatin1(context.category ? context.category : "default"), message));
    if (previousMessageHandler)
        previousMessageHandler(type, context, message);
    else
        std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
}

QString wineVersion()
{
#ifdef Q_OS_WIN
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using GetWineVersion = const char *(__cdecl *)();
    const auto getVersion = ntdll ? reinterpret_cast<GetWineVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
    if (getVersion) return QString::fromLatin1(getVersion());
#endif
    return {};
}

bool hasArgument(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

}

int main(int argc, char *argv[])
{
    startupLogPath = qEnvironmentVariable("OPENKOGAMA_STARTUP_LOG",
        QDir::tempPath() + "/openkogama-startup.log");
    traceStartup("process entry");
    previousMessageHandler = qInstallMessageHandler(logQtMessage);

    const bool smokeTest = hasArgument(argc, argv, "--qt-smoke-test");
    const bool diagnosticMode = hasArgument(argc, argv, "--diagnostic");
    const QString wine = wineVersion();
    traceStartup(QStringLiteral("build=%1 Qt compiled=%2 runtime=%3 pointer-bits=%4 Wine=%5 smoke=%6 diagnostic=%7")
        .arg(QStringLiteral(OPENKOGAMA_BUILD_REVISION), QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion()))
        .arg(sizeof(void *) * 8).arg(wine.isEmpty() ? "no" : wine).arg(smokeTest).arg(diagnosticMode));

    int smokeTimeoutMs = 0;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        const QString prefix = QStringLiteral("--smoke-test-timeout-ms=");
        if (!argument.startsWith(prefix)) continue;
        bool ok = false;
        smokeTimeoutMs = argument.mid(prefix.size()).toInt(&ok);
        if (!smokeTest || !ok || smokeTimeoutMs < 1 || smokeTimeoutMs > 60000) {
            traceStartup("invalid smoke timeout: use --qt-smoke-test --smoke-test-timeout-ms=1..60000");
            return 2;
        }
    }

    if (diagnosticMode && !qEnvironmentVariableIsSet("QT_DEBUG_PLUGINS"))
        qputenv("QT_DEBUG_PLUGINS", "1");

    // Test a narrowly scoped Qt Windows platform compatibility mode on Wine.
    // Can be overridden by setting QT_QPA_PLATFORM externally.
    if (!wine.isEmpty() && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "windows:darkmode=0");
        traceStartup("Wine platform mode: windows:darkmode=0");
    }
    for (const char *name : {"QT_QPA_PLATFORM", "QT_QPA_PLATFORMTHEME", "QT_PLUGIN_PATH",
             "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_STYLE_OVERRIDE", "QT_DEBUG_PLUGINS", "QT_LOGGING_RULES"})
        traceStartup(QString::fromLatin1(name) + '=' + qEnvironmentVariable(name));

    if (!smokeTest && !diagnosticMode) {
        traceStartup("before Velopack startup");
        Updater::startup();
        traceStartup("after Velopack startup");
    }

    traceStartup("before QApplication");
    QApplication a(argc, argv);
    traceStartup("after QApplication");
    traceStartup("QPA platform=" + QGuiApplication::platformName());
    const auto run = [&a]() {
        QTimer::singleShot(0, &a, []() { traceStartup("event loop entered"); });
        traceStartup("before event loop");
        const int result = a.exec();
        traceStartup(QStringLiteral("event loop exited: %1").arg(result));
        return result;
    };

    if (smokeTest) {
        traceStartup("before smoke window construction");
        QLabel label(QStringLiteral("OpenKoGaMa Qt %1 smoke test: window created successfully.").arg(QString::fromLatin1(qVersion())));
        label.setWindowTitle("OpenKoGaMa Qt smoke test");
        label.setMinimumSize(420, 100);
        traceStartup("before smoke window show");
        label.show();
        traceStartup("after smoke window show");
        if (smokeTimeoutMs)
            QTimer::singleShot(smokeTimeoutMs, &a, &QCoreApplication::quit);
        return run();
    }

    const QString link = Links::find(a.arguments());
    if (!link.isEmpty() && Links::forward(link)) return 0;
    traceStartup("before style");
    a.setStyle(Theme::style());
    a.setOrganizationName("OpenKogama");
#ifdef OPENKOGAMA_PORTABLE_TEST_BUILD
    a.setApplicationName("OpenKogama Launcher Whisky Test");
#else
    a.setApplicationName("OpenKogama Launcher");
#endif
    traceStartup("before theme application");
    Theme::apply(a);
    traceStartup("before language installation");
    Languages::install(a);
#ifndef OPENKOGAMA_PORTABLE_TEST_BUILD
    Links::registerScheme();
#endif
    traceStartup("before MainWindow construction");
    MainWindow w;
    traceStartup("after MainWindow construction");
    if (diagnosticMode || Links::importOf(link)) w.skipStartupLaunch();
    Links::listen(&w, [&w](const QString &opened) { w.openLink(opened); });
    traceStartup("before MainWindow show");
    w.show();
    traceStartup("after MainWindow show");
    if (!diagnosticMode) w.checkForUpdates();
    if (!link.isEmpty()) QTimer::singleShot(0, &w, [&w, link]() { w.openLink(link); });
    return run();
}
