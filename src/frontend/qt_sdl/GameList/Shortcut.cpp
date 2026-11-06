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

#include "Shortcut.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>

#include "Platform.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

namespace GameList
{

// the program to start: an AppImage runs from a temporary mount, so the file
// itself; a macOS app is started through its bundle's executable
static QString programPath()
{
    QString exe = QCoreApplication::applicationFilePath();
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (!appimage.isEmpty()) exe = appimage;
#endif
    return exe;
}

// the game's 32x32 icon, enlarged by whole numbers so the pixel art stays sharp
static QImage iconAt(const QImage& icon, int size)
{
    const int scale = std::max(1, size / std::max(1, icon.width()));
    QImage big = icon.convertToFormat(QImage::Format_ARGB32)
                     .scaled(icon.size() * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (big.width() == size) return big;
    QImage out(size, size, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QImage fit = big.scaled(size, size, Qt::KeepAspectRatio, size < big.width() ? Qt::SmoothTransformation
                                                                                 : Qt::FastTransformation);
    for (int y = 0; y < fit.height(); y++)
        for (int x = 0; x < fit.width(); x++)
            out.setPixel(x + (size - fit.width()) / 2, y + (size - fit.height()) / 2, fit.pixel(x, y));
    return out;
}

static QByteArray png(const QImage& img)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return data;
}

// Windows .ico with PNG images (16 to 256)
static QByteArray makeIco(const QImage& icon)
{
    const int sizes[] = {16, 32, 48, 64, 128, 256};
    std::vector<QByteArray> images;
    for (int s : sizes) images.push_back(png(iconAt(icon, s)));

    QByteArray out;
    auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); out.append(b, 2); };
    auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); out.append(b, 4); };
    u16(0); u16(1); u16((quint16)images.size());
    quint32 offset = 6 + 16 * (quint32)images.size();
    for (size_t i = 0; i < images.size(); i++)
    {
        const int s = sizes[i];
        out.append(char(s >= 256 ? 0 : s));
        out.append(char(s >= 256 ? 0 : s));
        out.append(char(0)); out.append(char(0));
        u16(1); u16(32);
        u32((quint32)images[i].size());
        u32(offset);
        offset += (quint32)images[i].size();
    }
    for (const QByteArray& img : images) out.append(img);
    return out;
}

// macOS .icns with PNG images
static QByteArray makeIcns(const QImage& icon)
{
    const struct { const char* type; int size; } kinds[] = {
        {"icp4", 16}, {"icp5", 32}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512}};
    QByteArray body;
    for (const auto& k : kinds)
    {
        const QByteArray data = png(iconAt(icon, k.size));
        char len[4];
        qToBigEndian((quint32)(data.size() + 8), len);
        body.append(k.type, 4);
        body.append(len, 4);
        body.append(data);
    }
    QByteArray out("icns");
    char len[4];
    qToBigEndian((quint32)(body.size() + 8), len);
    out.append(len, 4);
    out.append(body);
    return out;
}

static bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

// a file name from the game's title, without characters systems refuse
static QString safeName(const QString& title)
{
    QString name = title.section('\n', 0, 0).simplified();
    static const QString bad = "\\/:*?\"<>|";
    QString out;
    for (QChar c : name)
        if (!bad.contains(c) && c.unicode() >= 0x20) out += c;
    out = out.trimmed();
    while (out.endsWith('.')) out.chop(1);
    return out.isEmpty() ? QString("DS game") : out.left(100);
}

// where the shortcut icons are kept, next to the settings
static QString iconFolder()
{
    const QString dir = QString::fromStdString(melonDS::Platform::GetLocalFilePath("shortcut-icons"));
    QDir().mkpath(dir);
    return dir;
}

static QString iconName(const QString& id)
{
    QString n;
    for (QChar c : id) n += (c.isLetterOrNumber() || c == '-' || c == '_') ? c : QChar('_');
    return n.left(80);
}

#if !defined(_WIN32) && !defined(__APPLE__)
// a value in a .desktop file's Exec line: quoted, with the characters the
// format reserves escaped
static QString execArg(const QString& arg)
{
    QString out = "\"";
    for (QChar c : arg)
    {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    // % is special in Exec lines even inside quotes
    out.replace("%", "%%");
    // and in the file, backslashes are escaped once more
    out.replace("\\", "\\\\");
    return out;
}
#endif

QString createDesktopShortcut(const QString& title, const QString& romPath, const QImage& icon,
                              const QString& id, QString& error)
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty() || !QDir(desktop).exists())
    {
        error = "Couldn't find your desktop folder.";
        return {};
    }
    const QString exe = programPath();
    const QString rom = QFileInfo(romPath).absoluteFilePath();
    const QString name = safeName(title);
    const bool hasIcon = !icon.isNull();

#ifdef _WIN32
    QString iconPath;
    if (hasIcon)
    {
        iconPath = iconFolder() + "/" + iconName(id) + ".ico";
        if (!writeFile(iconPath, makeIco(icon))) iconPath.clear();
    }
    const QString path = desktop + "/" + name + ".lnk";

    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link);
    if (SUCCEEDED(hr))
    {
        link->SetPath((LPCWSTR)QDir::toNativeSeparators(exe).utf16());
        link->SetArguments((LPCWSTR)("\"" + QDir::toNativeSeparators(rom) + "\"").utf16());
        link->SetWorkingDirectory((LPCWSTR)QDir::toNativeSeparators(QFileInfo(exe).absolutePath()).utf16());
        link->SetDescription((LPCWSTR)QString("Play %1 in melonRA").arg(name).utf16());
        link->SetIconLocation((LPCWSTR)QDir::toNativeSeparators(iconPath.isEmpty() ? exe : iconPath).utf16(), 0);
        IPersistFile* file = nullptr;
        hr = link->QueryInterface(IID_IPersistFile, (void**)&file);
        if (SUCCEEDED(hr))
        {
            hr = file->Save((LPCWSTR)QDir::toNativeSeparators(path).utf16(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr))
    {
        error = "Windows couldn't create the shortcut.";
        return {};
    }
    return path;

#elif defined(__APPLE__)
    // a small app that starts melonRA with the game
    const QString app = desktop + "/" + name + ".app";
    const QString contents = app + "/Contents";
    if (!QDir().mkpath(contents + "/MacOS") || !QDir().mkpath(contents + "/Resources"))
    {
        error = "Couldn't write to your desktop folder.";
        return {};
    }
    auto shellQuote = [](const QString& s) { QString q = s; q.replace("'", "'\\''"); return "'" + q + "'"; };
    const QByteArray script = ("#!/bin/sh\nexec " + shellQuote(exe) + " " + shellQuote(rom) + "\n").toUtf8();
    const QString launcher = contents + "/MacOS/launch";
    if (!writeFile(launcher, script))
    {
        error = "Couldn't write to your desktop folder.";
        return {};
    }
    QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                    QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    if (hasIcon) writeFile(contents + "/Resources/icon.icns", makeIcns(icon));
    auto xml = [](QString s) { return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"); };
    const QString plist = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>CFBundleExecutable</key><string>launch</string>\n"
        "<key>CFBundleName</key><string>%1</string>\n"
        "<key>CFBundleIdentifier</key><string>net.melonra.shortcut.%2</string>\n"
        "<key>CFBundlePackageType</key><string>APPL</string>\n"
        "%3"
        "</dict></plist>\n").arg(xml(name), iconName(id),
                                 hasIcon ? "<key>CFBundleIconFile</key><string>icon</string>\n" : "");
    writeFile(contents + "/Info.plist", plist.toUtf8());
    return app;

#else
    QString iconPath = "melonRA";
    if (hasIcon)
    {
        const QString p = iconFolder() + "/" + iconName(id) + ".png";
        if (iconAt(icon, 256).save(p, "PNG")) iconPath = p;
    }
    auto value = [](QString s) { return s.replace("\\", "\\\\").replace("\n", " "); };
    const QString text = QString(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=%1\n"
        "Comment=Play %1 in melonRA\n"
        "Exec=%2 %3\n"
        "Icon=%4\n"
        "Terminal=false\n"
        "Categories=Game;Emulator;\n").arg(value(name), execArg(exe), execArg(rom), value(iconPath));
    const QString path = desktop + "/" + name + ".desktop";
    if (!writeFile(path, text.toUtf8()))
    {
        error = "Couldn't write to your desktop folder.";
        return {};
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                QFile::ReadGroup | QFile::ReadOther);
    return path;
#endif
}

}
