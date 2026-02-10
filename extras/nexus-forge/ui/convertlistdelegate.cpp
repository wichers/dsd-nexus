/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "convertlistdelegate.h"

#include <QPainter>

ConvertListDelegate::ConvertListDelegate(int progressColumn, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_progressColumn(progressColumn)
{
}

void ConvertListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    if (index.column() != m_progressColumn) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    int percentage = index.data(ProgressValueRole).toInt();
    QString text = index.data(ProgressTextRole).toString();

    painter->save();
    m_progressBarPainter.paint(painter, option, percentage, text);
    painter->restore();
}
