#pragma once

#include "IperfGuiTypes.h"

#include <QString>

class IperfJsonParser
{
public:
    static IperfGuiEvent parseJson(const QString &jsonText);

private:
    static QVariant variantFromJson(const QJsonValue &value);
    static QVariantMap mapFromJson(const QJsonObject &object);
    static QString summarizeFields(const QVariantMap &fields);
};

