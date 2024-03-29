#pragma once

#include "scanworker.h"

#include <QScopedPointer>
#include <QString>
#include <QRect>
#include <QVariant>
#include <QMainWindow>
#include <QLabel>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QItemEditorFactory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class Capturer;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(vg_sane::lib::ptr_t saneLibWrapper, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void optionButtonPressed(const QModelIndex&);
    void optionModelError(QString);
    void scannedImageGot(bool, QString);
    void onDrawingImageScaleChanged(float);
    void onDrawingImageMoved(QPoint, QPoint);
    void onDrawingImageGeometryChanged(QRect);
    void onRedrawRullerZone(bool, int, int, int);
    void scanProgress(QVariant);

    void on_btnReloadDevs_clicked();
    void on_comboBox_devices_currentIndexChanged(int index);
    void on_actionStartScan_triggered();
    void on_actionStopScan_triggered();
    void on_actionSave_triggered();
    void on_actionZoomIn_triggered();
    void on_actionZoomOut_triggered();
    void on_actionMirrorVert_triggered();
    void on_actionMirrorHorz_triggered();
    void on_actionRotateClockwise_triggered();
    void on_actionRotateCounterClockwise_triggered();

private:
    QScopedPointer<Ui::MainWindow> m_ui;
    QLabel* m_scaleStatusLabel;
    QLabel* m_modeStatusLabel;
    QLabel* m_rullerUnitsLabel;

    vg_sane::lib::ptr_t m_saneLibWrapperPtr;
    vg_sane::device m_scannerDevice;
    double m_lastScannedPicDPI = -1.0;
    double m_scannerToScreenDPIScale = 1.0;

    QScopedPointer<Capturer> m_imageCapturer;

    void closeEvent(QCloseEvent* ev) override;
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
