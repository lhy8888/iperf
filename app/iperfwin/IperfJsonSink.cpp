#include "IperfJsonSink.h"

IperfJsonSink::IperfJsonSink(QObject *parent)
    : QObject(parent)
{
}

void
IperfJsonSink::clear()
{
    m_events.clear();
    m_lastRawJson.clear();
    m_lastSummaryEvent = IperfGuiEvent();
    m_lastErrorMessage.clear();
    emit sessionReset();
}

QVector<IperfGuiEvent>
IperfJsonSink::events() const
{
    return m_events;
}

QString
IperfJsonSink::lastRawJson() const
{
    return m_lastRawJson;
}

IperfGuiEvent
IperfJsonSink::lastSummaryEvent() const
{
    return m_lastSummaryEvent;
}

QString
IperfJsonSink::lastErrorMessage() const
{
    return m_lastErrorMessage;
}

void
IperfJsonSink::enqueueJson(const QString &jsonText)
{
    m_lastRawJson = jsonText;

    const IperfGuiEvent event = IperfJsonParser::parseJson(jsonText);
    // Only retain lifecycle events (Started / Summary / Error / Finished).
    // Interval events arrive once per second and would cause unbounded growth
    // during long-running tests if stored here.
    if (event.kind != IperfEventKind::Interval) {
        m_events.push_back(event);
    }
    if (event.kind == IperfEventKind::Summary || event.kind == IperfEventKind::Finished) {
        m_lastSummaryEvent = event;
    }
    if (event.kind == IperfEventKind::Error) {
        m_lastErrorMessage = event.message;
    }

    emit eventParsed(event);
}

