#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "drawingsurface.h"

#include <QMetaType>
#include <QApplication>
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_optionTableResizedFirstTime(false) {
    ui->setupUi(this);

    m_scanThread.setObjectName("scan_thread");

    m_scanWorker = new ScanWorker;
    m_scanWorker->moveToThread(&m_scanThread);
    Q_ASSERT(connect(&m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater));

    auto deviceListModel = new DeviceListModel(*m_scanWorker, this);
    ui->comboBox_devices->setModel(deviceListModel);
    Q_ASSERT(connect(deviceListModel, &DeviceListModel::modelReset, this, &MainWindow::deviceInfoModelReset));
    Q_ASSERT(connect(deviceListModel, &DeviceListModel::updateFinished, this, &MainWindow::deviceInfoUpdateFinished));

    auto oldDelegate = ui->tableView_device_opts->itemDelegate();
    auto delgt = new OptionItemDelegate(this);
    ui->tableView_device_opts->setItemDelegate(delgt);

    Q_ASSERT(connect(ui->tableView_device_opts, &QTableView::pressed, delgt, &OptionItemDelegate::pressed));
    Q_ASSERT(connect(ui->tableView_device_opts, &QTableView::clicked, delgt, &OptionItemDelegate::clicked));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::updateButton, ui->tableView_device_opts,
        static_cast<void(QAbstractItemView::*)(const QModelIndex&)>(&QAbstractItemView::update)));
    Q_ASSERT(connect(delgt, &OptionItemDelegate::buttonPressed, this, &MainWindow::optionButtonPressed));

    delete oldDelegate;

    m_scanThread.start();
}

MainWindow::~MainWindow() {
    m_scanThread.requestInterruption();
    m_scanThread.quit();
    m_scanThread.wait();
    delete ui;
}

void MainWindow::on_btnReloadDevs_clicked() {
    ui->btnReloadDevs->setEnabled(false);
    ui->tableView_device_opts->setEnabled(false);
    static_cast<DeviceListModel*>(ui->comboBox_devices->model())->update();
}

void MainWindow::deviceInfoUpdateFinished(bool res, QString error) {
    if (! res)
        QMessageBox::critical(this, this->windowTitle() + tr(" - error"), error);

    ui->btnReloadDevs->setEnabled(true);
}

void MainWindow::deviceOptionsUpdateFinished(bool res, QString error) {
    if (! res)
        QMessageBox::critical(this, this->windowTitle() + tr(" - error"), error);
    else if (! m_optionTableResizedFirstTime) {
        ui->tableView_device_opts->resizeColumnsToContents();
        m_optionTableResizedFirstTime = true;
    }
}

void MainWindow::deviceInfoModelReset() {
    auto m = static_cast<DeviceListModel*>(ui->comboBox_devices->model());
    if (m->rowCount(QModelIndex()) == 0)
        // zero-sized model should cause combobox's index to reset to -1 and corresponding [indexChanged]
        // signal to be called... but it isn't. Let's call it directly
        on_comboBox_devices_currentIndexChanged(-1);
    else
        ui->tableView_device_opts->setEnabled(true);
}

void MainWindow::on_comboBox_devices_currentIndexChanged(int index) {
    if (index == -1) {
        ui->label_dev_model->clear();
        ui->label_dev_type->clear();
        ui->label_dev_vendor->clear();
    } else {
        auto m = static_cast<DeviceListModel*>(ui->comboBox_devices->model());
        ui->label_dev_model->setText(m->data(m->index(index), DeviceListModel::DeviceModelRole).toString());
        ui->label_dev_type->setText(m->data(m->index(index), DeviceListModel::DeviceTypeRole).toString());
        ui->label_dev_vendor->setText(m->data(m->index(index), DeviceListModel::DeviceVendorRole).toString());

        auto oldModel = ui->tableView_device_opts->model();
        auto deviceOptionModel = new DeviceOptionModel(
            *m_scanWorker, m->data(m->index(index), Qt::DisplayRole).toString(), this);
        ui->tableView_device_opts->setModel(deviceOptionModel);
        delete oldModel;

        Q_ASSERT(connect(deviceOptionModel, &DeviceOptionModel::deviceOptionsUpdated,
            this, &MainWindow::deviceOptionsUpdateFinished));
    }
}

void MainWindow::optionButtonPressed(const QModelIndex& index) {
    // As long as 'button' option is a special kind of option which has no value but just a side effect
    // instead, issue a 'setData()' call with any value.
    static_cast<DeviceOptionModel*>(ui->tableView_device_opts->model())->setData(index, true, Qt::EditRole);
}

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
