#include "capturer.h"

#include <QEvent>
#include <QCoreApplication>

#include <QtGlobal>
#include <QtDebug>

#include <exception>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace {

constexpr auto roundUp (auto val, auto den) {
    return val / den + (val % den ? 1 : 0);
};

class ImageBuilderBase : public IImageBuilder {
public:
    virtual void feedData(std::span<const unsigned char> data) final {
        feedDataImpl(data);
        m_bytesProcessed += (int)data.size();
    }

    std::variant<int, double> getProgress() final {
        if (m_totalLinesCount > 0)
            return 100.0 * m_scanLine / m_totalLinesCount;
        return m_bytesProcessed;
    }

protected:
    ::SANE_Parameters m_scanParams;
    IImageHolder& m_imageHolder;
    int m_scanLine = 0;
    int m_bytesProcessed = 0;
    int m_totalLinesCount = -1;

    ImageBuilderBase(const ::SANE_Parameters& params, IImageHolder& imageHolder)
        : m_scanParams(params)
        , m_imageHolder(imageHolder) {
    }

    virtual void feedDataImpl(std::span<const unsigned char> data) = 0;
};

class GrayImageBuilder final : public ImageBuilderBase {
    int m_linePos = 0;

public:
    GrayImageBuilder(const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint)
        : ImageBuilderBase(params, imageHolder) {
        int width = params.pixels_per_line;
        int height = params.lines > 0 ? params.lines : heightHint;

        if (height > 0)
            m_totalLinesCount = height;
        else
            // If height is not known, let's start from square image and adjust height on the flight
            height = width;

        if (params.depth == 1) {
            QImage img(width, height, QImage::Format_Mono);
            img.setColor(0, qRgb(255, 255, 255));
            img.setColor(1, qRgb(0, 0, 0));
            img.fill(0u);
            m_imageHolder.modifier().setImage(std::move(img));
        } else if (params.depth == 8) {
            QImage img(width, height, QImage::Format_RGB32);
            img.fill(Qt::white);
            m_imageHolder.modifier().setImage(std::move(img));
        } else {
            QImage img(width, height, QImage::Format_RGBX64);
            img.fill(Qt::white);
            m_imageHolder.modifier().setImage(std::move(img));
        }
    }

private:
    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for gray image");
    }

    void feedDataImpl(std::span<const unsigned char> data) override {
        auto modifier = m_imageHolder.modifier();

        if (m_scanParams.depth == 1) {
            const auto endPos = modifier.width() / 8 + (modifier.width() % 8 ? 1 : 0);

            while (! data.empty()) {
                auto toCopyBytes = std::min(endPos - m_linePos, (int)data.size());
                std::memcpy(modifier.scanLine(m_scanLine, m_linePos * 8, toCopyBytes * 8) + m_linePos,
                    data.data(), toCopyBytes);
                data = data.subspan(toCopyBytes);
                if ((m_linePos += toCopyBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        } else if (m_scanParams.depth == 8) {
            const auto endPos = modifier.width();

            while (! data.empty()) {
                auto toProcessBytes = std::min(endPos - m_linePos, (int)data.size());
                auto destPtr = modifier.scanLine(m_scanLine, m_linePos, toProcessBytes) + m_linePos * 4;
                for (auto srcPtr = data.data(), srcEnd = data.data() + toProcessBytes; srcPtr < srcEnd; ++srcPtr) {
                    *destPtr++ = *srcPtr;
                    *destPtr++ = *srcPtr;
                    *destPtr++ = *srcPtr;
                    *destPtr++ = '\xff';
                }
                data = data.subspan(toProcessBytes);
                if ((m_linePos += toProcessBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        } else if (m_scanParams.depth == 16) {
            const auto endPos = modifier.width() * 2;

            // TODO: need to verify. My device doesn't provide data with such color depth
            while (! data.empty()) {
                auto toProcessBytes = std::min(endPos - m_linePos, (int)data.size());
                auto interPos = m_linePos % 2;
                auto destPtr = modifier.scanLine(m_scanLine, m_linePos / 2, roundUp(toProcessBytes, 2))
                        + m_linePos / 2 * 8 + interPos;
                for (auto srcPtr = data.data(), srcEnd = data.data() + toProcessBytes; srcPtr < srcEnd; ++srcPtr) {
                    *destPtr = *srcPtr;
                    *(destPtr + 2) = *srcPtr;
                    *(destPtr + 4) = *srcPtr;
                    *(destPtr + 6) = '\xff';
                    if ((interPos = (interPos + 1) % 2) == 0)
                        destPtr += 8;
                    else
                        ++destPtr;
                }
                data = data.subspan(toProcessBytes);
                if ((m_linePos += toProcessBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        }
    }

    int getFinalHeight() override {
        return m_scanLine + (m_linePos != 0 ? 1 : 0);
    }
};

class InterleavedColorImageBuilder : public ImageBuilderBase {
    int m_linePos = 0;

public:
    InterleavedColorImageBuilder(const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint)
        : ImageBuilderBase(params, imageHolder) {
        int width = params.pixels_per_line;
        int height = params.lines > 0 ? params.lines : heightHint;

        if (height > 0)
            m_totalLinesCount = height;
        else
            // If height is not known, let's start from square image and adjust height on the flight
            height = width;

        if (params.depth == 1)
            throw std::runtime_error("unsupported color depth=1 by interleaved color image builder");
        else if (params.depth == 8) {
            QImage img(width, height, QImage::Format_RGB32);
            img.fill(Qt::white);
            m_imageHolder.modifier().setImage(std::move(img));
        } else {
            QImage img(width, height, QImage::Format_RGBX64);
            img.fill(Qt::white);
            m_imageHolder.modifier().setImage(std::move(img));
        }
    }

    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for interleaved color image");
    }

    void feedDataImpl(std::span<const unsigned char> data) override {
        auto modifier = m_imageHolder.modifier();

        if (m_scanParams.depth == 8) {
            // m_linePos points to a channel inside a pixel, like [R,G,B], [R,G,B], ...
            const auto endPos = modifier.width() * 3;

            while (! data.empty()) {
                auto toProcessBytes = std::min(endPos - m_linePos, (int)data.size());
                auto interPos = m_linePos % 3;
                auto destPtr = reinterpret_cast<QRgb*>(
                    modifier.scanLine(m_scanLine, m_linePos / 3, roundUp(toProcessBytes, 3))) + m_linePos / 3;

                for (auto srcPtr = data.data(), srcEnd = data.data() + toProcessBytes; srcPtr < srcEnd;) {
                    switch (interPos) {
                    case 0: *destPtr = qRgb(*srcPtr++, 0, 0); break;
                    case 1: *destPtr = qRgb(qRed(*destPtr), *srcPtr++, 0); break;
                    case 2: *destPtr = qRgb(qRed(*destPtr), qGreen(*destPtr), *srcPtr++); ++destPtr; break;
                    }
                    interPos = (interPos + 1) % 3;
                }
                data = data.subspan(toProcessBytes);
                if ((m_linePos += toProcessBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        } else if (m_scanParams.depth == 16) {
            // m_linePos points to a channel inside a pixel, like [R16,G16,B16], [R16,G16,B16], ...
            const auto endPos = modifier.width() * 6;

            while (! data.empty()) {
                // TODO: need to verify. My device doesn't provide data with such color depth
                auto toProcessBytes = std::min(endPos - m_linePos, (int)data.size());
                auto interPos = m_linePos % 6;
                auto destPtr = reinterpret_cast<quint64*>(
                    modifier.scanLine(m_scanLine, m_linePos / 6, roundUp(toProcessBytes, 6))) + m_linePos / 6;

                for (auto srcPtr = data.data(), srcEnd = data.data() + toProcessBytes; srcPtr < srcEnd;) {
                    const auto destRgba = QRgba64::fromRgba64(*destPtr);
                    switch (interPos) {
                    case 0: *destPtr = QRgba64::fromRgba64(*srcPtr++, 0, 0, 0); break;
                    case 1: *destPtr = QRgba64::fromRgba64((*srcPtr++ << 8) | destRgba.red(), 0, 0, 0); break;
                    case 2: *destPtr = QRgba64::fromRgba64(destRgba.red(), *srcPtr++, 0, 0); break;
                    case 3: *destPtr = QRgba64::fromRgba64(destRgba.red(), (*srcPtr++ << 8) | destRgba.green(), 0, 0); break;
                    case 4: *destPtr = QRgba64::fromRgba64(destRgba.red(), destRgba.green(), *srcPtr++, 0); break;
                    case 5: *destPtr = QRgba64::fromRgba64(destRgba.red(), destRgba.green(), (*srcPtr++ << 8) | destRgba.blue(), 0); ++destPtr; break;
                    }
                    interPos = (interPos + 1) % 6;
                }
                data = data.subspan(toProcessBytes);
                if ((m_linePos += toProcessBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        }
    }

    int getFinalHeight() override {
        return m_scanLine + (m_linePos != 0 ? 1 : 0);
    }
};

QScopedPointer<IImageBuilder> createBuilder(
    const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint) {
    if (params.depth != 1 && params.depth != 8 && params.depth != 16)
        throw std::runtime_error("unsupported image depth " + std::to_string(params.depth)
            + " bits per pixel");

    if (params.format == SANE_FRAME_GRAY)
        return QScopedPointer<IImageBuilder>(new GrayImageBuilder(params, imageHolder, heightHint));
    else if (params.format == SANE_FRAME_RGB)
        return QScopedPointer<IImageBuilder>(new InterleavedColorImageBuilder(params, imageHolder, heightHint));

    throw std::runtime_error("unable to decode image with unknown format id="
        + std::to_string(params.format));
}

} // ns anonymous

//--------------------------------------------------------------------------------------------------
Capturer::Capturer(vg_sane::device& device, IImageHolder& imageHolder, QObject *parent)
    : QObject{parent}
    , m_scannerDevice{device}
    , m_imageHolder{imageHolder} {
}

Capturer::~Capturer() = default;

bool Capturer::event(QEvent* ev) {
    if (ev->type() != QEvent::User)
        return QObject::event(ev);

    if (m_isWaitingForScanningParameters)
        processScanningParameters();
    else
        processImageData();

    return true;
}

template<typename F, typename ...Args>
void Capturer::wrappedCall(F&& f, QString msg, Args&& ... args) {
    try {
        std::forward<F>(f)(std::forward<Args>(args) ...);
    } catch (const std::exception& e) {
        emit finished(false, tr("%1:\n%2").arg(msg, QString::fromLocal8Bit(e.what())));
    } catch (...) {
        emit finished(false, tr("%1; no additional info").arg(msg));
    }
}

void Capturer::start(int lineCountHint) {
    m_isCancelRequested = false;
    m_lineCountHint = lineCountHint;
    startInner();
}

void Capturer::startInner() {
    m_isWaitingForScanningParameters = true;
    wrappedCall(
        [this](){
            m_scannerDevice.start_scanning(
                [this](){ QCoreApplication::postEvent(this, new QEvent(QEvent::User)); });
        },
        tr("Can't start scanning on device \"%1\"")
            .arg(QString::fromLocal8Bit(m_scannerDevice.name().c_str()))
    );
}

void Capturer::processScanningParameters() {
    const ::SANE_Parameters* scanParams = {};
    wrappedCall(
        [this, &scanParams](){
            scanParams = m_scannerDevice.get_scanning_parameters();
        },
        tr("Can't get actual image scanning parameters")
    );

    // It's ok that event can came while parameters still have not arrived
    if (! scanParams)
        return;

    m_isWaitingForScanningParameters = false;
    m_isLastFrame = scanParams->last_frame == SANE_TRUE;

    qDebug() << "Capturer got new frame:"
        << "\n  format:" << scanParams->format
        << "\n  last_frame:" << scanParams->last_frame
        << "\n  bytes_per_line:" << scanParams->bytes_per_line
        << "\n  pixels_per_line:" << scanParams->pixels_per_line
        << "\n  lines:" << scanParams->lines
        << "\n  depth:" << scanParams->depth;

    try {
        if (! m_imageBuilder) {
            auto p = createBuilder(*scanParams, m_imageHolder, m_lineCountHint);
            m_imageBuilder.swap(p);
        } else
            m_imageBuilder->newFrame(*scanParams);

        auto prgs = m_imageBuilder->getProgress();
        if (auto p = get_if<double>(&prgs))
            emit progress(*p);
        else
            emit progress(get<int>(prgs));
    } catch (...) {
        m_lastError = std::current_exception();
        m_lastErrorContext = tr("Can't accept new image frame: %1");
        m_scannerDevice.cancel_scanning(s_cancelScanningMode);
    }
}

void Capturer::processImageData() {
    std::vector<unsigned char> chunk;
    wrappedCall(
        [this, &chunk](){
            chunk = m_scannerDevice.get_scanning_data();
        },
        tr("Can't get another captured image data")
    );

    if (chunk.empty()) {
        if (m_isCancelRequested) {
            emit finished(false, tr("Operation cancelled"));
        } else if (m_lastError) {
            try {
               std::rethrow_exception(m_lastError);
            } catch (const std::exception& e) {
                emit finished(false, m_lastErrorContext.arg(QString::fromLocal8Bit(e.what())));
            } catch (...) {
                emit finished(false, m_lastErrorContext.arg(tr("<no data>")));
            }
        } else {
            if (m_isLastFrame) {
                // The image can grow vertically during feed scanning data but at the end its height
                // should be right amount of processed scanned lines.
                m_imageHolder.modifier().setHeight(m_imageBuilder->getFinalHeight());
                emit finished(true, {});
            } else
                startInner();
        }
    } else if (! m_isCancelRequested && ! m_lastError) {
        m_imageBuilder->feedData({chunk.begin(), chunk.end()});

        auto prgs = m_imageBuilder->getProgress();
        if (auto p = get_if<double>(&prgs))
            emit progress(*p);
        else
            emit progress(get<int>(prgs));
    }
}

void Capturer::cancel() {
    m_isCancelRequested = true;
    m_scannerDevice.cancel_scanning(s_cancelScanningMode);
}
