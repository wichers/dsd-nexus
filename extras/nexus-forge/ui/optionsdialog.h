/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QSpinBox;
class QLineEdit;
QT_END_NAMESPACE

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);
    ~OptionsDialog();

    int exec() override;

private slots:
    void slotBrowseOutputDir();
    void slotOutputFormatChanged();

private:
    // General
    QCheckBox *m_chkAutoStart;

    // Default output directory
    QLineEdit *m_editOutputDir;

    // Default output formats
    QCheckBox *m_chkDsf;
    QCheckBox *m_chkDsdiff;
    QCheckBox *m_chkEditMaster;
    QCheckBox *m_chkWav;
    QCheckBox *m_chkFlac;
    QCheckBox *m_chkXml;
    QCheckBox *m_chkCue;

    // Default PCM settings
    QComboBox *m_cboBitDepth;
    QComboBox *m_cboQuality;
    QComboBox *m_cboSampleRate;
    QSpinBox *m_spinFlacCompression;

    // Default DSD options
    QCheckBox *m_chkWriteId3;
    QCheckBox *m_chkWriteDst;

    // Default naming
    QComboBox *m_cboTrackFormat;
    QComboBox *m_cboAlbumFormat;

    void setupUi();
    void read_fields();
    void write_fields();
    void updatePcmEnabled();
};

#endif // OPTIONSDIALOG_H
