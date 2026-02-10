/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "dsdparamdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QFileInfo>

DsdParamDialog::DsdParamDialog(QWidget *parent) :
    QDialog(parent)
{
    setupUi();
}

DsdParamDialog::~DsdParamDialog()
{
}

void DsdParamDialog::setParameters(const DsdPipeParameters &param)
{
    m_originalParam = param;

    m_lblSource->setText(QFileInfo(param.source).fileName());

    // Output formats
    m_chkDsf->setChecked(param.outputFormats & DSD_FORMAT_DSF);
    m_chkDsdiff->setChecked(param.outputFormats & DSD_FORMAT_DSDIFF);
    m_chkEditMaster->setChecked(param.outputFormats & DSD_FORMAT_EDIT_MASTER);
    m_chkWav->setChecked(param.outputFormats & DSD_FORMAT_WAV);
    m_chkFlac->setChecked(param.outputFormats & DSD_FORMAT_FLAC);
    m_chkXml->setChecked(param.outputFormats & DSD_FORMAT_XML);
    m_chkCue->setChecked(param.outputFormats & DSD_FORMAT_CUE);

    // PCM options
    int bitIdx = m_cboBitDepth->findData(param.pcmBitDepth);
    if (bitIdx >= 0) m_cboBitDepth->setCurrentIndex(bitIdx);

    int qualIdx = m_cboQuality->findData(param.pcmQuality);
    if (qualIdx >= 0) m_cboQuality->setCurrentIndex(qualIdx);

    int rateIdx = m_cboSampleRate->findData(param.pcmSampleRate);
    if (rateIdx >= 0) m_cboSampleRate->setCurrentIndex(rateIdx);

    m_spinFlacCompression->setValue(param.flacCompression);

    // DSD options
    m_chkWriteId3->setChecked(param.writeId3);
    m_chkWriteDst->setChecked(param.writeDst);

    // Naming
    int tfmtIdx = m_cboTrackFormat->findData(param.trackFormat);
    if (tfmtIdx >= 0) m_cboTrackFormat->setCurrentIndex(tfmtIdx);

    int afmtIdx = m_cboAlbumFormat->findData(param.albumFormat);
    if (afmtIdx >= 0) m_cboAlbumFormat->setCurrentIndex(afmtIdx);

    // Output directory
    m_editOutputDir->setText(param.outputDir);

    updatePcmOptionsEnabled();
}

DsdPipeParameters DsdParamDialog::parameters() const
{
    DsdPipeParameters p = m_originalParam;

    // Output formats
    uint32_t formats = DSD_FORMAT_NONE;
    if (m_chkDsf->isChecked())        formats |= DSD_FORMAT_DSF;
    if (m_chkDsdiff->isChecked())      formats |= DSD_FORMAT_DSDIFF;
    if (m_chkEditMaster->isChecked())  formats |= DSD_FORMAT_EDIT_MASTER;
    if (m_chkWav->isChecked())         formats |= DSD_FORMAT_WAV;
    if (m_chkFlac->isChecked())        formats |= DSD_FORMAT_FLAC;
    if (m_chkXml->isChecked())         formats |= DSD_FORMAT_XML;
    if (m_chkCue->isChecked())         formats |= DSD_FORMAT_CUE;
    p.outputFormats = formats;

    // PCM
    p.pcmBitDepth = m_cboBitDepth->currentData().toInt();
    p.pcmQuality = m_cboQuality->currentData().toInt();
    p.pcmSampleRate = m_cboSampleRate->currentData().toInt();
    p.flacCompression = m_spinFlacCompression->value();

    // DSD
    p.writeId3 = m_chkWriteId3->isChecked();
    p.writeDst = m_chkWriteDst->isChecked();

    // Naming
    p.trackFormat = m_cboTrackFormat->currentData().toInt();
    p.albumFormat = m_cboAlbumFormat->currentData().toInt();

    // Output
    p.outputDir = m_editOutputDir->text();

    p.formatSummary = p.buildFormatSummary();

    return p;
}

void DsdParamDialog::slotOutputFormatChanged()
{
    updatePcmOptionsEnabled();
}

void DsdParamDialog::slotBrowseOutput()
{
    QString path = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"), m_editOutputDir->text());
    if (!path.isEmpty()) {
        m_editOutputDir->setText(path);
    }
}

void DsdParamDialog::setupUi()
{
    setWindowTitle(tr("Edit Conversion Parameters"));
    setMinimumWidth(500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // Source label
    {
        QHBoxLayout *srcRow = new QHBoxLayout();
        srcRow->addWidget(new QLabel(tr("Source:"), this));
        m_lblSource = new QLabel(this);
        m_lblSource->setStyleSheet(QStringLiteral("font-weight: bold;"));
        srcRow->addWidget(m_lblSource, 1);
        mainLayout->addLayout(srcRow);
    }

    // Output formats
    {
        QGroupBox *grp = new QGroupBox(tr("Output Formats"), this);
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

        connect(m_chkWav, &QCheckBox::toggled, this, &DsdParamDialog::slotOutputFormatChanged);
        connect(m_chkFlac, &QCheckBox::toggled, this, &DsdParamDialog::slotOutputFormatChanged);
    }

    // Options (PCM + DSD + Naming)
    {
        QHBoxLayout *optRow = new QHBoxLayout();

        // PCM
        m_grpPcmOptions = new QGroupBox(tr("PCM Options"), this);
        QFormLayout *pcmForm = new QFormLayout(m_grpPcmOptions);

        m_cboBitDepth = new QComboBox(m_grpPcmOptions);
        m_cboBitDepth->addItem(tr("16-bit"), 16);
        m_cboBitDepth->addItem(tr("24-bit"), 24);
        m_cboBitDepth->addItem(tr("32-bit float"), 32);
        pcmForm->addRow(tr("Bit Depth:"), m_cboBitDepth);

        m_cboQuality = new QComboBox(m_grpPcmOptions);
        m_cboQuality->addItem(tr("Fast"), 0);
        m_cboQuality->addItem(tr("Normal"), 1);
        m_cboQuality->addItem(tr("High"), 2);
        pcmForm->addRow(tr("Quality:"), m_cboQuality);

        m_cboSampleRate = new QComboBox(m_grpPcmOptions);
        m_cboSampleRate->addItem(tr("Auto"), 0);
        m_cboSampleRate->addItem(tr("88.2 kHz"), 88200);
        m_cboSampleRate->addItem(tr("176.4 kHz"), 176400);
        pcmForm->addRow(tr("Sample Rate:"), m_cboSampleRate);

        m_spinFlacCompression = new QSpinBox(m_grpPcmOptions);
        m_spinFlacCompression->setRange(0, 8);
        m_spinFlacCompression->setValue(5);
        pcmForm->addRow(tr("FLAC Compression:"), m_spinFlacCompression);

        optRow->addWidget(m_grpPcmOptions);

        // DSD + Naming (right side)
        QVBoxLayout *rightCol = new QVBoxLayout();

        QGroupBox *grpDsd = new QGroupBox(tr("DSD Options"), this);
        QVBoxLayout *dsdLay = new QVBoxLayout(grpDsd);
        m_chkWriteId3 = new QCheckBox(tr("Write ID3 tags"), grpDsd);
        m_chkWriteDst = new QCheckBox(tr("Keep DST compression"), grpDsd);
        dsdLay->addWidget(m_chkWriteId3);
        dsdLay->addWidget(m_chkWriteDst);
        rightCol->addWidget(grpDsd);

        QGroupBox *grpNaming = new QGroupBox(tr("Naming"), this);
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

    // Output directory
    {
        QHBoxLayout *dirRow = new QHBoxLayout();
        dirRow->addWidget(new QLabel(tr("Output:"), this));
        m_editOutputDir = new QLineEdit(this);
        QPushButton *btnBrowse = new QPushButton(tr("Browse..."), this);
        dirRow->addWidget(m_editOutputDir, 1);
        dirRow->addWidget(btnBrowse);
        mainLayout->addLayout(dirRow);

        connect(btnBrowse, &QPushButton::clicked,
                this, &DsdParamDialog::slotBrowseOutput);
    }

    // Buttons
    {
        QHBoxLayout *btnRow = new QHBoxLayout();
        btnRow->addStretch();
        QPushButton *btnOk = new QPushButton(tr("OK"), this);
        btnOk->setDefault(true);
        QPushButton *btnCancel = new QPushButton(tr("Cancel"), this);
        btnRow->addWidget(btnOk);
        btnRow->addWidget(btnCancel);
        mainLayout->addLayout(btnRow);

        connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }
}

void DsdParamDialog::updatePcmOptionsEnabled()
{
    bool pcmEnabled = m_chkWav->isChecked() || m_chkFlac->isChecked();
    m_grpPcmOptions->setEnabled(pcmEnabled);
    m_spinFlacCompression->setEnabled(m_chkFlac->isChecked());
}
