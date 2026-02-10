/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EXTENSIONS_H
#define EXTENSIONS_H

#include <QStringList>

class ExtensionList : public QStringList
{
public:
    ExtensionList();

    QString forFilter();
    QString forRegExp();
};

class Extensions
{
public:
    Extensions();
    ~Extensions();

    ExtensionList dsd() { return m_dsd; }

    bool contains(const QString &ext) const;

    /** @brief Build a file dialog filter string for DSD files */
    static QString fileDialogFilter();

protected:
    ExtensionList m_dsd;
};

#endif // EXTENSIONS_H
