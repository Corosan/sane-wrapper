#include "scanworker.h"

#include <QLocale>

#include <exception>
#include <numeric>

ScanWorker::ScanWorker()
    : m_saneLib(vg_sane::lib::instance()) {
    qRegisterMetaType<DeviceInfosOrError>();
    qRegisterMetaType<DeviceOptionsOrError>();
    qRegisterMetaType<std::string>();
}

void ScanWorker::getDeviceInfos() {
    try {
        emit gotDeviceInfos(m_saneLib->get_device_infos());
    } catch (...) {
        emit gotDeviceInfos(std::current_exception());
    }
}

void ScanWorker::openDevice(std::string name) {
    try {
        // Nullify old device because the new one can have the same name and no two devices
        // with the same name can exist in the program
        m_currentDevice = {};
        m_currentDevice = m_saneLib->open_device(name.c_str());
        emit gotDeviceOptions(m_currentDevice.get_option_infos());
    } catch (...) {
        emit gotDeviceOptions(std::current_exception());
    }
}

void ScanWorker::getDeviceOptions() {
    try {
        emit gotDeviceOptions(m_currentDevice.get_option_infos());
    }  catch (...) {
        emit gotDeviceOptions(std::current_exception());
    }
}

void ScanWorker::getOptionValue(QSemaphore* lock, int pos,
    vg_sane::opt_value_t* val, std::exception_ptr* excPtr) const {
    try {
        *val = m_currentDevice.get_option(pos);
    } catch (...) {
        *excPtr = std::current_exception();
    }

    lock->release();
}

void ScanWorker::setOptionValue(QSemaphore* lock, int pos,
    vg_sane::opt_value_t* val, vg_sane::device::set_opt_result_t* opt_status, std::exception_ptr* excPtr) {
    try {
        *opt_status = m_currentDevice.set_option(pos, *val);
    } catch (...) {
        *excPtr = std::current_exception();
    }

    lock->release();
}


DeviceListModel::DeviceListModel(ScanWorker& worker, QObject* parent)
    : QAbstractListModel(parent) {
    Q_ASSERT(connect(this, &DeviceListModel::getDeviceInfos, &worker, &ScanWorker::getDeviceInfos));
    Q_ASSERT(connect(&worker, &ScanWorker::gotDeviceInfos, this, &DeviceListModel::gotDeviceInfos));
}

void DeviceListModel::update() {
    beginResetModel();
    m_infos.clear();
    endResetModel();
    m_updateInProgress = true;
    emit getDeviceInfos();
}

void DeviceListModel::gotDeviceInfos(DeviceInfosOrError result) {
    m_updateInProgress = false;
    beginResetModel();
    if (auto exc_pp = get_if<std::exception_ptr>(&result)) {
        m_infos.clear();
        endResetModel();
        try {
            std::rethrow_exception(*exc_pp);
        } catch (const std::exception& e) {
            emit updateFinished(false,
                tr("Unexpected error on getting devices list:\n%1").arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            emit updateFinished(false, tr("Unknown error on getting devices list"));
        }
        return;
    }

    for (const auto& v : get<0>(result)) {
        m_infos.append({
            QString::fromLocal8Bit(v->name),
            QString::fromLocal8Bit(v->type),
            QString::fromLocal8Bit(v->model),
            QString::fromLocal8Bit(v->vendor)});
    }
    endResetModel();
    emit updateFinished(true, {});
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
    qRegisterMetaType<string_data_constraint>();
    qRegisterMetaType<integer_data_constraint>();
    qRegisterMetaType<integer_data_list_constraint>();
    qRegisterMetaType<double_data_constraint>();
    qRegisterMetaType<double_data_list_constraint>();

    Q_ASSERT(connect(this, &DeviceOptionModel::openDevice, &worker, &ScanWorker::openDevice));
    Q_ASSERT(connect(this, &DeviceOptionModel::getDeviceOptions, &worker, &ScanWorker::getDeviceOptions));
    Q_ASSERT(connect(this, &DeviceOptionModel::getOptionValueSig, &worker, &ScanWorker::getOptionValue));
    Q_ASSERT(connect(this, &DeviceOptionModel::setOptionValueSig, &worker, &ScanWorker::setOptionValue));
    Q_ASSERT(connect(&worker, &ScanWorker::gotDeviceOptions, this, &DeviceOptionModel::gotDeviceOptions));

    m_updateInProgress = true;
    emit openDevice(name.toStdString());
}

void DeviceOptionModel::gotDeviceOptions(DeviceOptionsOrError result) {
    m_updateInProgress = false;
    beginResetModel();
    if (auto exc_pp = get_if<std::exception_ptr>(&result)) {
        m_deviceOptionDescrs = {};
        m_isValid = false;
        endResetModel();
        try {
            std::rethrow_exception(*exc_pp);
        } catch (const std::exception& e) {
            emit deviceOptionsUpdated(false,
                tr("Unexpected error on getting device options list:\n%1").arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            emit deviceOptionsUpdated(false, tr("Unknown error on getting device options list"));
        }
        return;
    }

    m_deviceOptionDescrs = get<0>(result);
    m_isValid = true;
    endResetModel();
    emit deviceOptionsUpdated(true, {});
}

vg_sane::opt_value_t DeviceOptionModel::getOptionValue(
        const vg_sane::device::option_iterator::value_type& pos) const {
    QSemaphore s;
    vg_sane::opt_value_t val;
    std::exception_ptr excPtr;
    emit getOptionValueSig(&s, pos.first, &val, &excPtr);
    s.acquire();
    if (excPtr) {
        auto propName = QString::fromLocal8Bit(pos.second->title ? pos.second->title : pos.second->name);
        try {
            std::rethrow_exception(excPtr);
        } catch (const std::exception& e) {
            m_isValid = false;
            emit deviceOptionsUpdated(false,
                tr("Unexpected error on getting an option \"%1\" (stop reading others):\n%2")
                    .arg(propName, QString::fromLocal8Bit(e.what())));
        } catch (...) {
            m_isValid = false;
            emit deviceOptionsUpdated(false,
                tr("Unknown error on getting an option \"%1\" (stop reading others)")
                    .arg(propName));
        }
        throw StopGetOptError{};
    }
    return val;
}
vg_sane::device::set_opt_result_t DeviceOptionModel::setOptionValue(
        const vg_sane::device::option_iterator::value_type& pos, vg_sane::opt_value_t val) {
    QSemaphore s;
    vg_sane::device::set_opt_result_t status;
    std::exception_ptr excPtr;
    emit setOptionValueSig(&s, pos.first, &val, &status, &excPtr);
    s.acquire();
    if (excPtr) {
        auto propName = QString::fromLocal8Bit(pos.second->title ? pos.second->title : pos.second->name);
        try {
            std::rethrow_exception(excPtr);
        } catch (const std::exception& e) {
            emit deviceOptionsUpdated(false,
                tr("Unexpected error on setting an option \"%1\":\n%2")
                    .arg(propName, QString::fromLocal8Bit(e.what())));
        } catch (...) {
            emit deviceOptionsUpdated(false,
                tr("Unknown error on setting an option \"%1\"")
                    .arg(propName));
        }
        // We didn't expect to get any error on setting an option, but it has happened. So,
        // maybe re-reading options will fix the error.
        return SANE_INFO_RELOAD_OPTIONS;
    }
    return status;
}

Qt::ItemFlags DeviceOptionModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);

    if (index.parent() != QModelIndex())
        return flags;

    const auto& descr = m_deviceOptionDescrs[index.row()];

    // Tricky case is a 'button' type which is actually not a cell with some data for editing.
    // There is no worth in having editable cell for buttons, but real GUI objects - buttons should
    // be displayed there instead.
    if (SANE_OPTION_IS_ACTIVE(descr.second->cap) != SANE_TRUE || ! m_isValid)
        flags &= ~Qt::ItemIsEnabled;
    else if (index.column() == ColumnValue
             && SANE_OPTION_IS_SETTABLE(descr.second->cap) == SANE_TRUE
             && descr.second->type != SANE_TYPE_BUTTON)
        flags |= Qt::ItemIsEditable;

    return flags;
}

QVariant DeviceOptionModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex())
        return {};

    const auto& descr = m_deviceOptionDescrs[index.row()];

    try {
        switch (index.column()) {
        case ColumnTitle:
            if (role == Qt::DisplayRole)
                return QString::fromLocal8Bit(descr.second->title ? descr.second->title : descr.second->name);
            else if (role == Qt::ToolTipRole)
                return QString::fromLocal8Bit(descr.second->desc ? descr.second->desc : "");
            break;
        case ColumnValue:
            if (m_isValid) {
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
                    } else if (role == Qt::DisplayRole || role == Qt::EditRole) {
                        // One fixed will be editable as [double] but more than one - as a text - list of numbers
                        // separated by comma so far for simplicity. Still didn't decide what to do with constraints
                        // for list of fixeds
                        auto val = std::get<2>(getOptionValue(descr));
                        if (val.size() == 1) {
                            auto dval = static_cast<double>(*val.data()) / (1 << SANE_FIXED_SCALE_SHIFT);
                            return role == Qt::DisplayRole ? QVariant(QLocale().toString(dval)) : QVariant(dval);
                        }
                        return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                                return l + (l.isEmpty() ? "" : "; ") +
                                    QLocale().toString(static_cast<double>(r) / (1 << SANE_FIXED_SCALE_SHIFT));
                            });
                    } else if (role == Qt::ToolTipRole) {
                        if (descr.second->constraint_type == 0)
                            return tr("min value: -32768, max value: 32767.9999");
                        else if (descr.second->constraint_type == SANE_CONSTRAINT_RANGE)
                            return tr("min value: %1, max value: %2")
                                    .arg(static_cast<double>(descr.second->constraint.range->min) / (1 << SANE_FIXED_SCALE_SHIFT))
                                    .arg(static_cast<double>(descr.second->constraint.range->max) / (1 << SANE_FIXED_SCALE_SHIFT))
                                + (descr.second->constraint.range->quant != 0
                                    ? tr(", step: %1").arg(static_cast<double>(descr.second->constraint.range->quant) / (1 << SANE_FIXED_SCALE_SHIFT))
                                    : QString{});
                    }
                    break;
                case SANE_TYPE_INT:
                    if (role == ConstraintRole) {
                        if (descr.second->constraint_type == SANE_CONSTRAINT_RANGE)
                            return QVariant::fromValue(descr.second->constraint.range);
                        else if (descr.second->constraint_type == SANE_CONSTRAINT_WORD_LIST)
                            return QVariant::fromValue(descr.second->constraint.word_list);
                    } else if (role == Qt::DisplayRole || role == Qt::EditRole) {
                        // One integer will be editable as [integer] but more than one - as a text - list of numbers
                        // separated by comma so far for simplicity. Still didn't decide what to do with constraints
                        // for list of integers
                        auto val = std::get<2>(getOptionValue(descr));
                        if (val.size() == 1 && role == Qt::EditRole)
                            return *val.begin();
                        return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                                return l + (l.isEmpty() ? "" : "; ") + QLocale().toString(r);
                            });
                    } else if (role == Qt::ToolTipRole) {
                        if (descr.second->constraint_type == SANE_CONSTRAINT_RANGE)
                            return tr("min value: %1, max value: %2")
                                    .arg(descr.second->constraint.range->min)
                                    .arg(descr.second->constraint.range->max)
                                + (descr.second->constraint.range->quant != 0
                                    ? tr(", step: %1").arg(descr.second->constraint.range->quant)
                                    : QString{});
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
                    } else if (role == Qt::DisplayRole || role == Qt::EditRole)
                        return QString::fromLocal8Bit(std::get<3>(getOptionValue(descr)));
                    break;
                case SANE_TYPE_BOOL:
                    if (role == Qt::CheckStateRole)
                        return get<1>(getOptionValue(descr)).get() ? Qt::Checked : Qt::Unchecked;
                    else if (role == Qt::EditRole)
                        return static_cast<bool>(get<1>(getOptionValue(descr)));
                    break;
                case SANE_TYPE_GROUP:
                    return {};  // no special highlighting for a separator between groups though
                                // some fancy line could be displayed instead
                case SANE_TYPE_BUTTON:
                    if (role == ButtonRole)
                        return true;
                    return {};
                default:
                    if (role == Qt::DisplayRole)
                        return QString("<unsupported_type:%1>").arg(descr.second->type);
                    break;
                }
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
    } catch (const StopGetOptError&) {
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
                auto val = static_cast<::SANE_Fixed>(value.toDouble() * (1 << SANE_FIXED_SCALE_SHIFT));
                opRes = setOptionValue(descr, vg_sane::opt_value_t{std::span{&val, 1}});
            } else {
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
                opRes = setOptionValue(descr, vg_sane::opt_value_t{std::span{vals}});
            }
        }
        break;
    case SANE_TYPE_INT:
        if (role == Qt::EditRole) {
            if (descr.second->size == sizeof(::SANE_Word)) {
                res = true;
                auto val = static_cast<::SANE_Word>(value.toInt());
                opRes = setOptionValue(descr, vg_sane::opt_value_t{std::span{&val, 1}});
            } else {
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
                opRes = setOptionValue(descr, vg_sane::opt_value_t{std::span{vals}});
            }
        }
        break;
    case SANE_TYPE_STRING:
        if (role == Qt::EditRole) {
            res = true;
            opRes = setOptionValue(descr,
                vg_sane::opt_value_t{value.toString().toLocal8Bit().data()});
        }
        break;
    case SANE_TYPE_BOOL:
        if (role == Qt::EditRole) {
            res = true;
            ::SANE_Bool val = value.toBool() ? SANE_TRUE : SANE_FALSE;
            opRes = setOptionValue(descr, vg_sane::opt_value_t{val});
        }
        break;
    case SANE_TYPE_BUTTON:
        if (role == Qt::EditRole) {
            res = true;
            opRes = setOptionValue(descr, {});
        }
    default:
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
