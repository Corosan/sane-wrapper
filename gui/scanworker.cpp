#include "scanworker.h"

#include <QGuiApplication>
#include <QPalette>
#include <QLocale>
#include <QBrush>
#include <QtMath>

#include <cstring>
#include <algorithm>

DeviceListModel::DeviceListModel(vg_sane::lib::ptr_t lib_ptr, QObject* parent)
    : QAbstractListModel(parent)
    , m_lib{std::move(lib_ptr)} {
}

void DeviceListModel::update() {
    beginResetModel();
    try {
        m_scannerInfos = m_lib->get_device_infos();
    }  catch (...) {
        m_scannerInfos = {};
        endResetModel();
        throw;
    }
    endResetModel();
}

int DeviceListModel::rowCount(const QModelIndex &parent) const {
    return parent == QModelIndex() ? m_scannerInfos.size() : 0;
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex() || index.column() != 0)
        return {};

    const auto infoPtr = m_scannerInfos[index.row()];

    switch (role)
    {
    case Qt::DisplayRole:
        return QString::fromLocal8Bit(infoPtr->name);
    case DeviceTypeRole:
        return QString::fromLocal8Bit(infoPtr->type);
    case DeviceModelRole:
        return QString::fromLocal8Bit(infoPtr->model);
    case DeviceVendorRole:
        return QString::fromLocal8Bit(infoPtr->vendor);
    default:
        return {};
    }
}

vg_sane::device DeviceListModel::openDevice(int index) const {
    return m_lib->open_device(m_scannerInfos[index]->name);
}

//--------------------------------------------------------------------------------------------------
bool DeviceOptionModel::s_typesRegistered = false;

DeviceOptionModel::DeviceOptionModel(vg_sane::device& device, QObject* parent)
    : QAbstractTableModel(parent)
    , m_device{device} {
    if (! s_typesRegistered) {
        qRegisterMetaType<string_data_constraint>();
        qRegisterMetaType<integer_data_constraint>();
        qRegisterMetaType<integer_data_list_constraint>();
        qRegisterMetaType<double_data_constraint>();
        qRegisterMetaType<double_data_list_constraint>();
        s_typesRegistered = true;
    }

    m_optionDescriptors = m_device.get_option_infos();
}

int DeviceOptionModel::rowCount(const QModelIndex &parent) const {
    return parent == QModelIndex() ? m_optionDescriptors.size() : 0;
}

Qt::ItemFlags DeviceOptionModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);

    if (index.parent() != QModelIndex())
        return flags;

    if (! m_isEnabled) {
        flags &= ~Qt::ItemIsEnabled;
        return flags;
    }

    const auto [_, descrPtr] = m_optionDescriptors[index.row()];

    // Tricky case is a 'button' type which is actually not a cell with some data for editing.
    // There is no worth in having editable cell for buttons, but real GUI objects - buttons should
    // be displayed there instead.
    if (SANE_OPTION_IS_ACTIVE(descrPtr->cap) != SANE_TRUE)
        flags &= ~Qt::ItemIsEnabled;
    else if (index.column() == ColumnValue
             && SANE_OPTION_IS_SETTABLE(descrPtr->cap) == SANE_TRUE
             && descrPtr->type != SANE_TYPE_BUTTON)
        flags |= Qt::ItemIsEditable;

    return flags;
}

void DeviceOptionModel::enable(bool val) {
    if (val != m_isEnabled) {
        m_isEnabled = val;
        beginResetModel();
        endResetModel();
    }
}

inline double saneFixedToDouble(int val) {
    return static_cast<double>(val) / (1 << SANE_FIXED_SCALE_SHIFT);
}

inline ::SANE_Fixed doubleToSaneFixed(double val) {
    return static_cast<::SANE_Fixed>(val * (1 << SANE_FIXED_SCALE_SHIFT));
}

QVariant DeviceOptionModel::getConstraint(const ::SANE_Option_Descriptor& descr) const {
    switch (descr.type) {
    case SANE_TYPE_FIXED:
        // FIXED type is converted into double for editing purposes as long as there is no decimal boxes
        // in Qt. It would be better to subclass one of QxxxSpinBox classes to provide such a support when
        // I have a time.
        if (descr.constraint_type == 0)
            return QVariant::fromValue(double_data_constraint{-32768., 32767.9999, .0});
        else if (descr.constraint_type == SANE_CONSTRAINT_RANGE)
            return QVariant::fromValue(double_data_constraint{
                saneFixedToDouble(descr.constraint.range->min),
                saneFixedToDouble(descr.constraint.range->max),
                saneFixedToDouble(descr.constraint.range->quant)});
        else if (descr.constraint_type == SANE_CONSTRAINT_WORD_LIST) {
            auto src = descr.constraint.word_list + 1;
            QVector<double> dest;
            for (auto count = *(src - 1); count > 0; --count, ++src)
                dest.push_back(saneFixedToDouble(*src));
            return QVariant::fromValue(double_data_list_constraint{dest, -32768., 32767.9999});
        }
        break;
    case SANE_TYPE_INT:
        if (descr.constraint_type == SANE_CONSTRAINT_RANGE)
            return QVariant::fromValue(descr.constraint.range);
        else if (descr.constraint_type == SANE_CONSTRAINT_WORD_LIST)
            return QVariant::fromValue(descr.constraint.word_list);
        break;
    case SANE_TYPE_STRING:
    {
        string_data_constraint c{static_cast<std::size_t>(descr.size) - 1};
        if (descr.constraint_type == SANE_CONSTRAINT_STRING_LIST) {
            // TODO: can be cached also
            for (auto p = descr.constraint.string_list; *p != nullptr; ++p)
                c.m_values.push_back(QString::fromLocal8Bit(*p));
        }
        return QVariant::fromValue(std::move(c));
    }
    default:
        break;
    }

    return {};
}

QVariant DeviceOptionModel::getTooltip(const ::SANE_Option_Descriptor& descr) const {
    switch (descr.type) {
    case SANE_TYPE_FIXED:
        if (descr.constraint_type == 0)
            return tr("min value: -32768, max value: 32767.9999");
        else if (descr.constraint_type == SANE_CONSTRAINT_RANGE)
            return tr("min value: %1, max value: %2")
                    .arg(saneFixedToDouble(descr.constraint.range->min))
                    .arg(saneFixedToDouble(descr.constraint.range->max))
                + (descr.constraint.range->quant != 0
                    ? tr(", step: %1").arg(saneFixedToDouble(descr.constraint.range->quant))
                    : QString{});
        else if (descr.constraint_type == SANE_CONSTRAINT_WORD_LIST) {
            auto src = descr.constraint.word_list + 1;
            QString res = tr("Valid values: ");
            if (*(src - 1) == 0)
                res += "[]";
            else {
                const int maxNumbers = 10;
                res += QLocale().toString(saneFixedToDouble(*src++));
                for (int i = 0, count = std::min(*(src - 2) - 1, maxNumbers); i < count; ++i, ++src)
                    res += ", " + QLocale().toString(saneFixedToDouble(*src));
                if (*descr.constraint.word_list > maxNumbers)
                    res += ", ...";
            }
            return res;
        }
        break;
    case SANE_TYPE_INT:
        if (descr.constraint_type == SANE_CONSTRAINT_RANGE)
            return tr("min value: %1, max value: %2")
                    .arg(descr.constraint.range->min)
                    .arg(descr.constraint.range->max)
                + (descr.constraint.range->quant != 0
                    ? tr(", step: %1").arg(descr.constraint.range->quant)
                    : QString{});
        else if (descr.constraint_type == SANE_CONSTRAINT_WORD_LIST) {
            auto src = descr.constraint.word_list + 1;
            QString res = tr("Valid values: ");
            if (*(src - 1) == 0)
                res += "[]";
            else {
                const int maxNumbers = 10;
                res += QLocale().toString(*src++);
                for (int i = 0, count = std::min(*(src - 2) - 1, maxNumbers); i < count; ++i, ++src)
                    res += ", " + QLocale().toString(*src);
                if (*descr.constraint.word_list > maxNumbers)
                    res += ", ...";
            }
            return res;
        }
        break;
    default:
        break;
    }

    return {};
}

QVariant DeviceOptionModel::getValue(const QModelIndex &index, int role) const {
    const auto [optInd, descrPtr] = m_optionDescriptors[index.row()];

    switch (descrPtr->type) {
    case SANE_TYPE_FIXED:
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            // One fixed will be editable as [double] but more than one - as a text - list of numbers
            // separated by comma so far for simplicity. Still didn't decide what to do with constraints
            // for list of fixeds
            auto val = std::get<2>(m_device.get_option(optInd));
            if (val.size() == 1) {
                auto dval = saneFixedToDouble(*val.data());
                return role == Qt::DisplayRole ? QVariant(QLocale().toString(dval)) : QVariant(dval);
            }
            return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                    return l + (l.isEmpty() ? "" : "; ") + QLocale().toString(saneFixedToDouble(r));
                });
        }
        break;
    case SANE_TYPE_INT:
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            // One integer will be editable as [integer] but more than one - as a text - list of numbers
            // separated by comma so far for simplicity. Still didn't decide what to do with constraints
            // for list of integers
            auto val = std::get<2>(m_device.get_option(optInd));
            if (val.size() == 1 && role == Qt::EditRole)
                return *val.begin();
            return std::accumulate(val.begin(), val.end(), QString{}, [](const auto& l, const auto& r){
                    return l + (l.isEmpty() ? "" : "; ") + QLocale().toString(r);
                });
        }
        break;
    case SANE_TYPE_STRING:
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return QString::fromLocal8Bit(std::get<3>(m_device.get_option(optInd)));
        break;
    case SANE_TYPE_BOOL:
        if (role == Qt::CheckStateRole)
            return std::get<1>(m_device.get_option(optInd)).get() ? Qt::Checked : Qt::Unchecked;
        else if (role == Qt::EditRole)
            return static_cast<bool>(std::get<1>(m_device.get_option(optInd)));
        break;
    case SANE_TYPE_BUTTON:
        if (role == ButtonRole)
            return true;
        break;
    case SANE_TYPE_GROUP:
        break;
    default:
        if (role == Qt::DisplayRole)
            return QString("<unsupported_type:%1>").arg(descrPtr->type);
        break;
    }

    return {};
}

QVariant DeviceOptionModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex())
        return {};

    const auto [_, descrPtr] = m_optionDescriptors[index.row()];

    if (role == Qt::BackgroundRole && descrPtr->type == SANE_TYPE_GROUP) {
        return QGuiApplication::palette().brush(QPalette::Midlight);
    }

    switch (index.column()) {
    case ColumnTitle:
        if (role == Qt::DisplayRole)
            return QString::fromLocal8Bit(descrPtr->title ? descrPtr->title : descrPtr->name);
        else if (role == Qt::ToolTipRole)
            return QString::fromLocal8Bit(descrPtr->desc ? descrPtr->desc : "");
        break;
    case ColumnValue:
        if (role == ConstraintRole)
            return getConstraint(*descrPtr);
        else if (role == Qt::ToolTipRole)
            return getTooltip(*descrPtr);
        else {
            if (m_isEnabled) {
                auto res = getValue(index, role);
                if (role == Qt::DisplayRole) {
                    if (m_cachedValues.size() <= index.row())
                        m_cachedValues.resize(index.row() + 1);
                    m_cachedValues[index.row()] = res;
                }
                return res;
            } else
                return role == Qt::DisplayRole && m_cachedValues.size() > index.row()
                    ? m_cachedValues[index.row()] : QVariant{};
        }
        break;
    case ColumnUnit:
        if (role == Qt::DisplayRole)
            return
                descrPtr->unit == SANE_UNIT_PIXEL ? tr("px")
                : descrPtr->unit == SANE_UNIT_BIT ? tr("bit")
                : descrPtr->unit == SANE_UNIT_MM ? tr("mm")
                : descrPtr->unit == SANE_UNIT_DPI ? tr("dpi")
                : descrPtr->unit == SANE_UNIT_PERCENT ? QString::fromLatin1("%")
                : descrPtr->unit == SANE_UNIT_MICROSECOND ? tr("us")
                : QString();
        break;
    }

    return {};
}

bool DeviceOptionModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (index == QModelIndex() || index.parent() != QModelIndex() || index.column() != ColumnValue
        || role != Qt::EditRole)
        return false;

    bool res = false;
    vg_sane::device::set_opt_result_t opRes;
    const auto [optInd, descrPtr] = m_optionDescriptors[index.row()];

    try {
        switch (descrPtr->type) {
        case SANE_TYPE_FIXED:
            if (descrPtr->size == sizeof(::SANE_Fixed)) {
                res = true;
                auto val = doubleToSaneFixed(value.toDouble());
                opRes = m_device.set_option(optInd, vg_sane::opt_value_t{std::span{&val, 1}});
            } else {
                std::vector<::SANE_Fixed> vals;
                for (const auto& s : value.toString().split(";", Qt::SkipEmptyParts))
                    vals.push_back(doubleToSaneFixed(QLocale().toDouble(s.trimmed())));
                // If lesser values were provided than expected, others are filled with minimum
                // constraint value
                vals.resize(descrPtr->size / sizeof(::SANE_Fixed),
                    descrPtr->constraint_type == SANE_CONSTRAINT_RANGE ?
                        descrPtr->constraint.range->min :
                    (descrPtr->constraint_type == SANE_CONSTRAINT_WORD_LIST
                     && descrPtr->constraint.word_list[0] > 0) ?
                        descrPtr->constraint.word_list[1] : 0);
                res = true;
                opRes = m_device.set_option(optInd, vg_sane::opt_value_t{std::span{vals}});
            }
            break;
        case SANE_TYPE_INT:
            if (descrPtr->size == sizeof(::SANE_Word)) {
                res = true;
                auto val = static_cast<::SANE_Word>(value.toInt());
                opRes = m_device.set_option(optInd, vg_sane::opt_value_t{std::span{&val, 1}});
            } else {
                std::vector<::SANE_Word> vals;
                for (const auto& s : value.toString().split(";", Qt::SkipEmptyParts))
                    vals.push_back(QLocale().toInt(s.trimmed()));
                vals.resize(descrPtr->size / sizeof(::SANE_Word),
                    descrPtr->constraint_type == SANE_CONSTRAINT_RANGE ?
                        descrPtr->constraint.range->min :
                    (descrPtr->constraint_type == SANE_CONSTRAINT_WORD_LIST
                      && descrPtr->constraint.word_list[0] > 0) ?
                        descrPtr->constraint.word_list[1] : 0);
                res = true;
                opRes = m_device.set_option(optInd, vg_sane::opt_value_t{std::span{vals}});
            }
            break;
        case SANE_TYPE_STRING:
            res = true;
            opRes = m_device.set_option(optInd,
                vg_sane::opt_value_t{value.toString().toLocal8Bit().data()});
            break;
        case SANE_TYPE_BOOL:
        {
            res = true;
            ::SANE_Bool val = value.toBool() ? SANE_TRUE : SANE_FALSE;
            opRes = m_device.set_option(optInd, vg_sane::opt_value_t{val});
            break;
        }
        case SANE_TYPE_BUTTON:
            res = true;
            opRes = m_device.set_option(optInd, {});
            break;
        default:
            break;
        }
    } catch (const std::exception& e) {
        emit error(QString::fromLocal8Bit(e.what()));
        opRes = static_cast<unsigned long long>(vg_sane::device::set_opt_result_flags::reload_opts);
        res = false;
    } catch (...) {
        emit error("<no data>");
        opRes = static_cast<unsigned long long>(vg_sane::device::set_opt_result_flags::reload_opts);
        res = false;
    }

    // Is some special processing for "inexact value" case needed? Or Qt anyway reloads data into
    // a view from a model after editing...?
    if (opRes.test(static_cast<std::size_t>(vg_sane::device::set_opt_result_flags::reload_opts))) {
        beginResetModel();
        bool callEnd = true;
        try {
            m_optionDescriptors = m_device.get_option_infos();
        } catch (const std::exception& e) {
            m_optionDescriptors = {};
            endResetModel();
            callEnd = false;
            emit error(QString::fromLocal8Bit(e.what()));
        } catch (...) {
            m_optionDescriptors = {};
            endResetModel();
            callEnd = false;
            emit error("<no data>");
        }

        if (callEnd)
            endResetModel();
    }
    else if (res) {
        emit dataChanged(index, index);
    }

    return res;
}

QRect DeviceOptionModel::getScanAreaPx() const {
    double resolutionDpi = -1.0;
    double tl_x, tl_y, br_x, br_y;
    int axisUnit;
    bool axisUnitSet = false;
    int pointsFound = 0;

    auto setOptTo = [this](const ::SANE_Option_Descriptor* descrPtr, auto optInd, auto& dest){
        if (auto val = std::get<2>(m_device.get_option(optInd)); val.size() == 1) {
            if (descrPtr->type == SANE_TYPE_INT)
                dest = *val.begin();
            else if (descrPtr->type == SANE_TYPE_FIXED)
                dest = saneFixedToDouble(*val.begin());
            else
                return false;
        }
        return false;
    };

    for (const auto [optInd, descrPtr] : m_optionDescriptors) {
        if (descrPtr->unit == SANE_UNIT_DPI && std::strcmp(descrPtr->name, "resolution") == 0) {
            setOptTo(descrPtr, optInd, resolutionDpi);
            continue;
        }

        if (descrPtr->unit == SANE_UNIT_PIXEL || descrPtr->unit == SANE_UNIT_MM) {
            int b = 1;
            for (auto [name, dest] : {std::make_pair("tl-x", &tl_x), std::make_pair("tl-y", &tl_y),
                        std::make_pair("br-x", &br_x), std::make_pair("br-y", &br_y)}) {
                if (std::strcmp(descrPtr->name, name) == 0) {
                    if (! axisUnitSet) {
                        axisUnit = true;
                        axisUnit = descrPtr->unit;
                    }
                    if (axisUnit != descrPtr->unit)
                        continue;

                    if (setOptTo(descrPtr, optInd, *dest))
                        pointsFound |= b;
                    break;
                }
                b <<= 1;
            }
        }
    }

    auto mmToPx = [resolutionDpi](auto val){ return val * resolutionDpi / 25.4; };

    if (pointsFound == 0b1111) {
        if (axisUnit == SANE_UNIT_PIXEL)
            return QRect(tl_x, tl_y, br_x - tl_x, br_y - tl_y);
        else if (resolutionDpi > 0.0)
            return QRect(qFloor(mmToPx(tl_x)), qFloor(mmToPx(tl_y)),
                qCeil(mmToPx(br_x - tl_x)), qCeil(mmToPx(br_y - tl_y)));
    }
    return {};
}
