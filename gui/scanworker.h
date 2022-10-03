#pragma once

#include <QObject>
#include <QThread>
#include <QAbstractListModel>
#include <QString>

#include <sane-pp.h>

#include <string>

namespace vg_sane {
    using device_infos_t = std::ranges::subrange<const ::SANE_Device**, const ::SANE_Device**>;
    using device_opts_t = std::ranges::subrange<device::option_iterator>;
}

Q_DECLARE_METATYPE(vg_sane::device_infos_t)
Q_DECLARE_METATYPE(vg_sane::device_opts_t)
Q_DECLARE_METATYPE(std::string)

/**
 * @brief scanner bounary communication object working in separate thread
 */
class ScanWorker : public QObject {
    Q_OBJECT

public:
    ScanWorker();

public slots:
    void getDeviceInfos();
//    void openDevice(std::string);

private:
    vg_sane::lib::ptr_t m_saneLib;
    vg_sane::device m_currentDevice;

signals:
    void gotDeviceInfos(vg_sane::device_infos_t);
//    void deviceOptionsGot(vg_sane::device_opts_t);
    void errorHappened(QString);
};

class DeviceListModel : public QAbstractListModel {
    Q_OBJECT

    struct DeviceInfo {
        QString m_name;
        QString m_type;
        QString m_model;
        QString m_vendor;
    };

    // Design choice: we could use a range from underlying library instead of storing copies
    // of description strings here in a vector but still need to convert strings into
    // Qt-aware types every time data is requested
    QVector<DeviceInfo> m_infos;

public:
    enum DeviceInfoRole {
        DeviceTypeRole = Qt::UserRole,
        DeviceModelRole = Qt::UserRole + 1,
        DeviceVendorRole = Qt::UserRole + 2
    };

    explicit DeviceListModel(ScanWorker& worker, QObject* parent = nullptr);

    int rowCount(const QModelIndex &parent) const override {
        return parent == QModelIndex() ? m_infos.size() : 0;
    }

    QVariant data(const QModelIndex &index, int role) const override;

private slots:
    void gotDeviceInfos(vg_sane::device_infos_t);
/*
public slots:
    void update();
*/
signals:
    void getDeviceInfos();
};
