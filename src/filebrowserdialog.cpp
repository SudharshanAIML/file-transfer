// filebrowserdialog.cpp
#include "filebrowserdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QDirIterator>
#include <QLabel>
#include <QFrame>
#include <QListWidget>

// ── Helper: human-readable file size ──────────────────────────────────────────
static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)           return QString("%1 B").arg(bytes);
    if (bytes < 1024*1024)      return QString("%1 KB").arg(bytes/1024);
    if (bytes < 1024*1024*1024) return QString("%1 MB").arg(bytes/(1024*1024));
    return QString("%1 GB").arg(bytes/(1024*1024*1024));
}

FileBrowserDialog::FileBrowserDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Select Files or Folders");
    setMinimumSize(960, 600);
    setupUi();
    navigateTo(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
}

void FileBrowserDialog::setupUi()
{
    setStyleSheet(R"(
        QDialog {
            background: #1a1a2e;
            color: #e0e0f0;
        }
        QLabel {
            color: #a0a0c0;
            font-size: 12px;
        }
        QLabel#pathLabel {
            color: #6c91e0;
            font-size: 11px;
            padding: 4px 8px;
            background: #12122a;
            border-radius: 4px;
        }
        QTreeView {
            background: #12122a;
            color: #d0d0ee;
            border: 1px solid #2a2a4a;
            border-radius: 6px;
            font-size: 13px;
            selection-background-color: #3a3a6a;
            outline: none;
        }
        QTreeView::item:hover { background: #252550; }
        QTreeView::item:selected { background: #3a3a7a; color: #ffffff; }
        QListWidget {
            background: #12122a;
            color: #d0d0ee;
            border: 1px solid #2a2a4a;
            border-radius: 6px;
            font-size: 12px;
        }
        QListWidget::item { padding: 6px 8px; }
        QListWidget::item:hover { background: #252550; }
        QListWidget::item:selected { background: #3a3a7a; color: #ffffff; }
        QPushButton {
            background: #2a2a5a;
            color: #c0c0ff;
            border: 1px solid #4040a0;
            border-radius: 6px;
            padding: 7px 16px;
            font-size: 13px;
        }
        QPushButton:hover { background: #3a3a7a; border-color: #6060cc; }
        QPushButton:pressed { background: #252565; }
        QPushButton#okBtn {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                         stop:0 #667eea, stop:1 #764ba2);
            color: white;
            border: none;
            font-weight: bold;
        }
        QPushButton#okBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                         stop:0 #7a8ff5, stop:1 #8a5cb8);
        }
        QPushButton#removeBtn {
            background: #3a1a2a;
            border-color: #8a3a5a;
            color: #ff8aaa;
        }
        QPushButton#removeBtn:hover { background: #4a2a3a; }
        QScrollBar:vertical {
            background: #0d0d20;
            width: 8px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical {
            background: #3a3a6a;
            border-radius: 4px;
            min-height: 20px;
        }
        QHeaderView::section {
            background: #1a1a3a;
            color: #8080b0;
            border: none;
            padding: 4px;
            font-size: 11px;
        }
    )");

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(14, 14, 14, 14);

    // Title
    auto *titleLabel = new QLabel("📂  Browse and Select Files or Folders");
    titleLabel->setStyleSheet("font-size:16px; font-weight:bold; color:#a0aaff;");
    root->addWidget(titleLabel);

    // Path bar
    m_currentPathLabel = new QLabel;
    m_currentPathLabel->setObjectName("pathLabel");
    root->addWidget(m_currentPathLabel);

    // Main splitter: [shortcuts | tree | selected]
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setStyleSheet("QSplitter::handle { background: #2a2a4a; }");

    // ── Shortcuts panel ───────────────────────────────────────────────────────
    auto *shortcutPanel = new QWidget;
    auto *shortcutLayout = new QVBoxLayout(shortcutPanel);
    shortcutLayout->setContentsMargins(0,0,0,0);
    shortcutLayout->setSpacing(4);

    auto *shortcutTitle = new QLabel("Quick Access");
    shortcutTitle->setStyleSheet("color:#6060a0; font-size:11px; font-weight:bold;"
                                 "padding:4px 0;");
    shortcutLayout->addWidget(shortcutTitle);

    m_shortcutList = new QListWidget;
    m_shortcutList->setFixedWidth(150);
    m_shortcutList->setFrameShape(QFrame::NoFrame);

    struct Shortcut { QString label; QString path; };
    const QList<Shortcut> shortcuts = {
        {"🏠  Home",      QStandardPaths::writableLocation(QStandardPaths::HomeLocation)},
        {"📄  Documents", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)},
        {"⬇️  Downloads", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)},
        {"🎬  Videos",    QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)},
        {"🎵  Music",     QStandardPaths::writableLocation(QStandardPaths::MusicLocation)},
        {"🖼️  Pictures",  QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)},
        {"🖥️  Desktop",   QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)},
    };

    for (const auto &sc : shortcuts) {
        if (sc.path.isEmpty()) continue;
        auto *item = new QListWidgetItem(sc.label);
        item->setData(Qt::UserRole, sc.path);
        item->setToolTip(sc.path);
        m_shortcutList->addItem(item);
    }

    connect(m_shortcutList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        navigateTo(item->data(Qt::UserRole).toString());
    });

    shortcutLayout->addWidget(m_shortcutList);
    shortcutLayout->addStretch();
    splitter->addWidget(shortcutPanel);

    // ── File system tree ──────────────────────────────────────────────────────
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setRootPath(QDir::rootPath());
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_treeView = new QTreeView;
    m_treeView->setModel(m_fsModel);
    m_treeView->setColumnWidth(0, 280);
    m_treeView->setColumnHidden(2, true); // hide Type column
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    splitter->addWidget(m_treeView);

    connect(m_treeView, &QTreeView::activated,
            this, &FileBrowserDialog::onDirectoryActivated);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](){ updateAddButton(); });

    // ── Selected files panel ──────────────────────────────────────────────────
    auto *selectedPanel = new QWidget;
    auto *selLayout     = new QVBoxLayout(selectedPanel);
    selLayout->setContentsMargins(0,0,0,0);
    selLayout->setSpacing(6);

    auto *selTitle = new QLabel("Selected for Transfer");
    selTitle->setStyleSheet("color:#6060a0; font-size:11px; font-weight:bold; padding:4px 0;");
    selLayout->addWidget(selTitle);

    m_selectedList = new QListWidget;
    m_selectedList->setMinimumWidth(220);
    selLayout->addWidget(m_selectedList, 1);

    m_removeBtn = new QPushButton("✕  Remove");
    m_removeBtn->setObjectName("removeBtn");
    connect(m_removeBtn, &QPushButton::clicked, this, &FileBrowserDialog::onRemoveClicked);
    selLayout->addWidget(m_removeBtn);

    splitter->addWidget(selectedPanel);
    splitter->setSizes({150, 520, 240});

    root->addWidget(splitter, 1);

    // ── Bottom bar ────────────────────────────────────────────────────────────
    auto *bottomBar = new QHBoxLayout;
    m_addBtn = new QPushButton("＋  Add to Transfer");
    m_addBtn->setEnabled(false);
    connect(m_addBtn, &QPushButton::clicked, this, &FileBrowserDialog::onAddClicked);
    bottomBar->addWidget(m_addBtn);
    bottomBar->addStretch();

    auto *cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomBar->addWidget(cancelBtn);

    m_okBtn = new QPushButton("Send Selected  →");
    m_okBtn->setObjectName("okBtn");
    m_okBtn->setEnabled(false);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomBar->addWidget(m_okBtn);

    root->addLayout(bottomBar);
}

void FileBrowserDialog::navigateTo(const QString &path)
{
    QModelIndex idx = m_fsModel->index(path);
    m_treeView->setRootIndex(idx);
    m_treeView->collapseAll();
    m_currentPathLabel->setText("📁  " + path);
}

void FileBrowserDialog::onDirectoryActivated(const QModelIndex &index)
{
    if (m_fsModel->isDir(index)) {
        navigateTo(m_fsModel->filePath(index));
    }
}

void FileBrowserDialog::updateAddButton()
{
    m_addBtn->setEnabled(m_treeView->selectionModel()->hasSelection());
}

void FileBrowserDialog::onAddClicked()
{
    const auto selection = m_treeView->selectionModel()->selectedIndexes();
    QSet<QString> alreadyAdded;
    for (int i = 0; i < m_selectedList->count(); ++i)
        alreadyAdded.insert(m_selectedList->item(i)->data(Qt::UserRole).toString());

    for (const auto &idx : selection) {
        if (idx.column() != 0) continue;
        addPath(m_fsModel->filePath(idx));
    }

    m_okBtn->setEnabled(m_selectedList->count() > 0);
}

void FileBrowserDialog::addPath(const QString &path)
{
    // Deduplicate
    for (int i = 0; i < m_selectedList->count(); ++i) {
        if (m_selectedList->item(i)->data(Qt::UserRole).toString() == path) return;
    }

    QFileInfo fi(path);
    QString label = fi.isDir()
        ? QString("📁  %1").arg(fi.fileName())
        : QString("📄  %1  (%2)").arg(fi.fileName()).arg(humanSize(fi.size()));

    auto *item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    m_selectedList->addItem(item);
}

void FileBrowserDialog::onRemoveClicked()
{
    for (auto *item : m_selectedList->selectedItems()) {
        delete m_selectedList->takeItem(m_selectedList->row(item));
    }
    m_okBtn->setEnabled(m_selectedList->count() > 0);
}

void FileBrowserDialog::onItemDoubleClicked(QListWidgetItem *)
{
    onRemoveClicked();
}

void FileBrowserDialog::collectFiles(const QString &path, const QString &relBase,
                                     QList<FileEntry> &out)
{
    QFileInfo fi(path);
    if (fi.isFile()) {
        FileEntry e;
        e.absolutePath  = path;
        e.displayName   = fi.fileName();
        e.relativePath  = relBase.isEmpty() ? fi.fileName()
                                            : relBase + "/" + fi.fileName();
        e.size          = fi.size();
        out << e;
        return;
    }

    // Directory — recurse
    QDirIterator it(path,
                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo sub = it.fileInfo();
        if (!sub.isFile()) continue;

        QString rel = QDir(path).relativeFilePath(sub.absoluteFilePath());
        FileEntry e;
        e.absolutePath  = sub.absoluteFilePath();
        e.displayName   = sub.fileName();
        e.relativePath  = relBase.isEmpty() ? rel : relBase + "/" + rel;
        e.size          = sub.size();
        out << e;
    }
}

// Build FileEntry list when the dialog is accepted
QList<FileEntry> FileBrowserDialog::selectedEntries() const
{
    QList<FileEntry> result;
    for (int i = 0; i < m_selectedList->count(); ++i) {
        const QString path = m_selectedList->item(i)->data(Qt::UserRole).toString();
        // collectFiles is logically const (reads disk, doesn't modify members)
        FileBrowserDialog *self = const_cast<FileBrowserDialog*>(this);
        self->collectFiles(path, "", result);
    }
    return result;
}
