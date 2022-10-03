#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {
    ui->setupUi(this);

//    ui->comboBox_devices->setModel(&m_scanDeviceModel);
//    ui->listView_device_opts->setModel(&m_scanDeviceOptModel);

    m_scanThread.setObjectName("scan_thread");

    auto scanWorker = new ScanWorker;
    scanWorker->moveToThread(&m_scanThread);
    connect(&m_scanThread, &QThread::finished, scanWorker, &QObject::deleteLater);
    connect(scanWorker, &ScanWorker::errorHappened, this, &MainWindow::scanErrorHappened);
    connect(this, &MainWindow::getScanDeviceInfos, scanWorker, &ScanWorker::getDeviceInfos);
    connect(scanWorker, &ScanWorker::gotDeviceInfos, this, &MainWindow::gotScanDeviceInfos);

    auto deviceListModel = new DeviceListModel(*scanWorker, this);
    ui->comboBox_devices->setModel(deviceListModel);
    connect(deviceListModel, &DeviceListModel::modelReset, this, &MainWindow::deviceInfoModelReset);

//    connect(this, &MainWindow::getScanDeviceInfos, m_scanWorker, &ScanWorker::getDeviceInfos);
//    connect(this, &MainWindow::openScanDevice, m_scanWorker, &ScanWorker::openDevice);
//    connect(m_scanWorker, &ScanWorker::deviceInfosGot, this, &MainWindow::scanDeviceInfosGot);
//    connect(m_scanWorker, &ScanWorker::deviceOptionsGot, this, &MainWindow::scanDeviceOptionsGot);
//    connect(this, &Controller::operate, worker, &Worker::doWork);
//    connect(worker, &Worker::resultReady, this, &Controller::handleResults);

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
    ui->listView_device_opts->setEnabled(false);
    //auto i = ui->comboBox_devices->currentIndex();
    //m_scanDeviceModel.set({});
    // Though a model connected to the combo box becomes 0-sized and the combo box's current index
    // becomes -1, a slot, connected to it is not invoked on changing the index to -1. Suppose this
    // value is considered some special value which is not notified about.
    //on_comboBox_devices_currentIndexChanged(-1);
    //emit getScanDeviceInfos();
    //static_cast<DeviceListModel*>(ui->comboBox_devices->model())->update();
    emit getScanDeviceInfos();
}

void MainWindow::gotScanDeviceInfos(vg_sane::device_infos_t) {
    ui->btnReloadDevs->setEnabled(true);
    ui->listView_device_opts->setEnabled(true);
}

void MainWindow::scanErrorHappened(QString msg) {
    ui->btnReloadDevs->setEnabled(true);
    ui->listView_device_opts->setEnabled(true);

    QMessageBox::critical(this, this->windowTitle(), msg);
}

void MainWindow::deviceInfoModelReset() {
    auto m = static_cast<DeviceListModel*>(ui->comboBox_devices->model());
    if (m->rowCount(QModelIndex()) == 0) {
        auto i = ui->comboBox_devices->currentIndex();
        ui->comboBox_devices->setCurrentIndex(-1);
        qDebug() << "current index" << i << ", " << ui->comboBox_devices->currentIndex();
    }
//    qDebug() << "model reset, size" << m->rowCount(QModelIndex());
//    ui->listView_device_opts->setEnabled(true);
//    ui->btnReloadDevs->setEnabled(true);
}

/*
void MainWindow::scanDeviceInfosGot(vg_sane::device_infos_t device_infos) {
    if (! device_infos.empty()) {
        ui->listView_device_opts->setEnabled(true);
        m_scanDeviceModel.set(device_infos);
        ui->comboBox_devices->setCurrentIndex(0);
    }
    ui->btnReloadDevs->setEnabled(true);
}

void MainWindow::scanDeviceOptionsGot(vg_sane::device_opts_t device_opts) {

}
*/
void MainWindow::on_comboBox_devices_currentIndexChanged(int index) {
    qDebug() << "current index called" << index;
    if (index == -1) {
        ui->label_dev_model->clear();
        ui->label_dev_type->clear();
        ui->label_dev_vendor->clear();
    } else {
        auto m = static_cast<DeviceListModel*>(ui->comboBox_devices->model());
        ui->label_dev_model->setText(m->data(m->index(index), DeviceListModel::DeviceModelRole).toString());
        ui->label_dev_type->setText(m->data(m->index(index), DeviceListModel::DeviceTypeRole).toString());
        ui->label_dev_vendor->setText(m->data(m->index(index), DeviceListModel::DeviceVendorRole).toString());
//        emit openScanDevice(m_scanDeviceModel.get(index)->name);
    }
}
/*
void MainWindow::DeviceModel::set(vg_sane::device_infos_t val) {
    beginResetModel();
    m_deviceInfos = val;
    endResetModel();
}

QVariant MainWindow::DeviceModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex() || role != Qt::DisplayRole)
        return {};

    return m_deviceInfos[index.row()]->name;
}

QVariant MainWindow::OptionModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex() || role != Qt::DisplayRole)
        return {};
}

void MainWindow::OptionModel::set(vg_sane::device_opts_t val) {
    beginResetModel();
    m_deviceOptions = val;
    endResetModel();
}
*/
