/*
 * DSD Nexus - Qt6 frontend for DSD audio conversion
 * Copyright (c) 2026 Alexander Wichers
 *
 * Based on MystiQ by Maikel Llamaret Heredia (GPLv3)
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

#include <QTreeWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>
#include <QProgressDialog>
#include <QSettings>
#include <QMenu>
#include <QFileDialog>
#include <QInputDialog>
#include <QDesktopServices>
#include <QTextDocument>
#include <QMimeData>
#include <QtMath>
#include <cassert>

#include "convertlist.h"
#include "convertlistdelegate.h"
#include "dsdparamdialog.h"
#include "pipeline/dsdconverter.h"
#include "pipeline/dsdprobe.h"
#include "services/constants.h"

#define MIN_DURATION 100 // Minimum duration (milliseconds) to show progress dialog.

namespace {
QString htmlEscape(QString s) {
    return s.toHtmlEscaped();
}
}

class Task : public QObject
{
public:
    explicit Task(QObject *parent = nullptr) : QObject(parent) { }
    virtual ~Task();
    enum TaskStatus { QUEUED, RUNNING, FINISHED, FAILED };
    int id;
    TaskStatus status;
    DsdPipeParameters param;
    QTreeWidgetItem *listitem;
    QString errmsg;
};

Task::~Task() {}

/* Column layout for the DSD conversion task list.
   To add a new column:
     (1) Add a new constant before NUM_COLUMNS.
     (2) Fill in the title in init_treewidget_fill_column_titles().
     (3) Set default visibility in init_treewidget_columns_visibility().
     (4) Populate it in fill_list_fields().
 */
enum ConvertListColumns
{
    COL_SOURCE,       // Input filename
    COL_ALBUM,        // Album title
    COL_ARTIST,       // Album artist
    COL_TRACKS,       // Track selection (e.g. "all", "1-5")
    COL_FORMATS,      // Output formats summary (e.g. "DSF + WAV 24-bit")
    COL_OUTPUT_DIR,   // Output directory
    COL_PROGRESS,     // Progress bar
    NUM_COLUMNS
};

class ConvertList::ListEventFilter : public QObject
{
public:
    ListEventFilter(ConvertList *parent) : QObject(parent), m_parent(parent) { }
    virtual ~ListEventFilter();

    // Propagate events from the list to its parent.
    bool eventFilter(QObject */*object*/, QEvent *event)
    {
        switch (event->type()) {
        case QEvent::KeyPress:
            return m_parent->list_keyPressEvent(static_cast<QKeyEvent*>(event));
        case QEvent::DragEnter:
            m_parent->list_dragEnterEvent(static_cast<QDragEnterEvent*>(event));
            return true;
        case QEvent::DragMove:
            m_parent->list_dragMoveEvent(static_cast<QDragMoveEvent*>(event));
            return true;
        case QEvent::DragLeave:
            m_parent->list_dragLeaveEvent(static_cast<QDragLeaveEvent*>(event));
            return true;
        case QEvent::Drop:
            m_parent->list_dropEvent(static_cast<QDropEvent*>(event));
            return true;
        case QEvent::ChildRemoved:
            m_parent->list_childRemovedEvent(static_cast<QChildEvent*>(event));
            return true;
        default:
            break;
        }
        return false;
    }

private:
    ConvertList *m_parent;
};

ConvertList::ListEventFilter::~ListEventFilter() {}

ConvertList::ConvertList(QWidget *parent) :
    QWidget(parent),
    m_list(new QTreeWidget(this)),
    m_listEventFilter(new ListEventFilter(this)),
    prev_index(0),
    m_converter(new DsdConverter(this)),
    m_probe(new DsdProbe(this)),
    m_current_task(nullptr),
    is_busy(false),
    run_next(false)
{
    QLayout *layout = new QHBoxLayout(this);
    this->setLayout(layout);

    init_treewidget(m_list);
    m_list->setItemDelegate(new ConvertListDelegate(COL_PROGRESS, this));
    layout->addWidget(m_list);

    connect(m_converter, SIGNAL(finished(int)),
            this, SLOT(task_finished_slot(int)));
    connect(m_converter, SIGNAL(progressRefreshed(int)),
            this, SLOT(progress_refreshed(int)));
    connect(m_list, SIGNAL(itemSelectionChanged()),
            this, SIGNAL(itemSelectionChanged()));
    connect(m_list, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(slotDoubleClick(QModelIndex)));

    // Propagate context menu event.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, SIGNAL(customContextMenuRequested(QPoint)),
            this, SIGNAL(customContextMenuRequested(QPoint)));

    // enable drag/drop functions
    m_list->setAcceptDrops(true);

    // allow selecting multiple items
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Propagate events from the QTreeWidget to ConvertList.
    m_list->installEventFilter(m_listEventFilter);
    m_list->viewport()->installEventFilter(m_listEventFilter);

    // Enable internal drag-and-drop of list items
    m_list->setDragDropMode(QAbstractItemView::InternalMove);

    QSettings settings;
    QHeaderView *header = m_list->header();

    /* Only restore header states if the column count of the stored header state
       is the same as the current column count. Otherwise, the stored state is
       meaningless and should not be used. */
    int prev_column_count = settings.value("convertlist/column_count").toInt();
    if (prev_column_count == NUM_COLUMNS)
        header->restoreState(settings.value("convertlist/header_state").toByteArray());

    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(slotHeaderContextMenu(QPoint)));

    m_startTime.start();
    show_background_image();
}

ConvertList::~ConvertList()
{
    QSettings settings;
    /* must store column count along with header state, because saved header
       state is meaningless if the column count changes. */
    settings.setValue("convertlist/header_state", m_list->header()->saveState());
    settings.setValue("convertlist/column_count", NUM_COLUMNS);
}

bool ConvertList::addTask(DsdPipeParameters param)
{
    qDebug() << "Adding DSD task for:" << param.source;

    // Track output directory for duplicate detection.
    output_filenames_push(param.outputDir);

    QStringList columns;
    for (int i = 0; i < NUM_COLUMNS; i++)
        columns.append(QString());

    fill_list_fields(param, columns);

    QTreeWidgetItem *item = new QTreeWidgetItem(m_list, columns);

    /* Create a new Task object.
     * The ownership of the Task object belongs to m_list.
     * However, the Task object will also be deleted when the
     * corresponding QTreeWidgetItem is deleted.
     */
    Task *task = new Task(m_list);
    task->param = param;
    task->status = Task::QUEUED;
    task->id = ++prev_index;
    task->listitem = item;

    item->setData(0, Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(task)));

    // Prevent dropping directly on an item
    item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    m_list->addTopLevelItem(item);

    setProgressData(task, 0);

    update_tooltip(item);
    hide_background_image();

    qDebug() << QString("Added: \"%1\" -> \"%2\"").arg(param.source, param.outputDir);

    return true;
}

int ConvertList::addTasks(const QList<DsdPipeParameters> &paramList)
{
    const int file_count = paramList.size();
    int success_count = 0;

    // Record the files that could not be added.
    QStringList failed_files;

    // Create progress dialog.
    QProgressDialog dlgProgress(QString(""),
                                tr("Cancel"),
                                0, file_count,  /* min/max */
                                this);
    dlgProgress.setWindowModality(Qt::WindowModal);
    dlgProgress.setMinimumDuration(MIN_DURATION);

    int progress_count = 0;
    QList<DsdPipeParameters>::const_iterator it = paramList.begin();
    for (; it != paramList.end(); ++it) {

        dlgProgress.setLabelText(tr("Adding files (%1/%2)")
                                 .arg(progress_count).arg(file_count));

        dlgProgress.setValue(progress_count++);

        if (dlgProgress.wasCanceled())
            break;

        if (addTask(*it)) {
            success_count++;
        } else {
            failed_files.push_back(it->source);
            qDebug() << QString("Failed to add file: %1").arg(it->source);
        }
    }

    dlgProgress.setValue(file_count);

    if (!failed_files.isEmpty()) {
        QMessageBox msgBox;
        msgBox.setText(tr("Some files could not be added."));
        msgBox.setDetailedText(failed_files.join("\n"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.exec();
    }

    // start conversion if autostart is true
    const bool autostart_default = Constants::getBool("AutoStartConversion");
    bool autostart = QSettings().value("options/auto_start_conversion",
                                       autostart_default).toBool();
    if (autostart && count() > 0)
        start();

    return success_count;
}

bool ConvertList::isBusy() const
{
    return is_busy;
}

bool ConvertList::isEmpty() const
{
    return m_list->topLevelItemCount() == 0;
}

int ConvertList::count() const
{
    return m_list->topLevelItemCount();
}

int ConvertList::selectedCount() const
{
    return m_list->selectedItems().size();
}

int ConvertList::finishedCount() const
{
    return finished_items().size();
}

int ConvertList::elapsedTime() const
{
    return is_busy ? m_startTime.elapsed() : 0;
}

const DsdPipeParameters* ConvertList::getCurrentIndexParameter() const
{
    Task *task = first_selected_task();
    return task ? &task->param : nullptr;
}

bool ConvertList::selectedTaskFailed() const
{
    Task *task = first_selected_task();
    if (!task || selectedCount() != 1)
        return false;
    return task->status == Task::FAILED;
}

// Public Slots

void ConvertList::start()
{
    if (is_busy && !run_next)
        return;

    run_next = false;

    if (!is_busy) { // new session: start timing
        m_startTime.restart();
        is_busy = true;
        emit started();
    }

    if (!run_first_queued_task()) {
        // no task is executed
        this->stop();
        emit all_tasks_finished();
        emit stopped();
    }
}

void ConvertList::stop()
{
    is_busy = false;
    if (m_current_task) {
        progress_refreshed(0);
        m_current_task->status = Task::QUEUED;
        setProgressData(m_current_task, 0);
        m_current_task = nullptr;
        emit stopped();
    }
    m_converter->stop();
}

void ConvertList::removeSelectedItems()
{
    remove_items(m_list->selectedItems());
}

void ConvertList::removeCompletedItems()
{
    remove_items(finished_items());
}

void ConvertList::editSelectedParameters()
{
    QList<QTreeWidgetItem*> itemList = m_list->selectedItems();
    if (itemList.isEmpty())
        return;

    // Use the first selected task's parameters as the baseline
    Task *firstTask = get_task(itemList[0]);
    if (!firstTask || firstTask->status == Task::RUNNING)
        return;

    DsdParamDialog dialog(this);
    dialog.setParameters(firstTask->param);

    if (dialog.exec() == QDialog::Accepted) {
        DsdPipeParameters edited = dialog.parameters();

        // Apply changes to all selected non-running tasks
        foreach (QTreeWidgetItem *item, itemList) {
            Task *task = get_task(item);
            if (task && task->status != Task::RUNNING) {
                // Preserve per-task source/metadata, apply shared output settings
                task->param.outputFormats = edited.outputFormats;
                task->param.pcmBitDepth = edited.pcmBitDepth;
                task->param.pcmQuality = edited.pcmQuality;
                task->param.pcmSampleRate = edited.pcmSampleRate;
                task->param.flacCompression = edited.flacCompression;
                task->param.writeId3 = edited.writeId3;
                task->param.writeDst = edited.writeDst;
                task->param.trackFormat = edited.trackFormat;
                task->param.albumFormat = edited.albumFormat;
                task->param.outputDir = edited.outputDir;
                task->param.formatSummary = task->param.buildFormatSummary();

                // Update list columns
                item->setText(COL_FORMATS, task->param.formatSummary);
                item->setText(COL_OUTPUT_DIR, task->param.outputDir);
                update_tooltip(item);
            }
        }
    }
}

void ConvertList::changeSelectedOutputFile()
{
    // For DSD tasks the primary output is a directory, not a single file.
    // Redirect to changeSelectedOutputDirectory instead.
    changeSelectedOutputDirectory();
}

void ConvertList::changeSelectedOutputDirectory()
{
    Task *task = first_selected_task();

    if (!task)
        return;

    DsdPipeParameters &param = task->param;

    QString orig_path = param.outputDir;

    QString path = QFileDialog::getExistingDirectory(this, tr("Output Directory"),
                                                     orig_path);

    if (!path.isEmpty()) {
        // Apply the output path to all selected items
        QMessageBox::StandardButtons overwrite = QMessageBox::No;
        QList<QTreeWidgetItem*> itemList = m_list->selectedItems();
        foreach (QTreeWidgetItem *item, itemList) {
            Task *t = get_task(item);
            change_output_dir(t, path, overwrite, itemList.size() > 1);
        }
    }
}

void ConvertList::retrySelectedItems()
{
    QList<QTreeWidgetItem*> itemList = m_list->selectedItems();

    if (itemList.isEmpty())
        return;

    foreach (QTreeWidgetItem* item, itemList)
        reset_task(get_task(item));

    start();
}

void ConvertList::retryAll()
{
    const int list_size = m_list->topLevelItemCount();
    for (int i = 0; i < list_size; i++) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        reset_task(get_task(item));
    }

    start();
}

void ConvertList::showErrorMessage()
{
    Task *task = first_selected_task();
    if (task) {
        QMessageBox msgBox;
        msgBox.setText(tr("Error Message:\n\n") + task->errmsg);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setIcon(QMessageBox::Information);
        msgBox.exec();
    }
}

void ConvertList::clear()
{
    const int item_count = count();
    QList<QTreeWidgetItem*> itemList;
    for (int i = 0; i < item_count; i++)
        itemList.push_back(m_list->topLevelItem(i));
    remove_items(itemList);
}

// Private Slots

void ConvertList::task_finished_slot(int exitcode)
{
    if (m_current_task) {

        m_current_task->status = (exitcode == 0)
                ? Task::FINISHED
                : Task::FAILED;

        m_current_task->errmsg = exitcode != 0
                ? m_converter->errorMessage()
                : QString::fromLatin1("");

        refresh_progressbar(m_current_task);

        m_current_task = nullptr;
        emit task_finished(exitcode);

        run_next = true;
        this->start(); // start next task
    }
}

void ConvertList::progress_refreshed(int percentage)
{
    if (m_current_task) {
        qDebug() << "Progress Refreshed: " << percentage << "%";
        setProgressData(m_current_task, percentage);
    }
}

void ConvertList::show_background_image()
{
    m_list->viewport()->setStyleSheet(
                "background-image: url(:/other/icons/list_background.png);"
                "background-position: center;"
                "background-repeat: no-repeat;");
    QString tip = tr("Drag and drop files here to add tasks.");
    m_list->viewport()->setStatusTip(tip);
    m_list->viewport()->setToolTip(tip);
}

void ConvertList::hide_background_image()
{
    m_list->viewport()->setStyleSheet("");
    m_list->viewport()->setStatusTip("");
    m_list->viewport()->setToolTip("");
}

void ConvertList::slotHeaderContextMenu(QPoint point)
{
    const int header_count = m_list->header()->count();
    const int current_column = m_list->header()->logicalIndexAt(point);

    // Count visible columns.
    int visible_column_count = 0, visible_column_index = 0;
    for (int i = 0; i < header_count; i++) {
        if (!m_list->isColumnHidden(i)) {
            ++visible_column_count;
            visible_column_index = i;
        }
    }

    QMenu menu;

    // Add the item under the mouse to the list
    if (current_column >= 0 && visible_column_count > 1) {
        QAction *action = new QAction(&menu);
        QString column_text = m_list->headerItem()->text(current_column);
        QString action_text = tr("Hide \"%1\"").arg(column_text);
        action->setText(action_text);
        action->setData(current_column);
        action->setCheckable(false);
        action->setChecked(false);
        menu.addAction(action);
    }

    QAction *actionRestore = new QAction(&menu);
    actionRestore->setText(tr("Restore All Columns"));
    actionRestore->setData(-1);
    connect(actionRestore, SIGNAL(triggered()),
            this, SLOT(slotRestoreListHeaders()));
    menu.addAction(actionRestore);

    menu.addSeparator();

    // Construct the rest of the menu and uncheck hidden items.
    for (int i = 0; i < header_count; i++) {
        QString title = m_list->headerItem()->text(i);
        QAction *action = new QAction(title, &menu);
        action->setCheckable(true);
        action->setChecked(!m_list->isColumnHidden(i));
        action->setData(i); // save the column index

        // not allow user to hide the last column
        if (visible_column_count > 1 || visible_column_index != i)
            menu.addAction(action);
    }

    connect(&menu, SIGNAL(triggered(QAction*)),
            this, SLOT(slotHeaderContextMenuTriggered(QAction*)));

    menu.exec(QCursor::pos());
}

void ConvertList::slotHeaderContextMenuTriggered(QAction *action)
{
    const int column_index = action->data().toInt();
    if (column_index >= 0)
        m_list->setColumnHidden(column_index, !action->isChecked());
}

void ConvertList::slotRestoreListHeaders()
{
    const int column_count = m_list->columnCount();
    QHeaderView *header = m_list->header();
    for (int i = 0; i < column_count; i++) { // Restore all sections.
        m_list->showColumn(i);
        header->resizeSection(i, header->defaultSectionSize());
    }

    // Restore default value.
    init_treewidget_columns_visibility(m_list);
}

void ConvertList::slotDoubleClick(QModelIndex index)
{
    int row = index.row();
    if (row >= 0 && row < count()) {
        QTreeWidgetItem *item = m_list->topLevelItem(row);
        Task *task = get_task(item);
        if (task) {
            switch (task->status) {
            case Task::QUEUED:
            case Task::FAILED:
                {
                    DsdParamDialog dialog(this);
                    dialog.setParameters(task->param);
                    if (dialog.exec() == QDialog::Accepted) {
                        DsdPipeParameters edited = dialog.parameters();
                        task->param.outputFormats = edited.outputFormats;
                        task->param.pcmBitDepth = edited.pcmBitDepth;
                        task->param.pcmQuality = edited.pcmQuality;
                        task->param.pcmSampleRate = edited.pcmSampleRate;
                        task->param.flacCompression = edited.flacCompression;
                        task->param.writeId3 = edited.writeId3;
                        task->param.writeDst = edited.writeDst;
                        task->param.trackFormat = edited.trackFormat;
                        task->param.albumFormat = edited.albumFormat;
                        task->param.outputDir = edited.outputDir;
                        task->param.formatSummary = task->param.buildFormatSummary();

                        item->setText(COL_FORMATS, task->param.formatSummary);
                        item->setText(COL_OUTPUT_DIR, task->param.outputDir);
                        update_tooltip(item);
                    }
                }
                break;
            case Task::FINISHED:
                // Open output directory
                {
                    QString folder_path = task->param.outputDir;
                    if (QFileInfo(folder_path).exists()) {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(folder_path));
                    }
                }
                break;
            default:
                break;
            }
        }
    }
}

void ConvertList::slotAllItemsRemoved()
{
    show_background_image();
}

// Events

bool ConvertList::list_keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) { // Remove all selected items.
        removeSelectedItems();
        return true; // processed
    } else {
        return false; // not processed
    }
}

void ConvertList::list_dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ConvertList::list_dragMoveEvent(QDragMoveEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData && mimeData->hasUrls())
        event->acceptProposedAction();
}

void ConvertList::list_dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

// The user drops files into the area.
void ConvertList::list_dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData && mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        QStringList files;
        foreach (QUrl url, urlList) {
            QString file = url.toLocalFile();
            if (!file.isEmpty())
                files.append(file);
        }
        if (!files.isEmpty())
            emit filesDropped(files);
    }
}

void ConvertList::list_childRemovedEvent(QChildEvent */*event*/)
{
    // Data-role based progress doesn't need refresh after child removal.
    // Delegate repaints automatically from stored item data.
}

// Functions to access m_outputFileNames

void ConvertList::output_filenames_push(const QString& filename)
{
    if (m_outputFileNames.contains(filename)) {
        ++m_outputFileNames[filename];
    } else {
        m_outputFileNames.insert(filename, 1);
    }
}

void ConvertList::output_filenames_pop(const QString &filename)
{
    int count = m_outputFileNames.value(filename, 0);
    if (count > 1) {
        --m_outputFileNames[filename];
    } else if (count == 1) {
        m_outputFileNames.remove(filename);
    }
}

QHash<QString, int>& ConvertList::output_filenames()
{
    return m_outputFileNames;
}


// Initialize the QTreeWidget listing files.
void ConvertList::init_treewidget(QTreeWidget *w)
{
    Q_ASSERT_X(w, "ConvertList::init_treewidget", "w: null pointer");

    w->setColumnCount(NUM_COLUMNS);

    QStringList columnTitle;
    for (int i = 0; i < NUM_COLUMNS; i++) {
        columnTitle.append(QString());
    }

    // Set column titles.
    init_treewidget_fill_column_titles(columnTitle);

    w->setHeaderLabels(columnTitle);
    w->setRootIsDecorated(false);
    w->setUniformRowHeights(true);
    w->setAlternatingRowColors(true);

    init_treewidget_columns_visibility(w);
}

void ConvertList::init_treewidget_fill_column_titles(QStringList &columnTitle)
{
    columnTitle[COL_SOURCE]     = tr("Source");
    columnTitle[COL_ALBUM]      = tr("Album");
    columnTitle[COL_ARTIST]     = tr("Artist");
    columnTitle[COL_TRACKS]     = tr("Tracks");
    columnTitle[COL_FORMATS]    = tr("Output Formats");
    columnTitle[COL_OUTPUT_DIR] = tr("Output Directory");
    columnTitle[COL_PROGRESS]   = tr("Progress");
}

/* No hidden columns by default -- all columns are visible. */
void ConvertList::init_treewidget_columns_visibility(QTreeWidget *w)
{
    Q_UNUSED(w);
    // All columns visible by default
}

bool ConvertList::run_first_queued_task()
{
    // execute the first queued task in the list and return
    // returns true if a task is run, false if none
    const int task_count = count();
    for (int i = 0; i < task_count; i++) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        Task *task = get_task(item);
        if (task->status == Task::QUEUED) {
            // start the task
            is_busy = true;
            task->status = Task::RUNNING;
            m_current_task = task;

            setProgressData(task, 0);

            m_converter->start(task->param);
            emit start_conversion(i, task->param);

            return true;
        }
    }
    return false;
}

/* Fill in the columns of the list from the DsdPipeParameters.
   No probing needed -- display fields come from the parameters themselves.
*/
void ConvertList::fill_list_fields(const DsdPipeParameters &param, QStringList &columns)
{
    columns[COL_SOURCE]     = QFileInfo(param.source).fileName();
    columns[COL_ALBUM]      = param.albumTitle;
    columns[COL_ARTIST]     = param.albumArtist;
    columns[COL_TRACKS]     = param.trackSpec;
    columns[COL_FORMATS]    = param.buildFormatSummary();
    columns[COL_OUTPUT_DIR] = param.outputDir;
}

// Reset the item to the queued state.
void ConvertList::reset_task(Task *task)
{
    if (task && task->status != Task::RUNNING) {
        task->status = Task::QUEUED;
        refresh_progressbar(task);
    }
}

// Remove items in the list.
// A progress dialog is shown if the operation takes time longer than MIN_DURATION.
void ConvertList::remove_items(const QList<QTreeWidgetItem *>& itemList)
{
    QProgressDialog dlgProgress(tr("Removing tasks..."),
                                tr("Cancel"),
                                0, itemList.count(),
                                this);
    dlgProgress.setWindowModality(Qt::WindowModal);
    dlgProgress.setMinimumDuration(MIN_DURATION);

    int progress_count = 0;
    foreach (QTreeWidgetItem *item, itemList) {
        dlgProgress.setValue(++progress_count);

        if (dlgProgress.wasCanceled())
            break;

        remove_item(item);
    }

    dlgProgress.setValue(itemList.size());
}

/**
 * Store progress data in the item's data roles for the delegate to paint.
 * Also sets DisplayRole to ensure the tree widget schedules a repaint -
 * custom roles alone don't trigger visual updates in QTreeWidget.
 */
void ConvertList::setProgressData(Task *task, int percentage, const QString &text)
{
    QTreeWidgetItem *item = task->listitem;
    item->setData(COL_PROGRESS, ProgressValueRole, percentage);
    item->setData(COL_PROGRESS, ProgressTextRole, text);
    item->setData(COL_PROGRESS, Qt::DisplayRole,
                  text.isEmpty() ? QStringLiteral("%1%").arg(percentage) : text);
}

/** Change the output directory of @a task to @a new_dir
 *  and update relevant fields in the list.
 *  @return true if success, false if failed
 */
bool ConvertList::change_output_dir(Task *task, const QString &new_dir,
        QMessageBox::StandardButtons &overwrite, bool show_all_buttons)
{
    if (overwrite == QMessageBox::NoToAll) return false;

    DsdPipeParameters &param = task->param;
    QTreeWidgetItem *item = task->listitem;

    QString orig_dir = param.outputDir;

    if (new_dir == orig_dir) return true; // success: no need to change

    if ((QFileInfo(new_dir).exists() && output_filenames().contains(new_dir))
            && overwrite != QMessageBox::YesToAll) {
        QMessageBox::StandardButtons flags = QMessageBox::Yes | QMessageBox::No;
        if (show_all_buttons) {
            flags |= QMessageBox::YesToAll | QMessageBox::NoToAll;
        }
        overwrite = QMessageBox::warning(this, tr("Directory Exists"),
                          tr("%1 is already used by another task. "
                             "Still use this directory?").arg(new_dir),
                          flags);
        if (overwrite != QMessageBox::Yes && overwrite != QMessageBox::YesToAll)
            return false;
    }

    param.outputDir = new_dir;

    // Rebuild the set of tracked output directories.
    output_filenames_pop(orig_dir);
    output_filenames_push(new_dir);

    // Update item text
    item->setText(COL_OUTPUT_DIR, new_dir);
    update_tooltip(item);

    qDebug() << "Output directory changed: " + orig_dir + " => " + new_dir;
    return true;
}

/**
 * @brief Remove the @a item along with the associated Task object.
 */
void ConvertList::remove_item(QTreeWidgetItem *item)
{
    Task *task = get_task(item);
    Q_ASSERT(task != nullptr);
    if (task->status != Task::RUNNING) { // not a running task
        output_filenames_pop(task->param.outputDir);
        const int item_index = m_list->indexOfTopLevelItem(item);
        QTreeWidgetItem *item = m_list->takeTopLevelItem(item_index);
        /* Delete the Task object.
         * The ownership of the Task object belongs to m_list.
         * However, it is no longer used when the corresponding
         * list item is deleted, so it's OK to delete it here.
         */
        delete get_task(item);
        delete item;
        qDebug() << "Removed list item " << item_index;
        if (isEmpty()) // removed the last item
            slotAllItemsRemoved();
    } else { // The task is being executed.
        // Silently ignore.
    }
}

/**
 * @brief This function returns the pointer to the first selected task.
 * @retval nullptr No item is selected.
 */
Task* ConvertList::first_selected_task() const
{
    QList<QTreeWidgetItem*> itemList = m_list->selectedItems();
    if (itemList.isEmpty())
        return nullptr;
    else
        return get_task(itemList[0]);
}

/**
 * @brief Retrieve the task associated with the tree item.
 * @retval nullptr The task doesn't exist.
 */
Task* ConvertList::get_task(QTreeWidgetItem *item) const
{
    return reinterpret_cast<Task*>(item->data(0, Qt::UserRole).value<quintptr>());
}

void ConvertList::refresh_progressbar(Task *task)
{
    switch (task->status) {
    case Task::QUEUED:
        setProgressData(task, 0);
        task->listitem->setToolTip(COL_PROGRESS, QString());
        break;
    case Task::RUNNING:
        setProgressData(task, static_cast<int>(floor(m_converter->progress())));
        task->listitem->setToolTip(COL_PROGRESS, QString());
        break;
    case Task::FINISHED:
        setProgressData(task, 100, tr("Finished"));
        task->listitem->setToolTip(COL_PROGRESS, tr("Finished"));
        break;
    case Task::FAILED:
        setProgressData(task, 0, tr("Failed"));
        task->listitem->setToolTip(COL_PROGRESS, tr("Error: %1").arg(task->errmsg));
        break;
    }
}


void ConvertList::update_tooltip(QTreeWidgetItem *item)
{
    QStringList tip;

    // List all columns except "progress" in tooltip.
    tip << "<p style='white-space:pre'>"; // prevent automatic linebreak
    int count = 0;
    for (int i = 0; i < NUM_COLUMNS; i++) {
        if (i != COL_PROGRESS && !m_list->isColumnHidden(i)) {

            if (count++ != 0)
                tip << "<br/>"; // prepend linebreak if necessary

            // show full source path for COL_SOURCE, full outputDir for COL_OUTPUT_DIR
            QString content;
            if (i == COL_SOURCE)
                content = get_task(item)->param.source;
            else if (i == COL_OUTPUT_DIR)
                content = get_task(item)->param.outputDir;
            else
                content = item->text(i);

            // show only visible columns
            tip << "<b>"
                   + m_list->headerItem()->text(i) // column title
                   + ":</b> "
                   + htmlEscape(content) // column content
                   ;
        }
    }
    tip << "</p>";

    QString tip_str = tip.join("");

    // set tooltip for every column in the row
    for (int i = 0; i < NUM_COLUMNS; i++) {
        if (i != COL_PROGRESS)
            item->setToolTip(i, tip_str);
    }
}

QList<QTreeWidgetItem*> ConvertList::finished_items() const
{
    QList<QTreeWidgetItem*> itemList;
    const int item_count = count();
    for (int i = 0; i < item_count; i++) {
        Task *task = get_task(m_list->topLevelItem(i));
        if (task->status == Task::FINISHED) {
            itemList.push_back(task->listitem);
        }
    }
    return itemList;
}
