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

#include "addtaskdialog.h"
#include "pipeline/dsdprobe.h"
#include "services/extensions.h"

#include <QLineEdit>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSettings>
#include <QFileInfo>

AddTaskDialog::AddTaskDialog(QWidget *parent) :
    QDialog(parent),
    m_probe(new DsdProbe(this))
{
    setupUi();
    setupConnections();
    loadDefaults();
}

AddTaskDialog::~AddTaskDialog()
{
}

void AddTaskDialog::setSourceFile(const QString &path)
{
    m_editSource->setText(path);
    slotProbeFile();
}

DsdPipeParameters AddTaskDialog::parameters() const
{
    DsdPipeParameters p;

    // Source
    p.source = m_editSource->text();
    p.sourceType = m_probe->isProbed() ? m_probe->sourceType() : 0;

    // Output directory
    p.outputDir = m_editOutputDir->text();

    // Output formats bitmask
    uint32_t formats = DSD_FORMAT_NONE;
    if (m_chkDsf->isChecked())        formats |= DSD_FORMAT_DSF;
    if (m_chkDsdiff->isChecked())      formats |= DSD_FORMAT_DSDIFF;
    if (m_chkEditMaster->isChecked())  formats |= DSD_FORMAT_EDIT_MASTER;
    if (m_chkWav->isChecked())         formats |= DSD_FORMAT_WAV;
    if (m_chkFlac->isChecked())        formats |= DSD_FORMAT_FLAC;
    if (m_chkXml->isChecked())         formats |= DSD_FORMAT_XML;
    if (m_chkCue->isChecked())         formats |= DSD_FORMAT_CUE;
    p.outputFormats = formats;

    // Channel type
    p.channelType = m_cboChannelType->currentData().toInt();

    // Track selection
    int total = m_trackList->topLevelItemCount();
    if (total == 0) {
        p.trackSpec = QStringLiteral("all");
    } else {
        QStringList checked;
        bool allChecked = true;
        for (int i = 0; i < total; ++i) {
            QTreeWidgetItem *item = m_trackList->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) {
                checked << item->text(1); // track number column
            } else {
                allChecked = false;
            }
        }
        p.trackSpec = allChecked ? QStringLiteral("all") : checked.join(QStringLiteral(","));
    }

    // PCM options
    p.pcmBitDepth = m_cboBitDepth->currentData().toInt();
    p.pcmQuality = m_cboQuality->currentData().toInt();
    p.pcmSampleRate = m_cboSampleRate->currentData().toInt();
    p.flacCompression = m_spinFlacCompression->value();

    // DSD options
    p.writeId3 = m_chkWriteId3->isChecked();
    p.writeDst = m_chkWriteDst->isChecked();

    // Naming
    p.trackFormat = m_cboTrackFormat->currentData().toInt();
    p.albumFormat = m_cboAlbumFormat->currentData().toInt();

    // Display fields from probe
    if (m_probe->isProbed()) {
        p.albumTitle = m_probe->albumTitle();
        p.albumArtist = m_probe->albumArtist();
        p.trackCount = m_probe->trackCount();
    }

    p.formatSummary = p.buildFormatSummary();

    return p;
}

QList<DsdPipeParameters> AddTaskDialog::allParameters() const
{
    QList<DsdPipeParameters> list;
    list.append(parameters());
    return list;
}

// --- Private Slots ---

void AddTaskDialog::slotBrowseSource()
{
    QString startDir;
    if (!m_editSource->text().isEmpty()) {
        QFileInfo fi(m_editSource->text());
        startDir = fi.absolutePath();
    } else {
        startDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }

    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select DSD Source File"),
        startDir,
        Extensions::fileDialogFilter());

    if (!path.isEmpty()) {
        m_editSource->setText(path);
        slotProbeFile();
    }
}

void AddTaskDialog::slotBrowseOutput()
{
    QString startDir = m_editOutputDir->text();
    if (startDir.isEmpty()) {
        startDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }

    QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Select Output Directory"),
        startDir);

    if (!path.isEmpty()) {
        m_editOutputDir->setText(path);
    }
}

void AddTaskDialog::slotProbeFile()
{
    QString path = m_editSource->text().trimmed();
    if (path.isEmpty()) {
        clearProbeInfo();
        return;
    }

    if (!QFileInfo::exists(path)) {
        clearProbeInfo();
        QMessageBox::warning(this, tr("File Not Found"),
                             tr("The file \"%1\" does not exist.").arg(path));
        return;
    }

    int channelType = m_cboChannelType->currentData().toInt();
    bool ok = m_probe->probe(path, channelType);
    if (ok) {
        populateFromProbe();
    } else {
        clearProbeInfo();
        QMessageBox::warning(this, tr("Probe Failed"),
                             tr("Could not read metadata from \"%1\".\n"
                                "The file may be corrupted or not a valid DSD file.")
                             .arg(QFileInfo(path).fileName()));
    }
}

void AddTaskDialog::slotOutputFormatChanged()
{
    updatePcmOptionsEnabled();
}

void AddTaskDialog::slotSelectAllTracks()
{
    int count = m_trackList->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        m_trackList->topLevelItem(i)->setCheckState(0, Qt::Checked);
    }
}

void AddTaskDialog::slotSelectNoTracks()
{
    int count = m_trackList->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        m_trackList->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    }
}

void AddTaskDialog::slotChannelTypeChanged(int index)
{
    Q_UNUSED(index);

    /* When the user switches between stereo and multichannel on an SACD,
       re-probe the file to get the correct track listing for that channel area. */
    if (m_probe->isProbed() && m_probe->isSacd()) {
        slotProbeFile();
    }
}

// --- Private Methods ---

void AddTaskDialog::setupUi()
{
    setWindowTitle(tr("Add Conversion Task"));
    setMinimumSize(700, 550);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ========== Source row ==========
    {
        QGroupBox *grpSource = new QGroupBox(tr("Source"), this);
        QVBoxLayout *srcLayout = new QVBoxLayout(grpSource);

        QHBoxLayout *fileRow = new QHBoxLayout();
        QLabel *lblSource = new QLabel(tr("Source:"), grpSource);
        m_editSource = new QLineEdit(grpSource);
        m_editSource->setPlaceholderText(tr("Select a DSD file (ISO, DSF, DFF)..."));
        QPushButton *btnBrowseSource = new QPushButton(tr("Browse..."), grpSource);
        fileRow->addWidget(lblSource);
        fileRow->addWidget(m_editSource, 1);
        fileRow->addWidget(btnBrowseSource);
        srcLayout->addLayout(fileRow);

        m_lblFormatInfo = new QLabel(grpSource);
        m_lblFormatInfo->setStyleSheet(QStringLiteral("color: #666; font-style: italic;"));
        srcLayout->addWidget(m_lblFormatInfo);

        mainLayout->addWidget(grpSource);

        // Wire browse button
        connect(btnBrowseSource, &QPushButton::clicked,
                this, &AddTaskDialog::slotBrowseSource);
    }

    // ========== Album Info + Tracks (side by side) ==========
    {
        QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

        // --- Album Info (left) ---
        QGroupBox *grpAlbum = new QGroupBox(tr("Album Info"), this);
        QFormLayout *albumForm = new QFormLayout(grpAlbum);
        albumForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_lblTitle = new QLabel(grpAlbum);
        m_lblTitle->setWordWrap(true);
        m_lblTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
        albumForm->addRow(tr("Title:"), m_lblTitle);

        m_lblArtist = new QLabel(grpAlbum);
        m_lblArtist->setWordWrap(true);
        m_lblArtist->setTextInteractionFlags(Qt::TextSelectableByMouse);
        albumForm->addRow(tr("Artist:"), m_lblArtist);

        m_lblYear = new QLabel(grpAlbum);
        m_lblYear->setTextInteractionFlags(Qt::TextSelectableByMouse);
        albumForm->addRow(tr("Year:"), m_lblYear);

        m_lblGenre = new QLabel(grpAlbum);
        m_lblGenre->setTextInteractionFlags(Qt::TextSelectableByMouse);
        albumForm->addRow(tr("Genre:"), m_lblGenre);

        splitter->addWidget(grpAlbum);

        // --- Tracks (right) ---
        QWidget *trackWidget = new QWidget(this);
        QVBoxLayout *trackLayout = new QVBoxLayout(trackWidget);
        trackLayout->setContentsMargins(0, 0, 0, 0);

        QGroupBox *grpTracks = new QGroupBox(tr("Tracks"), trackWidget);
        QVBoxLayout *grpTracksLayout = new QVBoxLayout(grpTracks);

        m_trackList = new QTreeWidget(grpTracks);
        m_trackList->setHeaderLabels({
            QString(),          // checkbox column (empty header)
            tr("#"),
            tr("Title"),
            tr("Performer"),
            tr("Duration")
        });
        m_trackList->setRootIsDecorated(false);
        m_trackList->setAlternatingRowColors(true);
        m_trackList->setUniformRowHeights(true);
        m_trackList->header()->setStretchLastSection(false);
        m_trackList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_trackList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_trackList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_trackList->header()->setSectionResizeMode(3, QHeaderView::Stretch);
        m_trackList->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        grpTracksLayout->addWidget(m_trackList);

        QHBoxLayout *trackBtnRow = new QHBoxLayout();
        QPushButton *btnSelectAll = new QPushButton(tr("Select All"), grpTracks);
        QPushButton *btnSelectNone = new QPushButton(tr("Select None"), grpTracks);
        trackBtnRow->addWidget(btnSelectAll);
        trackBtnRow->addWidget(btnSelectNone);
        trackBtnRow->addStretch();
        grpTracksLayout->addLayout(trackBtnRow);

        trackLayout->addWidget(grpTracks);
        splitter->addWidget(trackWidget);

        // Give more space to the track list
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);

        mainLayout->addWidget(splitter, 1);

        // Wire track selection buttons
        connect(btnSelectAll, &QPushButton::clicked,
                this, &AddTaskDialog::slotSelectAllTracks);
        connect(btnSelectNone, &QPushButton::clicked,
                this, &AddTaskDialog::slotSelectNoTracks);
    }

    // ========== Output Formats ==========
    {
        QGroupBox *grpFormats = new QGroupBox(tr("Output Formats"), this);
        QGridLayout *fmtGrid = new QGridLayout(grpFormats);

        m_chkDsf = new QCheckBox(tr("DSF"), grpFormats);
        m_chkDsdiff = new QCheckBox(tr("DSDIFF"), grpFormats);
        m_chkEditMaster = new QCheckBox(tr("Edit Master"), grpFormats);
        m_chkWav = new QCheckBox(tr("WAV"), grpFormats);
        m_chkFlac = new QCheckBox(tr("FLAC"), grpFormats);
        m_chkXml = new QCheckBox(tr("XML"), grpFormats);
        m_chkCue = new QCheckBox(tr("CUE"), grpFormats);

        // Row 0: DSD formats
        fmtGrid->addWidget(m_chkDsf, 0, 0);
        fmtGrid->addWidget(m_chkDsdiff, 0, 1);
        fmtGrid->addWidget(m_chkEditMaster, 0, 2);

        // Row 1: PCM + metadata formats
        fmtGrid->addWidget(m_chkWav, 1, 0);
        fmtGrid->addWidget(m_chkFlac, 1, 1);
        fmtGrid->addWidget(m_chkXml, 1, 2);
        fmtGrid->addWidget(m_chkCue, 1, 3);

        mainLayout->addWidget(grpFormats);
    }

    // ========== Options row (PCM + DSD + Naming side by side) ==========
    {
        QHBoxLayout *optionsRow = new QHBoxLayout();

        // --- PCM Options ---
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

        optionsRow->addWidget(m_grpPcmOptions);

        // --- DSD Options + Naming ---
        QVBoxLayout *rightOptions = new QVBoxLayout();

        QGroupBox *grpDsd = new QGroupBox(tr("DSD Options"), this);
        QVBoxLayout *dsdLayout = new QVBoxLayout(grpDsd);
        m_chkWriteId3 = new QCheckBox(tr("Write ID3 tags"), grpDsd);
        m_chkWriteDst = new QCheckBox(tr("Keep DST compression"), grpDsd);
        dsdLayout->addWidget(m_chkWriteId3);
        dsdLayout->addWidget(m_chkWriteDst);
        rightOptions->addWidget(grpDsd);

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

        rightOptions->addWidget(grpNaming);
        rightOptions->addStretch();

        optionsRow->addLayout(rightOptions);

        mainLayout->addLayout(optionsRow);
    }

    // ========== Output directory + Channel type ==========
    {
        QGroupBox *grpOutput = new QGroupBox(tr("Output"), this);
        QVBoxLayout *outLayout = new QVBoxLayout(grpOutput);

        // Output directory row
        QHBoxLayout *dirRow = new QHBoxLayout();
        QLabel *lblOutput = new QLabel(tr("Output:"), grpOutput);
        m_editOutputDir = new QLineEdit(grpOutput);
        m_editOutputDir->setPlaceholderText(tr("Select output directory..."));
        QPushButton *btnBrowseOutput = new QPushButton(tr("Browse..."), grpOutput);
        dirRow->addWidget(lblOutput);
        dirRow->addWidget(m_editOutputDir, 1);
        dirRow->addWidget(btnBrowseOutput);
        outLayout->addLayout(dirRow);

        // Channel type row (only shown for SACD)
        m_channelRow = new QWidget(grpOutput);
        QHBoxLayout *channelLayout = new QHBoxLayout(m_channelRow);
        channelLayout->setContentsMargins(0, 0, 0, 0);
        QLabel *lblChannel = new QLabel(tr("Channel:"), m_channelRow);
        m_cboChannelType = new QComboBox(m_channelRow);
        m_cboChannelType->addItem(tr("Stereo"), 0);
        m_cboChannelType->addItem(tr("Multichannel"), 1);
        channelLayout->addWidget(lblChannel);
        channelLayout->addWidget(m_cboChannelType);
        channelLayout->addStretch();
        m_channelRow->setVisible(false); // hidden until SACD is probed
        outLayout->addWidget(m_channelRow);

        mainLayout->addWidget(grpOutput);

        // Wire browse button
        connect(btnBrowseOutput, &QPushButton::clicked,
                this, &AddTaskDialog::slotBrowseOutput);
    }

    // ========== Dialog buttons ==========
    {
        QHBoxLayout *btnRow = new QHBoxLayout();
        btnRow->addStretch();

        m_btnAddToQueue = new QPushButton(tr("Add to Queue"), this);
        m_btnAddToQueue->setDefault(true);
        m_btnAddToQueue->setEnabled(false); // disabled until probe succeeds
        connect(m_btnAddToQueue, &QPushButton::clicked,
                this, &QDialog::accept);

        QPushButton *btnCancel = new QPushButton(tr("Cancel"), this);
        connect(btnCancel, &QPushButton::clicked,
                this, &QDialog::reject);

        btnRow->addWidget(m_btnAddToQueue);
        btnRow->addWidget(btnCancel);

        mainLayout->addLayout(btnRow);
    }

    // Initial state
    updatePcmOptionsEnabled();
}

void AddTaskDialog::setupConnections()
{
    // Output format checkboxes -> enable/disable PCM options
    connect(m_chkDsf, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkDsdiff, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkEditMaster, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkWav, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkFlac, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkXml, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);
    connect(m_chkCue, &QCheckBox::toggled,
            this, &AddTaskDialog::slotOutputFormatChanged);

    // Channel type combo -> re-probe for SACD
    connect(m_cboChannelType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddTaskDialog::slotChannelTypeChanged);
}

void AddTaskDialog::loadDefaults()
{
    QSettings settings;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);

    // Output directory
    m_editOutputDir->setText(
        settings.value("options/default_output_dir", defaultDir).toString());

    // Output formats
    m_chkDsf->setChecked(settings.value("options/default_fmt_dsf", true).toBool());
    m_chkDsdiff->setChecked(settings.value("options/default_fmt_dsdiff", false).toBool());
    m_chkEditMaster->setChecked(settings.value("options/default_fmt_editmaster", false).toBool());
    m_chkWav->setChecked(settings.value("options/default_fmt_wav", false).toBool());
    m_chkFlac->setChecked(settings.value("options/default_fmt_flac", false).toBool());
    m_chkXml->setChecked(settings.value("options/default_fmt_xml", false).toBool());
    m_chkCue->setChecked(settings.value("options/default_fmt_cue", false).toBool());

    // PCM options
    int bitIdx = m_cboBitDepth->findData(settings.value("options/default_pcm_bitdepth", 24).toInt());
    if (bitIdx >= 0) m_cboBitDepth->setCurrentIndex(bitIdx);

    int qualIdx = m_cboQuality->findData(settings.value("options/default_pcm_quality", 1).toInt());
    if (qualIdx >= 0) m_cboQuality->setCurrentIndex(qualIdx);

    int rateIdx = m_cboSampleRate->findData(settings.value("options/default_pcm_samplerate", 0).toInt());
    if (rateIdx >= 0) m_cboSampleRate->setCurrentIndex(rateIdx);

    m_spinFlacCompression->setValue(settings.value("options/default_flac_compression", 5).toInt());

    // DSD options
    m_chkWriteId3->setChecked(settings.value("options/default_write_id3", true).toBool());
    m_chkWriteDst->setChecked(settings.value("options/default_write_dst", false).toBool());

    // Naming
    int tfmtIdx = m_cboTrackFormat->findData(settings.value("options/default_track_format", 2).toInt());
    if (tfmtIdx >= 0) m_cboTrackFormat->setCurrentIndex(tfmtIdx);

    int afmtIdx = m_cboAlbumFormat->findData(settings.value("options/default_album_format", 1).toInt());
    if (afmtIdx >= 0) m_cboAlbumFormat->setCurrentIndex(afmtIdx);

    updatePcmOptionsEnabled();
}

void AddTaskDialog::updatePcmOptionsEnabled()
{
    bool pcmEnabled = m_chkWav->isChecked() || m_chkFlac->isChecked();
    m_grpPcmOptions->setEnabled(pcmEnabled);

    // FLAC compression spinner only meaningful when FLAC is selected
    m_spinFlacCompression->setEnabled(m_chkFlac->isChecked());
}

void AddTaskDialog::populateFromProbe()
{
    if (!m_probe->isProbed())
        return;

    // Format info line
    QString formatLine = QStringLiteral("%1 %2 | %3 | %4 %5")
        .arg(m_probe->dsdRateString(),
             m_probe->channelConfigString(),
             m_probe->sourceTypeString(),
             QString::number(m_probe->trackCount()),
             m_probe->trackCount() == 1 ? tr("track") : tr("tracks"));
    m_lblFormatInfo->setText(formatLine);

    // Album metadata
    m_lblTitle->setText(m_probe->albumTitle());
    m_lblArtist->setText(m_probe->albumArtist());
    m_lblYear->setText(m_probe->year() > 0
                           ? QString::number(m_probe->year())
                           : QString());
    m_lblGenre->setText(m_probe->genre());

    // Track list
    m_trackList->clear();
    int trackCount = m_probe->trackCount();
    for (int i = 1; i <= trackCount; ++i) {
        DsdProbe::TrackInfo info = m_probe->trackInfo(i);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setCheckState(0, Qt::Checked);
        item->setText(1, QString::number(info.number));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, info.title);
        item->setText(3, info.performer);
        item->setText(4, formatDuration(info.durationSeconds));
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);

        m_trackList->addTopLevelItem(item);
    }

    // Show/hide channel type row for SACD
    m_channelRow->setVisible(m_probe->isSacd());

    // Enable the Add to Queue button
    m_btnAddToQueue->setEnabled(true);
}

void AddTaskDialog::clearProbeInfo()
{
    m_lblFormatInfo->clear();
    m_lblTitle->clear();
    m_lblArtist->clear();
    m_lblYear->clear();
    m_lblGenre->clear();
    m_trackList->clear();
    m_channelRow->setVisible(false);
    m_btnAddToQueue->setEnabled(false);
}

QString AddTaskDialog::formatDuration(double seconds)
{
    if (seconds <= 0.0)
        return QStringLiteral("0:00");

    int totalSeconds = static_cast<int>(seconds + 0.5);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;

    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(secs, 2, 10, QLatin1Char('0'));
}
