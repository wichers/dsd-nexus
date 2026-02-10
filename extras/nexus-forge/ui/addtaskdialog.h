/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ADDTASKDIALOG_H
#define ADDTASKDIALOG_H

#include <QDialog>
#include <QList>

#include "pipeline/dsdpipeparameters.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QLabel;
class QTreeWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QGroupBox;
class QPushButton;
QT_END_NAMESPACE

class DsdProbe;

class AddTaskDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AddTaskDialog(QWidget *parent = nullptr);
    ~AddTaskDialog();

    /** @brief Set initial file path (called before exec()) */
    void setSourceFile(const QString &path);

    /** @brief Get the configured parameters (call after exec() returns Accepted) */
    DsdPipeParameters parameters() const;

    /** @brief Get list of parameters (one per task; same as parameters() since pipeline handles multi-track) */
    QList<DsdPipeParameters> allParameters() const;

private slots:
    void slotBrowseSource();
    void slotBrowseOutput();
    void slotProbeFile();
    void slotOutputFormatChanged();
    void slotSelectAllTracks();
    void slotSelectNoTracks();
    void slotChannelTypeChanged(int index);

private:
    // Source row
    QLineEdit *m_editSource;
    QLabel *m_lblFormatInfo;

    // Album info labels
    QLabel *m_lblTitle;
    QLabel *m_lblArtist;
    QLabel *m_lblYear;
    QLabel *m_lblGenre;

    // Track list
    QTreeWidget *m_trackList;

    // Output format checkboxes
    QCheckBox *m_chkDsf;
    QCheckBox *m_chkDsdiff;
    QCheckBox *m_chkEditMaster;
    QCheckBox *m_chkWav;
    QCheckBox *m_chkFlac;
    QCheckBox *m_chkXml;
    QCheckBox *m_chkCue;

    // PCM options
    QComboBox *m_cboBitDepth;
    QComboBox *m_cboQuality;
    QComboBox *m_cboSampleRate;
    QSpinBox *m_spinFlacCompression;
    QGroupBox *m_grpPcmOptions;

    // DSD options
    QCheckBox *m_chkWriteId3;
    QCheckBox *m_chkWriteDst;

    // Naming
    QComboBox *m_cboTrackFormat;
    QComboBox *m_cboAlbumFormat;

    // Output
    QLineEdit *m_editOutputDir;
    QComboBox *m_cboChannelType;
    QWidget *m_channelRow;

    // Buttons
    QPushButton *m_btnAddToQueue;

    // Probe
    DsdProbe *m_probe;

    void setupUi();
    void setupConnections();
    void loadDefaults();
    void updatePcmOptionsEnabled();
    void populateFromProbe();
    void clearProbeInfo();

    static QString formatDuration(double seconds);
};

#endif // ADDTASKDIALOG_H
