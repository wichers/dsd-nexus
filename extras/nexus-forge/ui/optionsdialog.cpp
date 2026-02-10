/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "optionsdialog.h"
#include "services/constants.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>

OptionsDialog::OptionsDialog(QWidget *parent) :
    QDialog(parent)
{
    setupUi();
}

OptionsDialog::~OptionsDialog()
{
}

int OptionsDialog::exec()
{
    read_fields();
    bool accepted = (QDialog::exec() == QDialog::Accepted);
    if (accepted) {
        write_fields();
    }
    return accepted;
}

void OptionsDialog::slotBrowseOutputDir()
{
    QString path = QFileDialog::getExistingDirectory(
        this, tr("Select Default Output Directory"), m_editOutputDir->text());
    if (!path.isEmpty()) {
        m_editOutputDir->setText(path);
    }
}

void OptionsDialog::slotOutputFormatChanged()
{
    updatePcmEnabled();
}

void OptionsDialog::setupUi()
{
    setWindowTitle(tr("Options"));
    setMinimumWidth(520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // General
    {
        QGroupBox *grp = new QGroupBox(tr("General"), this);
        QVBoxLayout *lay = new QVBoxLayout(grp);
        m_chkAutoStart = new QCheckBox(tr("Start conversion automatically after adding files"), grp);
        lay->addWidget(m_chkAutoStart);
        mainLayout->addWidget(grp);
    }

    // Default output directory
    {
        QGroupBox *grp = new QGroupBox(tr("Default Output Directory"), this);
        QHBoxLayout *lay = new QHBoxLayout(grp);
        m_editOutputDir = new QLineEdit(grp);
        QPushButton *btnBrowse = new QPushButton(tr("Browse..."), grp);
        lay->addWidget(m_editOutputDir, 1);
        lay->addWidget(btnBrowse);
        mainLayout->addWidget(grp);

        connect(btnBrowse, &QPushButton::clicked,
                this, &OptionsDialog::slotBrowseOutputDir);
    }

    // Default output formats
    {
        QGroupBox *grp = new QGroupBox(tr("Default Output Formats"), this);
        QGridLayout *grid = new QGridLayout(grp);

        m_chkDsf = new QCheckBox(tr("DSF"), grp);
        m_chkDsdiff = new QCheckBox(tr("DSDIFF"), grp);
        m_chkEditMaster = new QCheckBox(tr("Edit Master"), grp);
        m_chkWav = new QCheckBox(tr("WAV"), grp);
        m_chkFlac = new QCheckBox(tr("FLAC"), grp);
        m_chkXml = new QCheckBox(tr("XML"), grp);
        m_chkCue = new QCheckBox(tr("CUE"), grp);

        grid->addWidget(m_chkDsf, 0, 0);
        grid->addWidget(m_chkDsdiff, 0, 1);
        grid->addWidget(m_chkEditMaster, 0, 2);
        grid->addWidget(m_chkWav, 1, 0);
        grid->addWidget(m_chkFlac, 1, 1);
        grid->addWidget(m_chkXml, 1, 2);
        grid->addWidget(m_chkCue, 1, 3);

        mainLayout->addWidget(grp);

        connect(m_chkWav, &QCheckBox::toggled, this, &OptionsDialog::slotOutputFormatChanged);
        connect(m_chkFlac, &QCheckBox::toggled, this, &OptionsDialog::slotOutputFormatChanged);
    }

    // PCM + DSD + Naming options side by side
    {
        QHBoxLayout *optRow = new QHBoxLayout();

        // PCM options (left)
        QGroupBox *grpPcm = new QGroupBox(tr("Default PCM Options"), this);
        QFormLayout *pcmForm = new QFormLayout(grpPcm);

        m_cboBitDepth = new QComboBox(grpPcm);
        m_cboBitDepth->addItem(tr("16-bit"), 16);
        m_cboBitDepth->addItem(tr("24-bit"), 24);
        m_cboBitDepth->addItem(tr("32-bit float"), 32);
        pcmForm->addRow(tr("Bit Depth:"), m_cboBitDepth);

        m_cboQuality = new QComboBox(grpPcm);
        m_cboQuality->addItem(tr("Fast"), 0);
        m_cboQuality->addItem(tr("Normal"), 1);
        m_cboQuality->addItem(tr("High"), 2);
        pcmForm->addRow(tr("Quality:"), m_cboQuality);

        m_cboSampleRate = new QComboBox(grpPcm);
        m_cboSampleRate->addItem(tr("Auto"), 0);
        m_cboSampleRate->addItem(tr("88.2 kHz"), 88200);
        m_cboSampleRate->addItem(tr("176.4 kHz"), 176400);
        pcmForm->addRow(tr("Sample Rate:"), m_cboSampleRate);

        m_spinFlacCompression = new QSpinBox(grpPcm);
        m_spinFlacCompression->setRange(0, 8);
        m_spinFlacCompression->setValue(5);
        pcmForm->addRow(tr("FLAC Compression:"), m_spinFlacCompression);

        optRow->addWidget(grpPcm);

        // DSD + Naming (right)
        QVBoxLayout *rightCol = new QVBoxLayout();

        QGroupBox *grpDsd = new QGroupBox(tr("Default DSD Options"), this);
        QVBoxLayout *dsdLay = new QVBoxLayout(grpDsd);
        m_chkWriteId3 = new QCheckBox(tr("Write ID3 tags"), grpDsd);
        m_chkWriteDst = new QCheckBox(tr("Keep DST compression"), grpDsd);
        dsdLay->addWidget(m_chkWriteId3);
        dsdLay->addWidget(m_chkWriteDst);
        rightCol->addWidget(grpDsd);

        QGroupBox *grpNaming = new QGroupBox(tr("Default Naming"), this);
        QFormLayout *namingForm = new QFormLayout(grpNaming);
        m_cboTrackFormat = new QComboBox(grpNaming);
        m_cboTrackFormat->addItem(tr("Number only"), 0);
        m_cboTrackFormat->addItem(tr("Number - Title"), 1);
        m_cboTrackFormat->addItem(tr("Number - Artist - Title"), 2);
        namingForm->addRow(tr("Track:"), m_cboTrackFormat);

        m_cboAlbumFormat = new QComboBox(grpNaming);
        m_cboAlbumFormat->addItem(tr("Title only"), 0);
        m_cboAlbumFormat->addItem(tr("Artist - Title"), 1);
        namingForm->addRow(tr("Album:"), m_cboAlbumFormat);
        rightCol->addWidget(grpNaming);
        rightCol->addStretch();

        optRow->addLayout(rightCol);
        mainLayout->addLayout(optRow);
    }

    // Dialog buttons
    {
        QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        mainLayout->addWidget(buttonBox);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
}

void OptionsDialog::read_fields()
{
    QSettings settings;

    // General
    m_chkAutoStart->setChecked(
        settings.value("options/auto_start_conversion",
                       Constants::getBool("AutoStartConversion")).toBool());

    // Output directory
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    m_editOutputDir->setText(
        settings.value("options/default_output_dir", defaultDir).toString());

    // Output formats (default: DSF only)
    m_chkDsf->setChecked(settings.value("options/default_fmt_dsf", true).toBool());
    m_chkDsdiff->setChecked(settings.value("options/default_fmt_dsdiff", false).toBool());
    m_chkEditMaster->setChecked(settings.value("options/default_fmt_editmaster", false).toBool());
    m_chkWav->setChecked(settings.value("options/default_fmt_wav", false).toBool());
    m_chkFlac->setChecked(settings.value("options/default_fmt_flac", false).toBool());
    m_chkXml->setChecked(settings.value("options/default_fmt_xml", false).toBool());
    m_chkCue->setChecked(settings.value("options/default_fmt_cue", false).toBool());

    // PCM
    int bitIdx = m_cboBitDepth->findData(settings.value("options/default_pcm_bitdepth", 24).toInt());
    if (bitIdx >= 0) m_cboBitDepth->setCurrentIndex(bitIdx);

    int qualIdx = m_cboQuality->findData(settings.value("options/default_pcm_quality", 1).toInt());
    if (qualIdx >= 0) m_cboQuality->setCurrentIndex(qualIdx);

    int rateIdx = m_cboSampleRate->findData(settings.value("options/default_pcm_samplerate", 0).toInt());
    if (rateIdx >= 0) m_cboSampleRate->setCurrentIndex(rateIdx);

    m_spinFlacCompression->setValue(settings.value("options/default_flac_compression", 5).toInt());

    // DSD
    m_chkWriteId3->setChecked(settings.value("options/default_write_id3", true).toBool());
    m_chkWriteDst->setChecked(settings.value("options/default_write_dst", false).toBool());

    // Naming
    int tfmtIdx = m_cboTrackFormat->findData(settings.value("options/default_track_format", 2).toInt());
    if (tfmtIdx >= 0) m_cboTrackFormat->setCurrentIndex(tfmtIdx);

    int afmtIdx = m_cboAlbumFormat->findData(settings.value("options/default_album_format", 1).toInt());
    if (afmtIdx >= 0) m_cboAlbumFormat->setCurrentIndex(afmtIdx);

    updatePcmEnabled();
}

void OptionsDialog::write_fields()
{
    QSettings settings;

    // General
    settings.setValue("options/auto_start_conversion", m_chkAutoStart->isChecked());

    // Output directory
    settings.setValue("options/default_output_dir", m_editOutputDir->text());

    // Output formats
    settings.setValue("options/default_fmt_dsf", m_chkDsf->isChecked());
    settings.setValue("options/default_fmt_dsdiff", m_chkDsdiff->isChecked());
    settings.setValue("options/default_fmt_editmaster", m_chkEditMaster->isChecked());
    settings.setValue("options/default_fmt_wav", m_chkWav->isChecked());
    settings.setValue("options/default_fmt_flac", m_chkFlac->isChecked());
    settings.setValue("options/default_fmt_xml", m_chkXml->isChecked());
    settings.setValue("options/default_fmt_cue", m_chkCue->isChecked());

    // PCM
    settings.setValue("options/default_pcm_bitdepth", m_cboBitDepth->currentData().toInt());
    settings.setValue("options/default_pcm_quality", m_cboQuality->currentData().toInt());
    settings.setValue("options/default_pcm_samplerate", m_cboSampleRate->currentData().toInt());
    settings.setValue("options/default_flac_compression", m_spinFlacCompression->value());

    // DSD
    settings.setValue("options/default_write_id3", m_chkWriteId3->isChecked());
    settings.setValue("options/default_write_dst", m_chkWriteDst->isChecked());

    // Naming
    settings.setValue("options/default_track_format", m_cboTrackFormat->currentData().toInt());
    settings.setValue("options/default_album_format", m_cboAlbumFormat->currentData().toInt());
}

void OptionsDialog::updatePcmEnabled()
{
    bool pcmNeeded = m_chkWav->isChecked() || m_chkFlac->isChecked();
    m_cboBitDepth->setEnabled(pcmNeeded);
    m_cboQuality->setEnabled(pcmNeeded);
    m_cboSampleRate->setEnabled(pcmNeeded);
    m_spinFlacCompression->setEnabled(m_chkFlac->isChecked());
}
