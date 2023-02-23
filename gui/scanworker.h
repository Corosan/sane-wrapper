#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QRect>

#include <sane_wrapper.h>

/*!
 * \brief The The Model part of Model/View Framework representing all available devices connected to
 *        the machine this program is running on.
 *
 * The class can generate exceptions from non-Qt methods which request data from underlying SANE
 * wrapper library.
 */
class DeviceListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum DeviceInfoRole {
        DeviceTypeRole = Qt::UserRole,
        DeviceModelRole = Qt::UserRole + 1,
        DeviceVendorRole = Qt::UserRole + 2
    };

    explicit DeviceListModel(vg_sane::lib::ptr_t lib_ptr, QObject* parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * @brief update a list of known devices
     *
     * The model doesn't re-check devices connected to the box at background. This method should be
     * called to do this.
     */
    void update();
    vg_sane::device openDevice(int index) const;

private:
    vg_sane::lib::ptr_t m_lib;

    // This is a range of device infos actually stored in a SANE library's memory. The range
    // is valid until the library is not unloaded and until new range is requested from it.
    vg_sane::devices_t m_scannerInfos;
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
 * \brief The Model part of Model/View Framework representing all options available from specific
 * scanner device
 *
 * The model can be in enabled and disabled state. Disabled model just shows options in 'disabled'
 * GUI state not allowing to change them. Used when the scanner actually makes its job.
 */
class DeviceOptionModel : public QAbstractTableModel {
    Q_OBJECT

    static bool s_typesRegistered;

public:
    enum Columns {
        ColumnTitle,
        ColumnValue,
        ColumnUnit,
        ColumnLast
    };

    /*!
     * \brief Additional roles available for requesting from this Model
     */
    enum DeviceInfoRole {
        /*!
         * \brief This role returns QVariant with optionally one of XXX_constraint types declared above
         */
        ConstraintRole = Qt::UserRole,
        /*!
         * \brief This role returns QVariant with bool = true if the cell should be displayed as a button
         */
        ButtonRole = Qt::UserRole + 1
    };

    /*!
     * \brief Create the model to be linked with specified device
     *
     * The device should be alive all the lifetime of this model object. All access/modification
     * operations are considered short-time so executed synchronously.
     */
    explicit DeviceOptionModel(vg_sane::device& device, QObject* parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override {
        return parent == QModelIndex() ? ColumnLast : 0;
    }

    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return section == ColumnTitle ? tr("Property")
                : section == ColumnValue ? tr("Value")
                : section == ColumnUnit ? tr("Unit") : QString();
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    /*!
     * \brief enables or disables this model thus making it editable / non-editable
     */
    void enable(bool val = true);

    /*!
     * \brief calculates position of scanned area in a scanner pixels based on well-known
     *        properties of a scanner like DPI.
     *
     * Assumed to be called after all options have been read via standard data() accessor.
     */
    QRect getScanAreaPx() const;

private:
    vg_sane::device& m_device;
    vg_sane::device_options_t m_optionDescriptors;
    bool m_isEnabled = true;
    mutable QVector<QVariant> m_cachedValues;

    QVariant getConstraint(const ::SANE_Option_Descriptor& descr) const;
    QVariant getTooltip(const ::SANE_Option_Descriptor& descr) const;
    QVariant getValue(const QModelIndex &index, int role) const;

signals:
    // Amaizing but Qt designers don't care that a model can refuse setting new data for instance
    // during some internal error. Let's add additional signal saying about a error if something
    // went wrong.
    void error(QString);
};
