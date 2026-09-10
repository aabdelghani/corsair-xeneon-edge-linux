// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Profiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace xen::profiles {
namespace {

constexpr const char* kActiveKey = "profiles/active";

QString fileFor(const QString& name) { return directory() + QLatin1Char('/') + slugFor(name) + QStringLiteral(".json"); }

} // namespace

QString directory()
{
    const QString dir = QFileInfo(QSettings().fileName()).absolutePath()
        + QStringLiteral("/profiles");
    QDir().mkpath(dir);
    return dir;
}

QString slugFor(const QString& name)
{
    static const QRegularExpression unsafe(QStringLiteral("[^a-zA-Z0-9._-]+"));
    QString s = name.trimmed();
    s.replace(unsafe, QStringLiteral("-"));
    // Leading dots would hide the file; leading dashes confuse command lines.
    while (s.startsWith(QLatin1Char('.')) || s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    if (s.isEmpty())
        s = QStringLiteral("profile");
    return s.left(64);
}

QStringList list()
{
    QStringList out;
    QDir d(directory());
    for (const QFileInfo& fi : d.entryInfoList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QString name = o.value(QStringLiteral("name")).toString();
        // A file whose contents do not name a profile is not one, and silently
        // inventing a name from the filename would hide the corruption.
        if (!name.isEmpty())
            out.append(name);
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

bool exists(const QString& name) { return QFile::exists(fileFor(name)); }

QJsonObject load(const QString& name)
{
    QFile f(fileFor(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool save(const QString& name, const QJsonObject& body, QString* errorOut)
{
    if (name.trimmed().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("a profile needs a name");
        return false;
    }
    QJsonObject out = body;
    out.insert(QStringLiteral("name"), name.trimmed());

    // Written through QSaveFile so an interrupted write cannot truncate an
    // existing profile to nothing.
    QSaveFile f(fileFor(name));
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = QStringLiteral("could not write %1").arg(f.fileName());
        return false;
    }
    f.write(QJsonDocument(out).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorOut) *errorOut = QStringLiteral("could not save %1").arg(f.fileName());
        return false;
    }
    return true;
}

bool remove(const QString& name, QString* errorOut)
{
    const QString path = fileFor(name);
    if (!QFile::exists(path)) {
        if (errorOut) *errorOut = QStringLiteral("no profile called '%1'").arg(name);
        return false;
    }
    if (!QFile::remove(path)) {
        if (errorOut) *errorOut = QStringLiteral("could not delete %1").arg(path);
        return false;
    }
    if (active() == name)
        setActive(QString());
    return true;
}

bool rename(const QString& from, const QString& to, QString* errorOut)
{
    if (!exists(from)) {
        if (errorOut) *errorOut = QStringLiteral("no profile called '%1'").arg(from);
        return false;
    }
    if (to.trimmed().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("a profile needs a name");
        return false;
    }
    if (slugFor(from) != slugFor(to) && exists(to)) {
        if (errorOut) *errorOut = QStringLiteral("'%1' already exists").arg(to);
        return false;
    }
    QJsonObject body = load(from);
    if (!save(to, body, errorOut))
        return false;
    if (slugFor(from) != slugFor(to))
        QFile::remove(fileFor(from));
    if (active() == from)
        setActive(to);
    return true;
}

QString active() { return QSettings().value(QLatin1String(kActiveKey)).toString(); }
void setActive(const QString& name) { QSettings().setValue(QLatin1String(kActiveKey), name); }

} // namespace xen::profiles
