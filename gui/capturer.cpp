#include "capturer.h"

#include <QCoreApplication>
#include <QEvent>

#include <exception>
#include <cstring>
#include <stdexcept>
#include <algorithm>

class GrayImageBuilder : public IImageBuilder {
    ::SANE_Parameters m_scanParams;
    IImageHolder& m_imageHolder;
    int m_scanLine = 0;
    int m_linePos = 0;

public:
    GrayImageBuilder(const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint)
        : m_scanParams(params)
        , m_imageHolder(imageHolder) {
        int width = params.pixels_per_line;
        // If height is not known, let's start from square image and adjust height on the flight
        int height = params.lines > 0 ? params.lines : heightHint > 0 ? heightHint : width;

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
        } else if (params.depth == 16) {
            QImage img(width, height, QImage::Format_RGBX64);
            img.fill(Qt::white);
            m_imageHolder.modifier().setImage(std::move(img));
        }
    }

    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for gray image");
    }

    void feedData(std::span<const unsigned char> data) override {
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
            const auto endPos = modifier.width();

            while (! data.empty()) {
                auto toProcessBytes = std::min(endPos - m_linePos, (int)data.size());
                auto destPtr = modifier.scanLine(m_scanLine, m_linePos, toProcessBytes) + m_linePos * 8;
                for (auto srcPtr = data.data(), srcEnd = data.data() + toProcessBytes; srcPtr < srcEnd; ++srcPtr) {
                    *destPtr++ = *srcPtr;
                    *destPtr++ = *(srcPtr+1);
                    *destPtr++ = *srcPtr;
                    *destPtr++ = *(srcPtr+1);
                    *destPtr++ = *srcPtr;
                    *destPtr++ = *(srcPtr+1);
                    *destPtr++ = '\xff';
                    *destPtr++ = '\xff';
                }
                data = data.subspan(toProcessBytes);
                if ((m_linePos += toProcessBytes) == endPos) {
                    m_linePos = 0;
                    ++m_scanLine;
                }
            }
        }
    }

    unsigned short getFinalHeight() override {
        return m_scanLine + (m_linePos != 0 ? 1 : 0);
    }
};

class InterleavedColorImageBuilder : public IImageBuilder {
    ::SANE_Parameters m_scanParams;
    IImageHolder& m_imageHolder;
    int m_scanLine = 0;
    int m_linePos = 0;

public:
    InterleavedColorImageBuilder(const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint)
        : m_scanParams(params)
        , m_imageHolder(imageHolder) {

    }

    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for interleaved color image");
    }

    void feedData(std::span<const unsigned char> data) override {
    }

    unsigned short getFinalHeight() override {
        return m_scanLine + (m_linePos != 0 ? 1 : 0);
    }
};

QScopedPointer<IImageBuilder> createBuilder(
    const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint = -1) {
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

void Capturer::start() {
    m_isCancelRequested = false;
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

    try {
        if (! m_imageBuilder) {
            auto p = createBuilder(*scanParams, m_imageHolder);
            m_imageBuilder.swap(p);
        } else
            m_imageBuilder->newFrame(*scanParams);
    } catch (...) {
        m_lastError = std::current_exception();
        m_lastErrorContext = tr("Can't accept new image frame: %1");
        m_scannerDevice.cancel_scanning(s_useFastCancelling);
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
    }
}

void Capturer::cancel() {
    m_isCancelRequested = true;
    m_scannerDevice.cancel_scanning(s_useFastCancelling);
}
