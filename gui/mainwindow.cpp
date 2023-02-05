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

#include <cmath>
#include <memory>

MainWindow::MainWindow(vg_sane::lib::ptr_t saneLibWrapper, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_saneLibWrapperPtr{saneLibWrapper} {
    ui->setupUi(this);
    ui->statusbar->addWidget((m_scaleStatusLabel = new QLabel(ui->statusbar)), 0);

    Q_ASSERT(connect(ui->scrollAreaWidgetContents, &DrawingSurface::scaleChanged, this, &MainWindow::drawingScaleChanged));
    drawingScaleChanged(ui->scrollAreaWidgetContents->getScale());

    auto deviceListModel = new DeviceListModel(m_saneLibWrapperPtr, this);
    ui->comboBox_devices->setModel(deviceListModel);

    auto oldDelegate = ui->tableView_device_opts->itemDelegate();
    auto delgt = new OptionItemDelegate(this);
    ui->tableView_device_opts->setItemDelegate(delgt);

    Q_ASSERT(connect(ui->tableView_device_opts, &QTableView::pressed, delgt, &OptionItemDelegate::pressed));
    Q_ASSERT(connect(ui->tableView_device_opts, &QTableView::clicked, delgt, &OptionItemDelegate::clicked));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::updateButton, ui->tableView_device_opts,
        static_cast<void(QAbstractItemView::*)(const QModelIndex&)>(&QAbstractItemView::update)));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::buttonPressed, this, &MainWindow::optionButtonPressed));

    delete oldDelegate;
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::on_btnReloadDevs_clicked() {
    auto model = static_cast<DeviceListModel*>(ui->comboBox_devices->model());

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
        ui->tableView_device_opts->setEnabled(false);
    } else {
        ui->tableView_device_opts->setEnabled(true);
    }
}

void MainWindow::on_comboBox_devices_currentIndexChanged(int index) {
    if (index == -1) {
        ui->label_dev_model->clear();
        ui->label_dev_type->clear();
        ui->label_dev_vendor->clear();
        ui->label_cap_model->setEnabled(false);
        ui->label_cap_type->setEnabled(false);
        ui->label_cap_vendor->setEnabled(false);
        ui->actionStart->setEnabled(false);
    } else {
        ui->label_cap_model->setEnabled(true);
        ui->label_cap_type->setEnabled(true);
        ui->label_cap_vendor->setEnabled(true);

        auto m = static_cast<DeviceListModel*>(ui->comboBox_devices->model());
        ui->label_dev_model->setText(m->data(m->index(index), DeviceListModel::DeviceModelRole).toString());
        ui->label_dev_type->setText(m->data(m->index(index), DeviceListModel::DeviceTypeRole).toString());
        ui->label_dev_vendor->setText(m->data(m->index(index), DeviceListModel::DeviceVendorRole).toString());

        // As long as old Device Option Model can be tied to current scanner device, firstly the model
        // should be destroyed and then - the device released.
        auto oldModel = ui->tableView_device_opts->model();
        ui->tableView_device_opts->setModel(nullptr);
        delete oldModel;

        m_scannerDevice = {};

        bool fullyInitializedDevice = true;
        try {
            m_scannerDevice = m->openDevice(index);
        } catch (const std::exception& e) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to open device \"%1\": %2")
                    .arg(ui->comboBox_devices->itemText(index)).arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to open device \"%1\", no additional info")
                    .arg(ui->comboBox_devices->itemText(index)));
        }

        QScopedPointer<DeviceOptionModel> modelPtr;
        try {
            modelPtr.reset(new DeviceOptionModel(m_scannerDevice, this));
            Q_ASSERT(connect(modelPtr.get(), &DeviceOptionModel::error, this, &MainWindow::optionModelError));
        } catch (const std::exception& e) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to observe device \"%1\" options: %2")
                    .arg(ui->comboBox_devices->itemText(index)).arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            fullyInitializedDevice = false;
            QMessageBox::critical(this, this->windowTitle(),
                tr("Unable to observe device \"%1\" options, no additional info")
                    .arg(ui->comboBox_devices->itemText(index)));
        }

        ui->tableView_device_opts->setModel(modelPtr.take());

        if (fullyInitializedDevice)
            ui->tableView_device_opts->resizeColumnsToContents();

        ui->actionStart->setEnabled(fullyInitializedDevice);
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
    static_cast<DeviceOptionModel*>(ui->tableView_device_opts->model())->setData(index, true, Qt::EditRole);
}

void MainWindow::on_actionStart_triggered() {
    m_imageCapturer.reset(new Capturer(m_scannerDevice, *ui->scrollAreaWidgetContents));
    Q_ASSERT(connect(m_imageCapturer.get(), &Capturer::finished, this, &MainWindow::scannedImageGot));

    static_cast<DeviceOptionModel*>(ui->tableView_device_opts->model())->enable(false);
    ui->comboBox_devices->setEnabled(false);
    ui->btnReloadDevs->setEnabled(false);
    ui->actionStop->setEnabled(true);
    ui->actionStart->setEnabled(false);

    m_imageCapturer->start();
}

void MainWindow::on_actionStop_triggered() {
    m_imageCapturer->cancel();
}

void MainWindow::scannedImageGot(bool status, QString errMsg) {
    m_imageCapturer.reset();

    static_cast<DeviceOptionModel*>(ui->tableView_device_opts->model())->enable(true);
    ui->comboBox_devices->setEnabled(true);
    ui->btnReloadDevs->setEnabled(true);
    ui->actionStop->setEnabled(false);
    ui->actionStart->setEnabled(true);

    if (! status)
        QMessageBox::critical(this, this->windowTitle() + tr(" - error"), errMsg);
}

void MainWindow::on_actionZoomIn_triggered() {
    ui->scrollAreaWidgetContents->setScale(ui->scrollAreaWidgetContents->getScale() * 2);
}


void MainWindow::on_actionZoomOut_triggered() {
    ui->scrollAreaWidgetContents->setScale(ui->scrollAreaWidgetContents->getScale() / 2);
}

void MainWindow::drawingScaleChanged(float scale) {
    m_scaleStatusLabel->setText(tr("scale: %1").arg(scale));
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
