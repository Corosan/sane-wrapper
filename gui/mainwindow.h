#pragma once

#include "scanworker.h"

#include <QMainWindow>
#include <QThread>
#include <QAbstractListModel>
#include <QAbstractTableModel>

#include <QtGlobal>
#include <QDebug>

#include <string>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
/*
    // TODO: whether to replace model each time underlying data is changed? Or have 'set' method
    // which resets the model instead? The question is related to all the models here.
    class DeviceModel : public QAbstractListModel {
        vg_sane::device_infos_t m_deviceInfos;

    public:
        int rowCount(const QModelIndex &parent) const override {
            return parent == QModelIndex() ? m_deviceInfos.size() : 0;
        }
        QVariant data(const QModelIndex &index, int role) const override;
        auto& get(int row) const {
            return m_deviceInfos[row];
        }
        void set(vg_sane::device_infos_t val);
    };

    class OptionModel : public QAbstractTableModel {
        vg_sane::device_opts_t m_deviceOptions;

    public:
        int rowCount(const QModelIndex &parent) const override {
            return parent == QModelIndex() ? m_deviceOptions.size() : 0;
        }
        int columnCount(const QModelIndex &parent) const override {
            return parent == QModelIndex() ? 3 : 0;
        }
        QVariant data(const QModelIndex &index, int role) const override;
        void set(vg_sane::device_opts_t val);
    };
*/
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
//    void scanDeviceOptionsGot(vg_sane::device_opts_t);
    void deviceInfoModelReset();

    void gotScanDeviceInfos(vg_sane::device_infos_t);
    void scanErrorHappened(QString);

    void on_btnReloadDevs_clicked();
    void on_comboBox_devices_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;
    QThread m_scanThread;
//    DeviceModel m_scanDeviceModel;
//    OptionModel m_scanDeviceOptModel;

signals:
    void getScanDeviceInfos();
//    void openScanDevice(std::string);
};
