#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QIntValidator>

#include <cmath>
#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {
    ui->setupUi(this);

    m_scanThread.setObjectName("scan_thread");

    m_scanWorker = new ScanWorker;
    m_scanWorker->moveToThread(&m_scanThread);
    connect(&m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);
    connect(m_scanWorker, &ScanWorker::errorHappened, this, &MainWindow::scanError);

    auto deviceListModel = new DeviceListModel(*m_scanWorker, this);
    ui->comboBox_devices->setModel(deviceListModel);
    connect(deviceListModel, &DeviceListModel::modelReset, this, &MainWindow::deviceInfoModelReset);
    connect(deviceListModel, &DeviceListModel::updateFinished, this, &MainWindow::deviceInfoUpdateFinished);

    auto oldDelegate = ui->tableView_device_opts->itemDelegate();
    ui->tableView_device_opts->setItemDelegate(new OptionItemDelegate(this));
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

void MainWindow::deviceInfoUpdateFinished(bool res) {
    ui->btnReloadDevs->setEnabled(true);
}

void MainWindow::scanError(std::string s) {
    QMessageBox::critical(this, this->windowTitle() + " - error", QString::fromLocal8Bit(s.c_str()));
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

        connect(deviceOptionModel, &DeviceOptionModel::deviceOptionsUpdated,
            ui->tableView_device_opts, &QTableView::resizeColumnsToContents);
    }
}

QWidget* OptionItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
    const QModelIndex &index) const {
    m_editingRow = index.row();

    if (auto c = index.data(DeviceOptionModel::ConstraintRole); c.isValid()) {
        if (c.canConvert<string_data_constraint>()) {
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
        }
        else if (c.canConvert<integer_data_constraint>()) {
            auto editor = std::unique_ptr<QWidget>(Base_t::createEditor(parent, option, index));
            if (auto spinBox = dynamic_cast<QSpinBox*>(editor.get())) {
                auto int_c = c.value<integer_data_constraint>();
                spinBox->setMinimum(int_c->min);
                spinBox->setMaximum(int_c->max);
                if (int_c->quant != 0)
                    spinBox->setSingleStep(int_c->quant);
            }
            return editor.release();
        }
        else if (c.canConvert<integer_data_list_constraint>()) {
            auto editor = std::make_unique<QComboBox>(parent);
            editor->setEditable(true);
            auto int_lst = c.value<integer_data_list_constraint>();
            auto count = int_lst[0];
            for (auto p = int_lst + 1; count > 0; --count, ++p)
                editor->addItem(QString::number(*p));
            editor->setValidator(new QIntValidator(
                std::numeric_limits<::SANE_Int>::min(), std::numeric_limits<::SANE_Int>::max(), editor.get()));
            return editor.release();
        }
        else if (c.canConvert<double_data_constraint>()) {
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

/*
void OptionItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const {

}
*/
/*
void OptionItemDelegate::paint(QPainter *painter,
    const QStyleOptionViewItem &option, const QModelIndex &index) const {

    auto o = option;
    o.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDecoration;
    o.showDecorationSelected = true;
    o.checkState = Qt::Checked;
    QStyledItemDelegate::paint(painter, o, index);

    auto opt = option;
    initStyleOption(&opt, index);

    if (index.parent() == QModelIndex() && index.column() == 1) {

    }

    auto* widget = option.widget;
    auto* style = widget->style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

}
*/
