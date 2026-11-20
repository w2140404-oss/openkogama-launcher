#include "assetinstaller.h"
#include "paths.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static constexpr unsigned long CreateNoWindow = 0x08000000;

AssetInstaller::AssetInstaller(QObject *parent)
    : QObject(parent)
{
}

QString AssetInstaller::versionOf(const QString &instancePath) {
    QFile file(QDir(instancePath).filePath("instance.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonObject info = QJsonDocument::fromJson(file.readAll()).object();
    QString version = info.value("version").toString();
    QString unity = info.value("unityVersion").toString();
    return version.isEmpty() || unity.isEmpty() ? version : version + "@" + unity;
}

void AssetInstaller::start(const QString &version) {
    cancel();
    m_buffer.clear();
    QDir dir(Paths::server());
    if (version.isEmpty() || !dir.exists("openkogama-server.exe")) {
        emit finished(false);
        return;
    }

    QDir().mkpath(Paths::serverData());
    m_process = new QProcess(this);
    m_process->setProgram(dir.filePath("openkogama-server.exe"));
    m_process->setArguments({"install", version});
    m_process->setWorkingDirectory(Paths::serverData());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    QProcess *process = m_process;
    connect(process, &QProcess::readyRead, this, &AssetInstaller::readOutput);
    // A failed-to-start child emits errorOccurred but not necessarily finished.
    // Without this branch the launch dialog may remain at 0% forever.
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process) return;
        const QString reason = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        emit failed(reason);
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_process != process) return;
        readOutput();
        // A progress receiver may have cancelled or restarted the installer.
        if (m_process != process) return;
        m_process = nullptr;
        process->deleteLater();
        emit finished(status == QProcess::NormalExit && code == 0);
    });
    process->start();
}

void AssetInstaller::cancel() {
    if (!m_process) return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
}

void AssetInstaller::readOutput() {
    if (!m_process) return;
    m_buffer += m_process->readAll();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        QList<QByteArray> parts = m_buffer.left(end).trimmed().split(' ');
        m_buffer.remove(0, end + 1);
        if (parts.size() == 3 && parts[0] == "progress")
            emit progress(parts[1].toLongLong(), parts[2].toLongLong());
    }
}
