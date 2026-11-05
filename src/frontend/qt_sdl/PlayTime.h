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

#ifndef PLAYTIME_H
#define PLAYTIME_H

#include <QDateTime>
#include <QObject>
#include <QString>

// How long each game has been played, kept in melonRA-playtime.json next to
// the settings. Games are identified like their Game Settings (game code, or
// the file name for homebrew).
namespace PlayTime
{

struct Entry
{
    qint64 seconds = 0;
    QDateTime lastPlayed;   // invalid if never
};

Entry get(const QString& id);

// adds time to a game and saves; safe from any thread
void add(const QString& id, qint64 seconds);

// "27 hours, 28 minutes, 30 seconds"; empty for no time
QString format(qint64 seconds);

class Notifier : public QObject
{
    Q_OBJECT
signals:
    void changed(const QString& id);
};
// emits on the main thread
Notifier* notifier();

}

#endif // PLAYTIME_H
