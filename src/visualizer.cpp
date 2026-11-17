#include <csignal>
#include <sys/prctl.h>
#include "visualizer.h"

#include "paths.h"

#include <QFile>
#include <QStandardPaths>

Visualizer::Visualizer(const QString &cavaPath, QObject *parent)
    : QObject(parent), m_cava(cavaPath.isEmpty() ? QStandardPaths::findExecutable("cava") : cavaPath) {
    setLevels(QList<int>(8, 0));
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer += m_process.readAllStandardOutput();
        int nl;
        QList<int> last;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const auto values = parseLine(m_buffer.left(nl));
            m_buffer.remove(0, nl + 1);
            if (!values.isEmpty())
                last = values;
        }
        // Only the newest frame matters; older ones in the same read are stale.
        if (!last.isEmpty())
            setLevels(last);
    });
    connect(&m_process, &QProcess::started, this, &Visualizer::runningChanged);
    connect(&m_process, &QProcess::finished, this, [this] {
        setLevels(QList<int>(8, 0));
        emit runningChanged();
    });
}

Visualizer::~Visualizer() {
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}

QList<int> Visualizer::parseLine(const QByteArray &line) {
    const auto parts = line.trimmed().split(';');
    QList<int> values;
    for (const auto &part : parts) {
        if (part.isEmpty())
            continue;
        bool ok = false;
        const int v = part.toInt(&ok);
        if (!ok)
            return {};
        values << qBound(0, v, 100);
    }
    return values.size() == 8 ? values : QList<int>{};
}

void Visualizer::setLevels(const QList<int> &values) {
    if (values == m_values)
        return;
    m_values = values;
    m_levels.clear();
    for (int v : values)
        m_levels << v;
    emit levelsChanged();
}

void Visualizer::writeConfig() {
    QFile file(Paths::cavaConfig());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write("[general]\nbars = 8\nframerate = 30\nautosens = 1\n"
               "[input]\nmethod = pipewire\nsource = omawave\n"
               "[output]\nmethod = raw\nraw_target = /dev/stdout\ndata_format = ascii\n"
               "ascii_max_range = 100\nbar_delimiter = 59\nframe_delimiter = 10\n");
}

void Visualizer::setWanted(bool wanted) {
    if (m_cava.isEmpty())
        return;
    if (wanted && m_process.state() == QProcess::NotRunning) {
        writeConfig();
        m_buffer.clear();
        m_process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_process.start(m_cava, {"-p", Paths::cavaConfig()});
    } else if (!wanted && m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
    }
}
