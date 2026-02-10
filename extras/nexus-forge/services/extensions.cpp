/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "extensions.h"
#include "constants.h"

ExtensionList::ExtensionList() : QStringList()
{
}

QString ExtensionList::forFilter()
{
    QString s;
    for (int n = 0; n < count(); n++) {
        s = s + "*." + at(n) + " ";
    }
    if (!s.isEmpty())
        s = "(" + s.trimmed() + ")";
    return s;
}

QString ExtensionList::forRegExp()
{
    QString s;
    for (int n = 0; n < count(); n++) {
        if (!s.isEmpty())
            s = s + "|";
        s = s + "^" + at(n) + "$";
    }
    return s;
}

Extensions::Extensions()
{
    QStringList dsd_exts = Constants::getSpaceSeparatedList("DsdExtensions");
    for (const QString &ext : dsd_exts)
        m_dsd << ext;
}

Extensions::~Extensions()
{
}

bool Extensions::contains(const QString &ext) const
{
    return m_dsd.contains(ext, Qt::CaseInsensitive);
}

QString Extensions::fileDialogFilter()
{
    return QObject::tr("DSD Files") + " (*.iso *.dsf *.dff *.dsdiff);;"
         + QObject::tr("SACD ISO Images") + " (*.iso);;"
         + QObject::tr("DSF Files") + " (*.dsf);;"
         + QObject::tr("DSDIFF Files") + " (*.dff *.dsdiff);;"
         + QObject::tr("All Files") + " (*)";
}
