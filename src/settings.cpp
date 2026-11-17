#include "settings.h"

#include "paths.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGlobal>

namespace {
const QStringList kMoods = {"any", "energetic", "happy", "calm", "sad"};
const QStringList kActivities = {"any", "wakeup", "commute", "work", "workout", "sleep"};
const QStringList kCharacters = {"favourite", "discover", "popular"};
}

Settings::Settings(const QString &directory, QObject *parent)
    : QObject(parent), m_path((directory.isEmpty() ? Paths::configDir() : directory) + "/settings.json") {
    load();
}

void Settings::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "omawave: ignoring corrupt settings file" << m_path;
        return;
    }
    const auto object = doc.object();
    m_volume = qBound(0, object.value("volume").toInt(70), 100);
    const QString browser = object.value("browser").toString();
    if (!browser.isEmpty())
        m_browser = browser;
    if (const QString v = object.value("waveMood").toString(); kMoods.contains(v))
        m_waveMood = v;
    if (const QString v = object.value("waveActivity").toString(); kActivities.contains(v))
        m_waveActivity = v;
    if (const QString v = object.value("waveCharacter").toString(); kCharacters.contains(v))
        m_waveCharacter = v;
}

void Settings::save() {
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "omawave: cannot save settings" << m_path;
        return;
    }
    file.write(QJsonDocument(QJsonObject{{"volume", m_volume}, {"browser", m_browser}, {"waveMood", m_waveMood},
                                         {"waveActivity", m_waveActivity}, {"waveCharacter", m_waveCharacter}})
                   .toJson());
    file.commit();
}

void Settings::setVolume(int volume) {
    volume = qBound(0, volume, 100);
    if (volume == m_volume)
        return;
    m_volume = volume;
    save();
    emit volumeChanged();
}

void Settings::setBrowser(const QString &browser) {
    if (browser.isEmpty() || browser == m_browser)
        return;
    m_browser = browser;
    save();
    emit browserChanged();
}

void Settings::setWaveMood(const QString &value) {
    if (!kMoods.contains(value) || value == m_waveMood)
        return;
    m_waveMood = value;
    save();
    emit waveChanged();
}

void Settings::setWaveActivity(const QString &value) {
    if (!kActivities.contains(value) || value == m_waveActivity)
        return;
    m_waveActivity = value;
    save();
    emit waveChanged();
}

void Settings::setWaveCharacter(const QString &value) {
    if (!kCharacters.contains(value) || value == m_waveCharacter)
        return;
    m_waveCharacter = value;
    save();
    emit waveChanged();
}
