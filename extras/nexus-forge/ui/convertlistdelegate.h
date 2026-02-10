/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONVERTLISTDELEGATE_H
#define CONVERTLISTDELEGATE_H

#include <QStyledItemDelegate>
#include "progressbarpainter.h"

/* Custom item data roles for the progress column */
enum ProgressDataRole {
    ProgressValueRole = Qt::UserRole + 100,  // int: 0-100
    ProgressTextRole  = Qt::UserRole + 101   // QString: override text (e.g. "Finished", "Failed")
};

class ConvertListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ConvertListDelegate(int progressColumn, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    int m_progressColumn;
    ProgressBarPainter m_progressBarPainter;
};

#endif // CONVERTLISTDELEGATE_H
