/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "PlayTime.h"

#include <mutex>

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "Platform.h"

namespace PlayTime
{

static std::mutex lock;
static QHash<QString, Entry> entries;
static bool loaded = false;

static QString filePath()
{
    return QString::fromStdString(melonDS::Platform::GetLocalFilePath("melonRA-playtime.json"));
}

// with the lock held
static void load()
{
    if (loaded) return;
    loaded = true;

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return;
    const QJsonObject games = QJsonDocument::fromJson(file.readAll()).object().value("games").toObject();
    for (auto it = games.begin(); it != games.end(); ++it)
    {
        const QJsonObject g = it.value().toObject();
        Entry e;
        e.seconds = std::max<qint64>(0, (qint64)g.value("seconds").toDouble());
        const qint64 last = (qint64)g.value("lastPlayed").toDouble();
        if (last > 0) e.lastPlayed = QDateTime::fromSecsSinceEpoch(last);
        if (e.seconds > 0 && it.key().size() <= 300) entries.insert(it.key(), e);
    }
}

// with the lock held
static void save()
{
    QJsonObject games;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
    {
        QJsonObject g{{"seconds", (double)it->seconds}};
        if (it->lastPlayed.isValid()) g.insert("lastPlayed", (double)it->lastPlayed.toSecsSinceEpoch());
        games.insert(it.key(), g);
    }
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"games", games}}).toJson(QJsonDocument::Indented));
    file.commit();
}

Entry get(const QString& id)
{
    std::lock_guard<std::mutex> guard(lock);
    load();
    return entries.value(id);
}

void add(const QString& id, qint64 seconds)
{
    if (id.isEmpty() || seconds <= 0) return;
    {
        std::lock_guard<std::mutex> guard(lock);
        load();
        Entry& e = entries[id];
        e.seconds += seconds;
        e.lastPlayed = QDateTime::currentDateTime();
        save();
    }
    Notifier* n = notifier();
    QMetaObject::invokeMethod(n, [n, id]() { emit n->changed(id); }, Qt::QueuedConnection);
}

QString format(qint64 seconds)
{
    if (seconds <= 0) return {};
    const qint64 h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    QStringList parts;
    if (h) parts << (h == 1 ? QString("1 hour") : QString("%1 hours").arg(h));
    if (m) parts << (m == 1 ? QString("1 minute") : QString("%1 minutes").arg(m));
    if (s) parts << (s == 1 ? QString("1 second") : QString("%1 seconds").arg(s));
    return parts.join(", ");
}

Notifier* notifier()
{
    static Notifier* n = []()
    {
        auto* obj = new Notifier;
        if (QCoreApplication::instance()) obj->moveToThread(QCoreApplication::instance()->thread());
        return obj;
    }();
    return n;
}

}
