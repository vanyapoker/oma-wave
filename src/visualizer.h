#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

// Runs cava with raw ASCII output and exposes 8 bar levels (0-100).
class Visualizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit Visualizer(const QString &cavaPath = {}, QObject *parent = nullptr);
    ~Visualizer() override;

    QVariantList levels() const { return m_levels; }
    QList<int> levelValues() const { return m_values; }
    bool available() const { return !m_cava.isEmpty(); }
    bool running() const { return m_process.state() != QProcess::NotRunning; }

    Q_INVOKABLE void setWanted(bool wanted);

    // Returns the 8 levels of a cava line "a;b;...;", or an empty list when malformed.
    static QList<int> parseLine(const QByteArray &line);

signals:
    void levelsChanged();
    void runningChanged();

private:
    void writeConfig();
    void setLevels(const QList<int> &values);

    QString m_cava;
    QProcess m_process;
    QByteArray m_buffer;
    QVariantList m_levels;
    QList<int> m_values;
};
