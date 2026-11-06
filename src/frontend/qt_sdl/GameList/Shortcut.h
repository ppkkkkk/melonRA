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

#ifndef GAMELIST_SHORTCUT_H
#define GAMELIST_SHORTCUT_H

#include <QImage>
#include <QString>

namespace GameList
{

// Puts a shortcut on the desktop that starts melonRA with this game, with the
// game's icon. Returns where it went, or an empty string and why not.
QString createDesktopShortcut(const QString& title, const QString& romPath, const QImage& icon,
                              const QString& id, QString& error);

}

#endif // GAMELIST_SHORTCUT_H
