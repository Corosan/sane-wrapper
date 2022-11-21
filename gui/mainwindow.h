#pragma once

#include "scanworker.h"

#include <QMainWindow>
#include <QThread>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QItemEditorFactory>

#include <QtGlobal>
#include <QDebug>

#include <string>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void deviceInfoModelReset();
    void deviceInfoUpdateFinished(bool, QString);
    void deviceOptionsUpdateFinished(bool, QString);
    void optionButtonPressed(const QModelIndex&);

    void on_btnReloadDevs_clicked();
    void on_comboBox_devices_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;
    QThread m_scanThread;
    ScanWorker* m_scanWorker;
};

class OptionItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

    using Base_t = QStyledItemDelegate;

    mutable int m_editingRow = -1;

public:
    //explicit OptionItemDelegate(QObject* parent = nullptr);
    using Base_t::QStyledItemDelegate;

    void paint(QPainter *painter,
        const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override;

    void destroyEditor(QWidget *editor, const QModelIndex &index) const override {
        m_editingRow = -1;
        Base_t::destroyEditor(editor, index);
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        Base_t::initStyleOption(option, index);

        // A check box is used for displaying boolean device properties.
        // But no check boxes should be displayed while editing
        if (m_editingRow != -1 && index.row() == m_editingRow && index.parent() == QModelIndex() && index.column() == 1) {
            option->features &= ~QStyleOptionViewItem::HasCheckIndicator;
        }
    }

private:
    QModelIndex m_pressedIndex;

public slots:
    void pressed(const QModelIndex&);
    void clicked(const QModelIndex&);

signals:
    void updateButton(const QModelIndex&);
    void buttonPressed(const QModelIndex&);
};
