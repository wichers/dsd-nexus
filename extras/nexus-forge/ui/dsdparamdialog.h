/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DSDPARAMDIALOG_H
#define DSDPARAMDIALOG_H

#include <QDialog>
#include "pipeline/dsdpipeparameters.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QSpinBox;
class QGroupBox;
class QLineEdit;
class QLabel;
QT_END_NAMESPACE

/**
 * @brief Dialog for editing conversion parameters of a queued task.
 *
 * Unlike AddTaskDialog, this does not probe files or show tracks.
 * It only edits the output format, PCM, DSD, and naming options.
 */
class DsdParamDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DsdParamDialog(QWidget *parent = nullptr);
    ~DsdParamDialog();

    /** @brief Load parameters into the dialog */
    void setParameters(const DsdPipeParameters &param);

    /** @brief Get the edited parameters */
    DsdPipeParameters parameters() const;

private slots:
    void slotOutputFormatChanged();
    void slotBrowseOutput();

private:
    QLabel *m_lblSource;

    QCheckBox *m_chkDsf;
    QCheckBox *m_chkDsdiff;
    QCheckBox *m_chkEditMaster;
    QCheckBox *m_chkWav;
    QCheckBox *m_chkFlac;
    QCheckBox *m_chkXml;
    QCheckBox *m_chkCue;

    QComboBox *m_cboBitDepth;
    QComboBox *m_cboQuality;
    QComboBox *m_cboSampleRate;
    QSpinBox *m_spinFlacCompression;
    QGroupBox *m_grpPcmOptions;

    QCheckBox *m_chkWriteId3;
    QCheckBox *m_chkWriteDst;

    QComboBox *m_cboTrackFormat;
    QComboBox *m_cboAlbumFormat;

    QLineEdit *m_editOutputDir;

    DsdPipeParameters m_originalParam;

    void setupUi();
    void updatePcmOptionsEnabled();
};

#endif // DSDPARAMDIALOG_H
