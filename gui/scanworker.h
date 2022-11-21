#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QModelIndex>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QSemaphore>

#include <QtGlobal>
#include <QDebug>

#include <sane-pp.h>

#include <string>
#include <variant>
#include <exception>

// Additional types and registrations used for communicating with ScanWorker object
//
using DeviceInfosOrError = std::variant<
    std::ranges::subrange<const ::SANE_Device**, const ::SANE_Device**>,
    std::exception_ptr>;

using DeviceOptionsOrError = std::variant<
    std::ranges::subrange<vg_sane::device::option_iterator>,
    std::exception_ptr>;

Q_DECLARE_METATYPE(DeviceInfosOrError)
Q_DECLARE_METATYPE(DeviceOptionsOrError)
Q_DECLARE_METATYPE(std::string)

/*!
   \brief The Scanner bounary communication object working in separate thread. All the interaction
          with SANE library is done in that thread.
 */
class ScanWorker : public QObject {
    Q_OBJECT

public:
    ScanWorker();

public slots:
    /*!
     * \brief Starts asynchronous operation to get a list of available devices.
     *
     * Result will be returned in a form of asynchronous signal gotDeviceInfos().
     */
    void getDeviceInfos();

    /*!
     * \brief Starts asynchronous operation to open specified device and get a list of its
     *        available options.
     *
     * Result will be returned in a form of asynchronous signal gotDeviceOptions().
     */
    void openDevice(std::string);

    /*!
     * \brief Starts asynchronous operation to get a list of available options for already
     *        opened device.
     *
     * Result will be returned in a form of asynchronous signal gotDeviceOptions().
     */
    void getDeviceOptions();

    void getOptionValue(QSemaphore*, int, vg_sane::opt_value_t*, std::exception_ptr*) const;
    void setOptionValue(QSemaphore*, int, vg_sane::opt_value_t*, vg_sane::device::set_opt_result_t*, std::exception_ptr*);

private:
    vg_sane::lib::ptr_t m_saneLib;
    vg_sane::device m_currentDevice;

signals:
    void gotDeviceInfos(DeviceInfosOrError);
    void gotDeviceOptions(DeviceOptionsOrError);
};


// Design choice: should we provide the only interface via this model to communicate with the
// worker? Or it should be freely accessed outside this model?
/*!
   \brief The The Model part of Model/View Framework representing all available devices connected to
          the machine this program is running on.
 */
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
    void gotDeviceInfos(DeviceInfosOrError);

public slots:
    /*!
       \brief starts asynchronous updating procedure. At the end a signal updateFinished() is raise
              with status and optional error string.
     */
    void update();

signals:
    // private signals
    //
    void getDeviceInfos();

    // public signals
    //
    void updateFinished(bool, QString);
};

// Additional types and registrations used for communicating with ScanWorker object
//
struct string_data_constraint {
    std::size_t m_maxLength;
    QStringList m_values;
};

struct double_data_constraint {
    double m_min;
    double m_max;
    double m_step;
};

struct double_data_list_constraint {
    QVector<double> m_values;
    double m_min;
    double m_max;
};

using integer_data_constraint = const ::SANE_Range*;
using integer_data_list_constraint = const ::SANE_Word*;

Q_DECLARE_METATYPE(string_data_constraint)
Q_DECLARE_METATYPE(integer_data_constraint)
Q_DECLARE_METATYPE(integer_data_list_constraint)
Q_DECLARE_METATYPE(double_data_constraint)
Q_DECLARE_METATYPE(double_data_list_constraint)

/*!
   \brief The Model part of Model/View Framework representing all options available from specific device
 */
class DeviceOptionModel : public QAbstractTableModel {
    Q_OBJECT

    ScanWorker& m_worker;
    std::ranges::subrange<vg_sane::device::option_iterator> m_deviceOptionDescrs;
    bool m_updateInProgress = false;

public:
    enum Columns {
        ColumnTitle,
        ColumnValue,
        ColumnUnit,
        ColumnLast
    };

    /*!
       \brief Additional roles available for requesting from this Model
     */
    enum DeviceInfoRole {
        /*!
           \brief This role returns QVariant with optionally one of XXX_constraint types declared above
         */
        ConstraintRole = Qt::UserRole,
        /*!
           \brief This role returns QVariant with bool = true if the cell should be displayed as a button
         */
        ButtonRole = Qt::UserRole + 1
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

private:
    mutable bool m_isValid = true;

    struct StopGetOptError {};

    // Proxy methods for transferring the call into a worker thread where SANE wrapper lives
    //
    vg_sane::opt_value_t getOptionValue(const vg_sane::device::option_iterator::value_type& pos) const;
    vg_sane::device::set_opt_result_t setOptionValue(
        const vg_sane::device::option_iterator::value_type& pos, vg_sane::opt_value_t val);

private slots:
    void gotDeviceOptions(DeviceOptionsOrError);

signals:
    // private signals
    //
    void openDevice(std::string);
    void getDeviceOptions();
    void getOptionValueSig(QSemaphore*, int, vg_sane::opt_value_t*, std::exception_ptr*) const;
    void setOptionValueSig(QSemaphore*, int, vg_sane::opt_value_t*, vg_sane::device::set_opt_result_t*, std::exception_ptr*);

    // public signals
    //
    void deviceOptionsUpdated(bool, QString) const;
};
