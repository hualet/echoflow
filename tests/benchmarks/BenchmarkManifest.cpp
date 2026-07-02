// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BenchmarkManifest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <stdexcept>

namespace echoflow {

std::vector<BenchmarkEntry> loadBenchmarkManifest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open manifest: " + path.toStdString());
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        throw std::runtime_error("manifest must be a JSON array: "
                                 + error.errorString().toStdString());
    }

    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    const std::filesystem::path base =
        std::filesystem::path(path.toStdString()).parent_path();
    QSet<QString> ids;
    std::vector<BenchmarkEntry> entries;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            throw std::runtime_error("manifest entry must be an object");
        }
        const QJsonObject object = value.toObject();
        BenchmarkEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.audio = object.value(QStringLiteral("audio")).toString().toStdString();
        entry.sha256 = object.value(QStringLiteral("sha256")).toString();
        entry.reference = object.value(QStringLiteral("reference")).toString();
        entry.condition = object.value(QStringLiteral("condition")).toString();
        if (entry.id.isEmpty()) {
            throw std::runtime_error("manifest entry has no id");
        }
        if (ids.contains(entry.id)) {
            throw std::runtime_error("duplicate manifest id: " + entry.id.toStdString());
        }
        if (entry.audio.empty()) {
            throw std::runtime_error("manifest entry has no audio path: "
                                     + entry.id.toStdString());
        }
        if (!shaPattern.match(entry.sha256).hasMatch()) {
            throw std::runtime_error("manifest entry has invalid sha256: "
                                     + entry.id.toStdString());
        }
        if (entry.audio.is_relative()) {
            entry.audio = base / entry.audio;
        }
        for (const QJsonValue& intervalValue : object.value(QStringLiteral("speech")).toArray()) {
            const QJsonArray interval = intervalValue.toArray();
            if (interval.size() != 2) {
                throw std::runtime_error("speech interval must have two values: "
                                         + entry.id.toStdString());
            }
            entry.speech.push_back({interval[0].toDouble(), interval[1].toDouble()});
        }
        ids.insert(entry.id);
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        throw std::runtime_error("manifest has no entries");
    }
    return entries;
}

}  // namespace echoflow
