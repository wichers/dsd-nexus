/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * Inspired by qBittorrent's ProgressBarPainter (GPLv3)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PROGRESSBARPAINTER_H
#define PROGRESSBARPAINTER_H

#include <QProgressBar>
#include <QStyleOptionProgressBar>

class QPainter;
class QStyle;

class ProgressBarPainter
{
public:
    explicit ProgressBarPainter();

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               int percentage, const QString &text) const;

private:
    QProgressBar m_dummyProgressBar;
};

#endif // PROGRESSBARPAINTER_H
