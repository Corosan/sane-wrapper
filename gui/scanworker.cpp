#include "scanworker.h"

#include <QLocale>

#include <exception>
#include <numeric>

ScanWorker::ScanWorker()
    : m_saneLib(vg_sane::lib::instance()) {
    qRegisterMetaType<vg_sane::device_infos_t>();
    qRegisterMetaType<vg_sane::device_opts_t>();
    qRegisterMetaType<std::string>();
    qRegisterMetaType<string_data_constraint>();
    qRegisterMetaType<integer_data_constraint>();
    qRegisterMetaType<integer_data_list_constraint>();
    qRegisterMetaType<double_data_constraint>();
    qRegisterMetaType<double_data_list_constraint>();
}

void ScanWorker::getDeviceInfos() {
    try {
        emit gotDeviceInfos(m_saneLib->get_device_infos());
    } catch (const std::exception& e) {
        emit errorHappened(e.what());
    }
}

void ScanWorker::openDevice(std::string name) {
    try {
        // Nullify old device because the new one can have the same name and no two devices
        // with the same name can exist in the program
        m_currentDevice = {};
        m_currentDevice = m_saneLib->open_device(name.c_str());
        emit gotDeviceOptions(m_currentDevice.get_option_infos());
    } catch (const std::exception& e) {
        emit errorHappened(e.what());
    }
}

void ScanWorker::getDeviceOptions() {
    try {
        emit gotDeviceOptions(m_currentDevice.get_option_infos());
    }  catch (const std::exception& e) {
        emit errorHappened(e.what());
    }
}

vg_sane::opt_value_t ScanWorker::getOptionValue(int pos) const {
    return m_currentDevice.get_option(pos);
}

vg_sane::device::set_opt_result_t ScanWorker::setOptionValue(int pos, vg_sane::opt_value_t val) {
    return m_currentDevice.set_option(pos, val);
}

DeviceListModel::DeviceListModel(ScanWorker& worker, QObject* parent)
    : QAbstractListModel(parent) {
    connect(this, &DeviceListModel::getDeviceInfos, &worker, &ScanWorker::getDeviceInfos);
    connect(&worker, &ScanWorker::gotDeviceInfos, this, &DeviceListModel::gotDeviceInfos);
    QObject::connect(&worker, &ScanWorker::errorHappened, this, &DeviceListModel::gotError);
}

void DeviceListModel::update() {
    beginResetModel();
    m_infos.clear();
    endResetModel();
    m_updateInProgress = true;
    emit getDeviceInfos();
}

void DeviceListModel::gotDeviceInfos(vg_sane::device_infos_t val) {
    m_updateInProgress = false;
    beginResetModel();
    for (const auto& v : val) {
        m_infos.append({
            QString::fromLocal8Bit(v->name),
            QString::fromLocal8Bit(v->type),
            QString::fromLocal8Bit(v->model),
            QString::fromLocal8Bit(v->vendor)});
    }
    endResetModel();
    emit updateFinished(true);
}

void DeviceListModel::gotError(std::string s) {
    if (m_updateInProgress) {
        m_updateInProgress = false;
        emit updateFinished(false);
    }
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex() || index.column() != 0)
        return {};

    switch (role)
    {
    case Qt::DisplayRole:
        return m_infos[index.row()].m_name;
    case DeviceTypeRole:
        return m_infos[index.row()].m_type;
    case DeviceModelRole:
        return m_infos[index.row()].m_model;
    case DeviceVendorRole:
        return m_infos[index.row()].m_vendor;
    default:
        return {};
    }
}

DeviceOptionModel::DeviceOptionModel(ScanWorker& worker, QString name, QObject* parent)
    : QAbstractTableModel(parent)
    , m_worker(worker) {
    connect(this, &DeviceOptionModel::openDevice, &worker, &ScanWorker::openDevice);
    connect(this, &DeviceOptionModel::getDeviceOptions, &worker, &ScanWorker::getDeviceOptions);
    connect(&worker, &ScanWorker::gotDeviceOptions, this, &DeviceOptionModel::gotDeviceOptions);
    connect(&worker, &ScanWorker::errorHappened, this, &DeviceOptionModel::gotError);

    m_updateInProgress = true;
    emit openDevice(name.toStdString());
}

void DeviceOptionModel::gotDeviceOptions(vg_sane::device_opts_t val) {
    m_updateInProgress = false;
    beginResetModel();
    m_deviceOptionDescrs = val;
    endResetModel();
    emit deviceOptionsUpdated();
}

void DeviceOptionModel::gotError(std::string s) {
    m_updateInProgress = false;
}

Qt::ItemFlags DeviceOptionModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);

    if (index.parent() != QModelIndex())
        return flags;

    const auto& descr = m_deviceOptionDescrs[index.row()];

    if (SANE_OPTION_IS_ACTIVE(descr.second->cap) != SANE_TRUE)
        flags &= ~Qt::ItemIsEnabled;
    else if (index.column() == ColumnValue && SANE_OPTION_IS_SETTABLE(descr.second->cap) == SANE_TRUE)
        flags |= Qt::ItemIsEditable;

    return flags;
}

QVariant DeviceOptionModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex())
        return {};

    const auto& descr = m_deviceOptionDescrs[index.row()];

    switch (index.column()) {
    case ColumnTitle:
        if (role == Qt::DisplayRole)
            return QString::fromLocal8Bit(descr.second->title ? descr.second->title : descr.second->name);
        else if (role == Qt::ToolTipRole)
            return QString::fromLocal8Bit(descr.second->desc ? descr.second->desc : "");
        break;
    case ColumnValue:
        switch (descr.second->type) {
        case SANE_TYPE_FIXED:
            // FIXED type is converted into double for editing purposes as long as there is no decimal boxes
            // in Qt. It would be better to subclass one of QxxxSpinBox classes to provide such a support when
            // we have a time.
            if (role == ConstraintRole) {
                if (descr.second->constraint_type == 0)
                    return QVariant::fromValue(double_data_constraint{-32768., 32767.9999, .0});
                else if (descr.second->constraint_type == SANE_CONSTRAINT_RANGE)
                    return QVariant::fromValue(double_data_constraint{
                        static_cast<double>(descr.second->constraint.range->min) / (1 << SANE_FIXED_SCALE_SHIFT),
                        static_cast<double>(descr.second->constraint.range->max) / (1 << SANE_FIXED_SCALE_SHIFT),
                        static_cast<double>(descr.second->constraint.range->quant) / (1 << SANE_FIXED_SCALE_SHIFT)});
                else if (descr.second->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
                    auto src = descr.second->constraint.word_list + 1;
                    QVector<double> dest;
                    for (auto count = *(src - 1); count > 0; --count, ++src)
                        dest.push_back(static_cast<double>(*src) / (1 << SANE_FIXED_SCALE_SHIFT));
                    return QVariant::fromValue(double_data_list_constraint{dest, -32768., 32767.9999});
                }
            }
            else if (role == Qt::DisplayRole || role == Qt::EditRole) {
                // One fixed will be editable as [double] but more than one - as a text - list of numbers
                // separated by comma so far for simplicity. Still didn't decide what to do with constraints
                // for list of fixeds
                auto val = std::get<2>(m_worker.getOptionValue(descr.first));
                if (val.size() == 1) {
                    auto dval = static_cast<double>(*val.data()) / (1 << SANE_FIXED_SCALE_SHIFT);
                    return role == Qt::DisplayRole ? QVariant(QLocale().toString(dval)) : QVariant(dval);
                }
                return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                        return l + (l.isEmpty() ? "" : "; ") +
                            QLocale().toString(static_cast<double>(r) / (1 << SANE_FIXED_SCALE_SHIFT));
                    });
            }
            break;
        case SANE_TYPE_INT:
            if (role == ConstraintRole) {
                if (descr.second->constraint_type == SANE_CONSTRAINT_RANGE)
                    return QVariant::fromValue(descr.second->constraint.range);
                else if (descr.second->constraint_type == SANE_CONSTRAINT_WORD_LIST)
                    return QVariant::fromValue(descr.second->constraint.word_list);
            }
            else if (role == Qt::DisplayRole || role == Qt::EditRole) {
                // One integer will be editable as [integer] but more than one - as a text - list of numbers
                // separated by comma so far for simplicity. Still didn't decide what to do with constraints
                // for list of integers
                auto val = std::get<2>(m_worker.getOptionValue(descr.first));
                return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                        return l + (l.isEmpty() ? "" : "; ") + QLocale().toString(r);
                    });
            }
            break;
        case SANE_TYPE_STRING:
            if (role == ConstraintRole) {
                string_data_constraint c{static_cast<std::size_t>(descr.second->size) - 1};
                if (descr.second->constraint_type == SANE_CONSTRAINT_STRING_LIST) {
                    // TODO: can be cached also
                    for (auto p = descr.second->constraint.string_list; *p != nullptr; ++p)
                        c.m_values.push_back(QString::fromLocal8Bit(*p));
                }
                return QVariant::fromValue(std::move(c));
            }
            else if (role == Qt::DisplayRole || role == Qt::EditRole)
                return QString::fromLocal8Bit(std::get<3>(m_worker.getOptionValue(descr.first)));
            break;
        case SANE_TYPE_BOOL:
            if (role == Qt::CheckStateRole)
                return get<1>(m_worker.getOptionValue(descr.first)).get() ? Qt::Checked : Qt::Unchecked;
            else if (role == Qt::EditRole)
                return static_cast<bool>(get<1>(m_worker.getOptionValue(descr.first)));
            break;
        default:
            if (role == Qt::DisplayRole)
                return QString("<unsupported_type:%1>").arg(descr.second->type);
            break;
        }
        break;
    case ColumnUnit:
        if (role == Qt::DisplayRole)
            return
                descr.second->unit == SANE_UNIT_PIXEL ? tr("px")
                : descr.second->unit == SANE_UNIT_BIT ? tr("bit")
                : descr.second->unit == SANE_UNIT_MM ? tr("mm")
                : descr.second->unit == SANE_UNIT_DPI ? tr("dpi")
                : descr.second->unit == SANE_UNIT_PERCENT ? QString::fromLatin1("%")
                : descr.second->unit == SANE_UNIT_MICROSECOND ? tr("us")
                : QString();
        break;
    }
    return {};
}

bool DeviceOptionModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (index == QModelIndex() || index.parent() != QModelIndex() || index.column() != ColumnValue)
        return false;

    bool res = false;
    vg_sane::device::set_opt_result_t opRes;
    const auto& descr = m_deviceOptionDescrs[index.row()];

    switch (descr.second->type) {
    case SANE_TYPE_FIXED:
        if (role == Qt::EditRole) {
            if (descr.second->size == sizeof(::SANE_Fixed)) {
                res = true;
                auto val = static_cast<::SANE_Fixed>(QLocale().toDouble(value.toString()) * (1 << SANE_FIXED_SCALE_SHIFT));
                opRes = m_worker.setOptionValue(descr.first, vg_sane::opt_value_t{std::span{&val, 1}});
            }
            else {
                std::vector<::SANE_Fixed> vals;
                for (const auto& s : value.toString().split(";", Qt::SkipEmptyParts))
                    vals.push_back(static_cast<::SANE_Fixed>(QLocale().toDouble(s.trimmed()) * (1 << SANE_FIXED_SCALE_SHIFT)));
                vals.resize(descr.second->size / sizeof(::SANE_Fixed),
                    descr.second->constraint_type == SANE_CONSTRAINT_RANGE ?
                        descr.second->constraint.range->min :
                    (descr.second->constraint_type == SANE_CONSTRAINT_WORD_LIST
                     && descr.second->constraint.word_list[0] > 0) ?
                        descr.second->constraint.word_list[1] : 0);
                res = true;
                opRes = m_worker.setOptionValue(descr.first, vg_sane::opt_value_t{std::span{vals}});
            }
        }
        break;
    case SANE_TYPE_INT:
        if (role == Qt::EditRole) {
            if (descr.second->size == sizeof(::SANE_Word)) {
                res = true;
                auto val = static_cast<::SANE_Word>(QLocale().toInt(value.toString()));
                opRes = m_worker.setOptionValue(descr.first, vg_sane::opt_value_t{std::span{&val, 1}});
            }
            else {
                std::vector<::SANE_Word> vals;
                for (const auto& s : value.toString().split(";", Qt::SkipEmptyParts))
                    vals.push_back(QLocale().toInt(s.trimmed()));
                vals.resize(descr.second->size / sizeof(::SANE_Word),
                    descr.second->constraint_type == SANE_CONSTRAINT_RANGE ?
                        descr.second->constraint.range->min :
                    (descr.second->constraint_type == SANE_CONSTRAINT_WORD_LIST
                      && descr.second->constraint.word_list[0] > 0) ?
                        descr.second->constraint.word_list[1] : 0);
                res = true;
                opRes = m_worker.setOptionValue(descr.first, vg_sane::opt_value_t{std::span{vals}});
            }
        }
        break;
    case SANE_TYPE_STRING:
        if (role == Qt::EditRole) {
            res = true;
            opRes = m_worker.setOptionValue(descr.first,
                vg_sane::opt_value_t{value.toString().toLocal8Bit().data()});
        }
        break;
    case SANE_TYPE_BOOL:
        if (role == Qt::EditRole) {
            res = true;
            ::SANE_Bool val = value.toBool() ? SANE_TRUE : SANE_FALSE;
            opRes = m_worker.setOptionValue(descr.first, vg_sane::opt_value_t{val});
        }
        break;
    }

    // Is some special processing for "inexact value" case needed? Or Qt anyway reloads data into
    // a view from a model after editing...?
    if (opRes.test(static_cast<std::size_t>(vg_sane::device::set_opt_result_flags::reload_opts))) {
        m_updateInProgress = true;
        beginResetModel();
        m_deviceOptionDescrs = {};
        endResetModel();
        emit getDeviceOptions();
    }
    else if (res) {
        emit dataChanged(index, index);
    }

    return res;
}
