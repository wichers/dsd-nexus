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

#include "progressbarpainter.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionProgressBar>
#include <QApplication>

ProgressBarPainter::ProgressBarPainter()
{
    m_dummyProgressBar.setRange(0, 100);
    m_dummyProgressBar.setTextVisible(true);
}

void ProgressBarPainter::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               int percentage, const QString &text) const
{
    QStyleOptionProgressBar styleOption;
    styleOption.initFrom(&m_dummyProgressBar);

    styleOption.rect = option.rect;
    styleOption.minimum = 0;
    styleOption.maximum = 100;
    styleOption.progress = percentage;
    styleOption.textVisible = true;
    styleOption.text = text.isEmpty()
        ? QStringLiteral("%1%").arg(percentage)
        : text;

    // Ensure horizontal orientation so Fusion renders text horizontally
    styleOption.state |= QStyle::State_Horizontal | QStyle::State_Enabled;

    QStyle *style = QApplication::style();

    // Draw the item view background (handles selection highlight)
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, option.widget);

    // Draw the progress bar on top (uses dummy widget's palette/state, not the view's)
    style->drawControl(QStyle::CE_ProgressBar, &styleOption, painter, &m_dummyProgressBar);
}
