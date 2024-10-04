#pragma once

#include "scanworker.h"
#include "surface_widgets.h"

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

namespace drawing {

class DashedCursorController;

}

class MainWindow final : public QMainWindow, drawing::IPlaneProvider {
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
    void onDashCursorPositionChanged(int, int);
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
    void on_actionDashCursor_triggered();

private:
    std::unique_ptr<Ui::MainWindow> m_ui;
    /*!
     * \brief current display scale of the drawing surface as it's written on a status bar
     */
    QLabel* m_scaleStatusLabel;
    /*!
     * \brief name of units assumed by rullers around the drawing surface as it's written on a status bar
     */
    QLabel* m_rullerUnitsLabel;
    /*!
     * \brief position of dash cursor on the drawing surface as it's written on a status bar
     */
    QLabel* m_dashPointPositionLabel;

    vg_sane::lib::ptr_t m_saneLibWrapperPtr;
    vg_sane::device m_scannerDevice;
    double m_lastScannedPicDPI = -1.0;
    double m_scannerToScreenDPIScale = 1.0;

    QPoint m_scannedImageOffset;

    std::unique_ptr<Capturer> m_imageCapturer;
    std::unique_ptr<drawing::DashedCursorController> m_dashedCursorController;

    void closeEvent(QCloseEvent*) override;
    void showEvent(QShowEvent*) override;

    drawing::IPlane& getRullerTopPlane() override;
    drawing::IPlane& getRullerBottomPlane() override;
    drawing::IPlane& getRullerLeftPlane() override;
    drawing::IPlane& getRullerRightPlane() override;
    drawing::IPlane& getSurfacePlane() override;
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
