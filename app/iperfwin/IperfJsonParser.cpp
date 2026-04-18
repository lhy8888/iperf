#include "IperfJsonParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

QString firstString(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isString()) {
        return value.toString();
    }
    return QString();
}

QString eventKindToString(IperfEventKind kind)
{
    switch (kind) {
    case IperfEventKind::Started:
        return QStringLiteral("started");
    case IperfEventKind::Interval:
        return QStringLiteral("interval");
    case IperfEventKind::Summary:
        return QStringLiteral("summary");
    case IperfEventKind::Error:
        return QStringLiteral("error");
    case IperfEventKind::Finished:
        return QStringLiteral("finished");
    case IperfEventKind::Info:
    default:
        return QStringLiteral("info");
    }
}

} // namespace

QVariant
IperfJsonParser::variantFromJson(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double:
        return value.toDouble();
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array: {
        QVariantList list;
        const QJsonArray array = value.toArray();
        list.reserve(array.size());
        for (const QJsonValue &item : array) {
            list.push_back(variantFromJson(item));
        }
        return list;
    }
    case QJsonValue::Object:
        return mapFromJson(value.toObject());
    case QJsonValue::Null:
    case QJsonValue::Undefined:
    default:
        return QVariant();
    }
}

QVariantMap
IperfJsonParser::mapFromJson(const QJsonObject &object)
{
    QVariantMap map;
    for (auto it = object.begin(); it != object.end(); ++it) {
        map.insert(it.key(), variantFromJson(it.value()));
    }
    return map;
}

QString
IperfJsonParser::summarizeFields(const QVariantMap &fields)
{
    const QVariant value = fields.value(QStringLiteral("summary_name"));
    if (value.isValid()) {
        return value.toString();
    }
    if (fields.contains(QStringLiteral("connecting_to"))) {
        return QStringLiteral("connecting");
    }
    if (fields.contains(QStringLiteral("cpu_utilization_percent"))) {
        return QStringLiteral("cpu");
    }
    return QString();
}

IperfGuiEvent
IperfJsonParser::parseJson(const QString &jsonText)
{
    IperfGuiEvent event;
    event.rawJson = jsonText;
    event.receivedAt = QDateTime::currentDateTime();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        event.kind = IperfEventKind::Error;
        event.message = QStringLiteral("JSON parse error: %1").arg(parseError.errorString());
        return event;
    }

    const QJsonObject root = document.object();
    event.rawObject = root;

    const QJsonValue eventValue = root.value(QStringLiteral("event"));
    if (eventValue.isString()) {
        event.eventName = eventValue.toString();
        const QJsonValue dataValue = root.value(QStringLiteral("data"));
        if (dataValue.isObject()) {
            event.fields = mapFromJson(dataValue.toObject());
            event.message = summarizeFields(event.fields);
        } else if (dataValue.isString()) {
            event.message = dataValue.toString();
            event.fields.insert(QStringLiteral("text"), event.message);
        } else {
            event.fields.insert(QStringLiteral("value"), variantFromJson(dataValue));
        }

        if (event.eventName == QLatin1String("start")) {
            event.kind = IperfEventKind::Started;
            if (event.message.isEmpty()) {
                const QVariantMap connecting = event.fields.value(QStringLiteral("connecting_to")).toMap();
                if (!connecting.isEmpty()) {
                    const QString host = connecting.value(QStringLiteral("host")).toString();
                    const QString port = connecting.value(QStringLiteral("port")).toString();
                    event.message = QStringLiteral("Connecting to %1:%2").arg(host, port);
                }
            }
        } else if (event.eventName == QLatin1String("interval")) {
            event.kind = IperfEventKind::Interval;
            const QString summaryName = event.fields.value(QStringLiteral("summary_name")).toString();
            if (!summaryName.isEmpty()) {
                event.fields.insert(QStringLiteral("summary_key"), summaryName);
                const QVariant summaryVariant = event.fields.value(summaryName);
                if (summaryVariant.isValid()) {
                    event.fields.insert(QStringLiteral("summary"), summaryVariant);
                }
            }
            if (event.message.isEmpty()) {
                event.message = QStringLiteral("Interval update");
            }
        } else if (event.eventName == QLatin1String("end")) {
            event.kind = IperfEventKind::Summary;
            event.message = QStringLiteral("Summary ready");
        } else if (event.eventName == QLatin1String("error")) {
            event.kind = IperfEventKind::Error;
            event.message = dataValue.toString();
        } else if (event.eventName == QLatin1String("server_output_text")) {
            event.kind = IperfEventKind::Info;
            event.message = dataValue.toString();
        } else if (event.eventName == QLatin1String("server_output_json")) {
            event.kind = IperfEventKind::Info;
            event.message = QStringLiteral("Server output JSON");
        } else {
            event.kind = IperfEventKind::Info;
            if (event.message.isEmpty()) {
                event.message = event.eventName;
            }
        }
        return event;
    }

    event.kind = IperfEventKind::Finished;
    event.eventName = QStringLiteral("full_output");
    event.fields = mapFromJson(root);
    if (root.contains(QStringLiteral("end")) && root.value(QStringLiteral("end")).isObject()) {
        event.fields.insert(QStringLiteral("end"), variantFromJson(root.value(QStringLiteral("end"))));
    }
    event.message = QStringLiteral("Full JSON output");
    return event;
}

