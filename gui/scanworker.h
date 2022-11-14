#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QModelIndex>
#include <QAbstractListModel>
#include <QAbstractTableModel>

#include <QtGlobal>
#include <QDebug>

#include <sane-pp.h>

#include <string>

namespace vg_sane {
    using device_infos_t = std::ranges::subrange<const ::SANE_Device**, const ::SANE_Device**>;
    using device_opts_t = std::ranges::subrange<device::option_iterator>;
}

// Additional types for communicating between backend worker and GUI model classes
Q_DECLARE_METATYPE(vg_sane::device_infos_t)
Q_DECLARE_METATYPE(vg_sane::device_opts_t)
Q_DECLARE_METATYPE(std::string)

struct string_data_constraint {
    std::size_t m_maxLength;
    QStringList m_values;
};

struct double_data_constraint {
    double m_min;
    double m_max;
    double m_step;
};

using integer_data_constraint = const ::SANE_Range*;
using integer_data_list_constraint = const ::SANE_Word*;

Q_DECLARE_METATYPE(string_data_constraint)
Q_DECLARE_METATYPE(integer_data_constraint)
Q_DECLARE_METATYPE(integer_data_list_constraint)
Q_DECLARE_METATYPE(double_data_constraint)

/**
 * @brief scanner bounary communication object working in separate thread
 */
class ScanWorker : public QObject {
    Q_OBJECT

public:
    ScanWorker();

    // Design choice: whether it's acceptable to access next methods from GUI thread?
    vg_sane::opt_value_t getOptionValue(int pos) const;
    vg_sane::device::set_opt_result_t setOptionValue(int pos, vg_sane::opt_value_t);

public slots:
    void getDeviceInfos();
    void openDevice(std::string);
    void getDeviceOptions();

private:
    vg_sane::lib::ptr_t m_saneLib;
    vg_sane::device m_currentDevice;

signals:
    void gotDeviceInfos(vg_sane::device_infos_t);
    void gotDeviceOptions(vg_sane::device_opts_t);
    void errorHappened(std::string);
};

// Design choice: should we provide the only interface via this model to communicate with the
// worker? Or it should be freely accessed outside this model?
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
    // Qt-aware types every time data is requested.
    QVector<DeviceInfo> m_infos;
    bool m_updateInProgress = false;

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
    void gotError(std::string);

public slots:
    /**
     * @brief starts asynchronous updating procedure
     *
     * At the end a signal updateFinished() is raised with status and optional error string.
     */
    void update();

signals:
    // private signals
    //
    void getDeviceInfos();

    // public signals
    //
    void updateFinished(bool);
};


class DeviceOptionModel : public QAbstractTableModel {
    Q_OBJECT

    ScanWorker& m_worker;
    vg_sane::device_opts_t m_deviceOptionDescrs;
    bool m_updateInProgress = false;

public:
    enum Columns {
        ColumnTitle,
        ColumnValue,
        ColumnUnit,
        ColumnLast
    };

    enum DeviceInfoRole {
        // This role returns QVariant with optionally one of XXX_constraint types declared above
        ConstraintRole = Qt::UserRole
    };

    explicit DeviceOptionModel(ScanWorker& worker, QString name, QObject* parent = nullptr);

    int rowCount(const QModelIndex &parent) const override {
        return parent == QModelIndex() ? m_deviceOptionDescrs.size() : 0;
    }

    int columnCount(const QModelIndex &parent) const override {
        return parent == QModelIndex() ? ColumnLast : 0;
    }

    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return section == ColumnTitle ? tr("Property")
                : section == ColumnValue ? tr("Value")
                : section == ColumnUnit ? tr("Unit") : QString();
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
    void gotDeviceOptions(vg_sane::device_opts_t);
    void gotError(std::string);

signals:
    // private signals
    //
    void openDevice(std::string);
    void getDeviceOptions();

    // public signals
    void deviceOptionsUpdated();
};
