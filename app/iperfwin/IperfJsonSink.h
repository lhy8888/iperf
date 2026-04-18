#pragma once

#include "IperfGuiTypes.h"
#include "IperfJsonParser.h"

#include <QObject>

class IperfJsonSink : public QObject
{
    Q_OBJECT

public:
    explicit IperfJsonSink(QObject *parent = nullptr);

    void clear();
    QVector<IperfGuiEvent> events() const;
    QString lastRawJson() const;
    IperfGuiEvent lastSummaryEvent() const;
    QString lastErrorMessage() const;

public slots:
    void enqueueJson(const QString &jsonText);

signals:
    void eventParsed(const IperfGuiEvent &event);
    void sessionReset();

private:
    QVector<IperfGuiEvent> m_events;
    QString m_lastRawJson;
    IperfGuiEvent m_lastSummaryEvent;
    QString m_lastErrorMessage;
};

