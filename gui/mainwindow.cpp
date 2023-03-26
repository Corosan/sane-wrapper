#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "drawingsurface.h"
#include "capturer.h"

#include <QMetaType>
#include <QApplication>
#include <QScopedPointer>
#include <QMessageBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QIntValidator>
#include <QDoubleValidator>
#include <QItemEditorCreatorBase>
#include <QFrame>
#include <QCloseEvent>
#include <QScrollBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>

#include <QtGlobal>
#include <QtDebug>

#include <cmath>
#include <memory>

MainWindow::MainWindow(vg_sane::lib::ptr_t saneLibWrapper, QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_saneLibWrapperPtr{saneLibWrapper} {

    m_ui->setupUi(this);

    m_ui->ruller_top->setOrientation(Ruller::Orientation::Top);
    m_ui->ruller_right->setOrientation(Ruller::Orientation::Right);
    m_ui->ruller_bottom->setOrientation(Ruller::Orientation::Bottom);
    m_ui->ruller_left->setOrientation(Ruller::Orientation::Left);

    m_ui->statusbar->addWidget(new QLabel(m_ui->statusbar), 1);

    auto statusBarSep = new QFrame(m_ui->statusbar);
    statusBarSep->setFrameShape(QFrame::VLine);
    m_ui->statusbar->addPermanentWidget(statusBarSep, 0);

    m_ui->statusbar->addPermanentWidget((m_scaleStatusLabel = new QLabel(m_ui->statusbar)), 0);

    statusBarSep = new QFrame(m_ui->statusbar);
    statusBarSep->setFrameShape(QFrame::VLine);
    m_ui->statusbar->addPermanentWidget(statusBarSep, 0);

    m_ui->statusbar->addPermanentWidget((m_rullerUnitsLabel = new QLabel(m_ui->statusbar)), 0);

    // TODO: is it worth to switch on these optimizations as long as the whole underlying picture
    // in drawing surface is refreshed?
    m_ui->scrollAreaWidgetContents->setAutoFillBackground(false);
    m_ui->scrollAreaWidgetContents->setAttribute(Qt::WA_NoSystemBackground);
    Q_ASSERT(connect(m_ui->scrollAreaWidgetContents, &DrawingSurface::scaleChanged,
        this, &MainWindow::drawingImageScaleChanged));
    Q_ASSERT(connect(m_ui->scrollAreaWidgetContents, &DrawingSurface::mainImageGeometryChanged,
        this, &MainWindow::drawingImageGeometryChanged));
    Q_ASSERT(connect(m_ui->scrollAreaWidgetContents, &DrawingSurface::mainImageMoved,
        this, &MainWindow::drawingImageMoved));
    drawingImageScaleChanged(m_ui->scrollAreaWidgetContents->getScale());

    auto deviceListModel = new DeviceListModel(m_saneLibWrapperPtr, this);
    m_ui->comboBox_devices->setModel(deviceListModel);

    auto oldDelegate = m_ui->tableView_device_opts->itemDelegate();
    auto delgt = new OptionItemDelegate(this);
    m_ui->tableView_device_opts->setItemDelegate(delgt);

    Q_ASSERT(connect(m_ui->tableView_device_opts, &QTableView::pressed, delgt, &OptionItemDelegate::pressed));
    Q_ASSERT(connect(m_ui->tableView_device_opts, &QTableView::clicked, delgt, &OptionItemDelegate::clicked));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::updateButton, m_ui->tableView_device_opts,
        static_cast<void(QAbstractItemView::*)(const QModelIndex&)>(&QAbstractItemView::update)));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::buttonPressed, this, &MainWindow::optionButtonPressed));

    delete oldDelegate;

    QSettings s;
    restoreGeometry(s.value("MainWindow/geometry").toByteArray());
    restoreState(s.value("MainWindow/state").toByteArray());
}

MainWindow::~MainWindow() = default;

void MainWindow::on_btnReloadDevs_clicked() {
    auto model = static_cast<DeviceListModel*>(m_ui->comboBox_devices->model());

    try {
        model->update();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, this->windowTitle(),
            tr("Unable to update a list of devices: %1").arg(QString::fromLocal8Bit(e.what())));
    } catch (...) {
        QMessageBox::critical(this, this->windowTitle(),
            tr("Unable to update a list of devices, no additional info"));
    }

    if (model->rowCount({}) == 0) {
        // zero-sized model should cause combobox's index to reset to -1 and corresponding [indexChanged]
        // signal to be called... but it isn't. Let's call it directly
        on_comboBox_devices_currentIndexChanged(-1);
        m_ui->tableView_device_opts->setEnabled(false);
    } else {
        m_ui->tableView_device_opts->setEnabled(true);
    }
}

void MainWindow::on_comboBox_devices_currentIndexChanged(int index) {
    if (index == -1) {
        m_ui->label_dev_model->clear();
        m_ui->label_dev_type->clear();
        m_ui->label_dev_vendor->clear();
        m_ui->label_cap_model->setEnabled(false);
        m_ui->label_cap_type->setEnabled(false);
        m_ui->label_cap_vendor->setEnabled(false);
        m_ui->actionStartScan->setEnabled(false);
    } else {
        m_ui->label_cap_model->setEnabled(true);
        m_ui->label_cap_type->setEnabled(true);
        m_ui->label_cap_vendor->setEnabled(true);

        auto m = static_cast<DeviceListModel*>(m_ui->comboBox_devices->model());
        m_ui->label_dev_model->setText(m->data(m->index(index), DeviceListModel::DeviceModelRole).toString());
        m_ui->label_dev_type->setText(m->data(m->index(index), DeviceListModel::DeviceTypeRole).toString());
        m_ui->label_dev_vendor->setText(m->data(m->index(index), DeviceListModel::DeviceVendorRole).toString());

        // As long as old Device Option Model can be tied to current scanner device, firstly the model
        // should be destroyed and then - the device released.
        auto oldModel = m_ui->tableView_device_opts->model();
        m_ui->tableView_device_opts->setModel(nullptr);
        delete oldModel;

        m_scannerDevice = {};

        bool fullyInitializedDevice = true;
        try {
            m_scannerDevice = m->openDevice(index);
        } catch (const std::exception& e) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to open device \"%1\": %2")
                    .arg(m_ui->comboBox_devices->itemText(index)).arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to open device \"%1\", no additional info")
                    .arg(m_ui->comboBox_devices->itemText(index)));
        }

        QScopedPointer<DeviceOptionModel> modelPtr;
        try {
            modelPtr.reset(new DeviceOptionModel(m_scannerDevice, this));
            Q_ASSERT(connect(modelPtr.get(), &DeviceOptionModel::error, this, &MainWindow::optionModelError));
        } catch (const std::exception& e) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to observe device \"%1\" options: %2")
                    .arg(m_ui->comboBox_devices->itemText(index)).arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to observe device \"%1\" options, no additional info")
                    .arg(m_ui->comboBox_devices->itemText(index)));
        }

        m_ui->tableView_device_opts->setModel(modelPtr.take());

        if (fullyInitializedDevice)
            m_ui->tableView_device_opts->resizeColumnsToContents();

        m_ui->actionStartScan->setEnabled(fullyInitializedDevice);
    }
}

void MainWindow::optionModelError(QString msg) {
    QMessageBox::critical(this, this->windowTitle(),
        tr("Error happened while changing scanner options. Try to re-open the device. "
           "Additional details:\n%1").arg(msg));
}

void MainWindow::optionButtonPressed(const QModelIndex& index) {
    // As long as 'button' option is a special kind of option which has no value but just a side effect
    // instead, issue a 'setData()' call with any value.
    static_cast<DeviceOptionModel*>(m_ui->tableView_device_opts->model())->setData(index, true, Qt::EditRole);
}

void MainWindow::on_actionStartScan_triggered() {
    qDebug() << "action::start";

    m_imageCapturer.reset(new Capturer(m_scannerDevice, *m_ui->scrollAreaWidgetContents));
    Q_ASSERT(connect(m_imageCapturer.get(), &Capturer::finished, this, &MainWindow::scannedImageGot));
    Q_ASSERT(connect(m_imageCapturer.get(), &Capturer::progress, this, &MainWindow::scanProgress));

    m_lastScannedPicDPI = -1.0;
    auto model = static_cast<DeviceOptionModel*>(m_ui->tableView_device_opts->model());
    QRect scanArea = model->getScanAreaPx(&m_lastScannedPicDPI);

    if (m_lastScannedPicDPI > 0.0)
        m_scannerToScreenDPIScale = physicalDpiX() / m_lastScannedPicDPI;
    else
        m_scannerToScreenDPIScale = 1.0;

    // Set initial scale for displaying scanned image as 1-1 to real world (as long as the scanner DPI and the screen
    // resolution are reported correctly)
    m_ui->scrollAreaWidgetContents->setScale(m_scannerToScreenDPIScale);

    model->enable(false);
    m_ui->comboBox_devices->setEnabled(false);
    m_ui->btnReloadDevs->setEnabled(false);
    m_ui->actionStopScan->setEnabled(true);
    m_ui->actionStartScan->setEnabled(false);
    m_ui->actionSave->setEnabled(false);

    m_ui->actionMirrorVert->setEnabled(false);
    m_ui->actionMirrorHorz->setEnabled(false);
    m_ui->actionRotateClockwise->setEnabled(false);
    m_ui->actionRotateCounterClockwise->setEnabled(false);

    m_ui->statusbar->showMessage(tr("Scanning..."));

    qDebug() << "action::start - calling capturer::start";

    int lineCountHint = scanArea.isValid() ? scanArea.height(): -1;
    m_imageCapturer->start(lineCountHint);
}

void MainWindow::scanProgress(QVariant prgs) {
    if ((QMetaType::Type)prgs.type() == QMetaType::Double)
        m_ui->statusbar->showMessage(tr("Scanning... %L1%").arg(prgs.toDouble(), 0, 'f', 1));
    else
        m_ui->statusbar->showMessage(tr("Scanning... %1 bytes").arg(prgs.toInt()));
}

void MainWindow::on_actionStopScan_triggered() {
    m_ui->statusbar->showMessage(tr("Cancelling..."));
    m_imageCapturer->cancel();
}

void MainWindow::scannedImageGot(bool status, QString errMsg) {
    qDebug() << "scanning finished, status =" << status;

    m_imageCapturer.reset();

    static_cast<DeviceOptionModel*>(m_ui->tableView_device_opts->model())->enable(true);
    m_ui->comboBox_devices->setEnabled(true);
    m_ui->btnReloadDevs->setEnabled(true);
    m_ui->actionStopScan->setEnabled(false);
    m_ui->actionStartScan->setEnabled(true);

    m_ui->statusbar->clearMessage();

    if (status) {
        m_ui->actionSave->setEnabled(true);
        m_ui->actionMirrorVert->setEnabled(true);
        m_ui->actionMirrorHorz->setEnabled(true);
        m_ui->actionRotateClockwise->setEnabled(true);
        m_ui->actionRotateCounterClockwise->setEnabled(true);
    } else {
        QMessageBox::critical(this, this->windowTitle() + tr(" - error"), errMsg);
    }
}

void MainWindow::on_actionSave_triggered() {
    auto pathToSave = QFileDialog::getSaveFileName(this, tr("Save Image to a file"), QString{},
        tr("Jpeg images (*.jpg *.jpeg)(*.jpg *.jpeg);;Png images (*.png)(*.png);;All files (*.*)(*)"));

    if (pathToSave.isEmpty())
        return;

    if (QFileInfo{pathToSave}.completeSuffix().isEmpty()) {
        QMessageBox::critical(this, this->windowTitle(),
            tr("Please provide destination file name with one of supported extensions (see filters in the save dialog)"));
        return;
    }

    if (! m_ui->scrollAreaWidgetContents->getImage().save(pathToSave))
        QMessageBox::critical(this, this->windowTitle(),
            tr("Error happened during saving the image into:\n%1").arg(pathToSave));
    else
        m_ui->statusbar->showMessage(tr("The image stored into %1").arg(pathToSave), 2000);
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    if (m_imageCapturer) {
        QMessageBox::information(this, this->windowTitle(),
            tr("The application can't be closed while scanning operation is in progress"));
        ev->ignore();
    } else {
        QSettings s;
        s.setValue("MainWindow/state", saveState());
        s.setValue("MainWindow/geometry", saveGeometry());
        ev->accept();
    }
}

void MainWindow::on_actionZoomIn_triggered() {
    m_ui->scrollAreaWidgetContents->setScale(m_ui->scrollAreaWidgetContents->getScale() * 2);
}


void MainWindow::on_actionZoomOut_triggered() {
    m_ui->scrollAreaWidgetContents->setScale(m_ui->scrollAreaWidgetContents->getScale() / 2);
}

void MainWindow::on_actionMirrorVert_triggered() {
    m_ui->scrollAreaWidgetContents->mirror(true);
}

void MainWindow::on_actionMirrorHorz_triggered() {
    m_ui->scrollAreaWidgetContents->mirror(false);
}

void MainWindow::on_actionRotateClockwise_triggered() {
    m_ui->scrollAreaWidgetContents->rotate(true);
}

void MainWindow::on_actionRotateCounterClockwise_triggered() {
    m_ui->scrollAreaWidgetContents->rotate(false);
}

void MainWindow::drawingImageScaleChanged(float scale) {
    // The scaling is reported relative to real world in a sense that all the geometry of a scanned image
    // is calculated respective to the screen DPI
    m_scaleStatusLabel->setText(tr("x %1").arg(QLocale().toString(scale / m_scannerToScreenDPIScale)));
}

void MainWindow::drawingImageGeometryChanged(QRect geometry) {
    m_ui->ruller_top->setParams(geometry.x(), geometry.width(),
        m_lastScannedPicDPI, m_ui->scrollAreaWidgetContents->getScale());
    m_ui->ruller_bottom->setParams(geometry.x(), geometry.width(),
        m_lastScannedPicDPI, m_ui->scrollAreaWidgetContents->getScale());
    m_ui->ruller_left->setParams(geometry.y(), geometry.height(),
        m_lastScannedPicDPI, m_ui->scrollAreaWidgetContents->getScale());
    m_ui->ruller_right->setParams(geometry.y(), geometry.height(),
        m_lastScannedPicDPI, m_ui->scrollAreaWidgetContents->getScale());

    m_rullerUnitsLabel->setText(m_ui->ruller_top->isCm() ? tr("cm") : tr("mm"));

    m_ui->actionZoomIn->setEnabled(geometry.isValid());
    m_ui->actionZoomOut->setEnabled(geometry.isValid());
}

void MainWindow::drawingImageMoved(QPoint pos, QPoint oldPos) {
    if (pos.x() != oldPos.x()) {
        m_ui->ruller_top->scrollBy(pos.x() - oldPos.x());
        m_ui->ruller_bottom->scrollBy(pos.x() - oldPos.x());
    }
    if (pos.y() != oldPos.y()) {
        m_ui->ruller_left->scrollBy(pos.y() - oldPos.y());
        m_ui->ruller_right->scrollBy(pos.y() - oldPos.y());
    }
}

//--------------------------------------------------------------------------------------------------
void OptionItemDelegate::pressed(const QModelIndex& index) {
    if (index.column() == 1) {
        if (auto val = index.data(DeviceOptionModel::ButtonRole);
            val.type() == qMetaTypeId<bool>()
            && val.toBool()) {

            m_pressedIndex = index;
            emit updateButton(index);
        }
    }
}

void OptionItemDelegate::clicked(const QModelIndex& index) {
    if (m_pressedIndex.isValid()) {
        m_pressedIndex = {};
        emit updateButton(index);
        emit buttonPressed(index);
    }
}

void OptionItemDelegate::paint(QPainter *painter,
    const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (index.column() == 1) {
        if (auto val = index.data(DeviceOptionModel::ButtonRole);
            val.type() == qMetaTypeId<bool>()
            && val.toBool()) {

            QStyleOptionButton btnStyle;
            static_cast<QStyleOption&>(btnStyle) = option;
            if (m_pressedIndex == index)
                btnStyle.state |= QStyle::State_Sunken;
            const QWidget* widget = option.widget;
            QStyle* style = widget ? widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_PushButton, &btnStyle, painter, widget);
            return;
        }
    }

    Base_t::paint(painter, option, index);
}

/*!
   \brief Create various types of editors for editing device options with corresponding constraints
          (list, range, ...)
 */
QWidget* OptionItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
    const QModelIndex &index) const {
    m_editingRow = index.row();

    if (auto c = index.data(DeviceOptionModel::ConstraintRole); c.isValid()) {
        if (c.userType() == qMetaTypeId<string_data_constraint>()) {
            const auto& str_c = c.value<string_data_constraint>();
            if (str_c.m_values.empty()) {
                auto editor = std::make_unique<QLineEdit>(parent);
                editor->setMaxLength(str_c.m_maxLength);
                return editor.release();
            }
            auto editor = std::make_unique<QComboBox>(parent);
            editor->setEditable(true);
            editor->addItems(str_c.m_values);
            editor->lineEdit()->setMaxLength(str_c.m_maxLength);
            return editor.release();
        } else if (c.userType() == qMetaTypeId<integer_data_constraint>()) {
            auto editor = std::unique_ptr<QWidget>(Base_t::createEditor(parent, option, index));
            if (auto spinBox = dynamic_cast<QSpinBox*>(editor.get())) {
                auto int_c = c.value<integer_data_constraint>();
                spinBox->setMinimum(int_c->min);
                spinBox->setMaximum(int_c->max);
                if (int_c->quant != 0)
                    spinBox->setSingleStep(int_c->quant);
            }
            return editor.release();
        } else if (c.userType() == qMetaTypeId<integer_data_list_constraint>()) {
            auto editor = std::make_unique<QComboBox>(parent);
            editor->setEditable(true);
            auto int_lst = c.value<integer_data_list_constraint>();
            auto count = int_lst[0];
            for (auto p = int_lst + 1; count > 0; --count, ++p)
                editor->addItem(QLocale().toString(*p));
            editor->setValidator(new QIntValidator(
                std::numeric_limits<::SANE_Int>::min(), std::numeric_limits<::SANE_Int>::max(), editor.get()));
            return editor.release();
        } else if (c.userType() == qMetaTypeId<double_data_constraint>()) {
            auto editor = std::unique_ptr<QWidget>(Base_t::createEditor(parent, option, index));
            if (auto spinBox = dynamic_cast<QDoubleSpinBox*>(editor.get())) {
                auto double_c = c.value<double_data_constraint>();
                spinBox->setDecimals(5);
                spinBox->setMinimum(double_c.m_min);
                spinBox->setMaximum(double_c.m_max);
                if (std::fabs(double_c.m_step) >= std::numeric_limits<double>::epsilon())
                    spinBox->setSingleStep(double_c.m_step);
            }
            return editor.release();
        } else if (c.userType() == qMetaTypeId<double_data_list_constraint>()) {
            auto editor = std::make_unique<QComboBox>(parent);
            editor->setEditable(true);
            auto double_c = c.value<double_data_list_constraint>();
            for (auto v : double_c.m_values)
                editor->addItem(QLocale().toString(v));
            editor->setValidator(new QDoubleValidator(
                double_c.m_min, double_c.m_max, /*decimals*/ 5, editor.get()));
            return editor.release();
        }
    }

    return Base_t::createEditor(parent, option, index);
}

void OptionItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    if (auto p = dynamic_cast<QComboBox*>(editor))
        p->setCurrentText(index.data(Qt::EditRole).toString());
    else
        Base_t::setEditorData(editor, index);
}
