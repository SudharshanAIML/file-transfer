// filebrowserdialog.h — custom directory/file picker with common shortcuts
#pragma once
#include <QDialog>
#include <QTreeView>
#include <QFileSystemModel>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include "transfersession.h"

class FileBrowserDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileBrowserDialog(QWidget *parent = nullptr);

    QList<FileEntry> selectedEntries() const { return m_selectedEntries; }

private slots:
    void onDirectoryActivated(const QModelIndex &index);
    void onAddClicked();
    void onRemoveClicked();
    void onShortcutClicked();
    void onItemDoubleClicked(QListWidgetItem *item);
    void updateAddButton();

private:
    void setupUi();
    void navigateTo(const QString &path);
    void addPath(const QString &path);
    void collectFiles(const QString &path, const QString &relBase,
                      QList<FileEntry> &out);

    QFileSystemModel *m_fsModel{nullptr};
    QTreeView        *m_treeView{nullptr};
    QListWidget      *m_selectedList{nullptr};
    QListWidget      *m_shortcutList{nullptr};
    QLabel           *m_currentPathLabel{nullptr};
    QPushButton      *m_addBtn{nullptr};
    QPushButton      *m_removeBtn{nullptr};
    QPushButton      *m_okBtn{nullptr};

    QList<FileEntry>  m_selectedEntries;
};
