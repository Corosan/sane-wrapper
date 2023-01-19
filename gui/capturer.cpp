#include "capturer.h"

#include <QtGlobal>
#include <QtDebug>

#include <exception>
#include <cstring>
#include <memory>
#include <variant>
#include <stdexcept>
#include <string>
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

        //m_image = QImage(width, height,
        //    params.depth == 1 ? QImage::Format_Mono :
        //    params.depth == 8 ? QImage::Format_Grayscale8 :
        //        QImage::Format_Grayscale16);
        if (params.depth == 1) {
            QImage img(width, height, QImage::Format_Mono);
            img.setColor(0, qRgb(255, 255, 255));
            img.setColor(1, qRgb(0, 0, 0));
            img.fill(0u);
            m_imageHolder.modifier().setImage(std::move(img));
        } else
            throw std::runtime_error("GrayImageBuilder doesn't support "
                + std::to_string(params.depth) + " bits-per-pixel depth");
    }

    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for gray image");
    }

    void feedData(std::span<const unsigned char> data) override {
        auto modifier = m_imageHolder.modifier();
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
    }

    unsigned short getFinalHeight() override {
        return m_scanLine + (m_linePos != 0 ? 1 : 0);
    }
};

std::unique_ptr<IImageBuilder> createBuilder(
    const ::SANE_Parameters& params, IImageHolder& imageHolder, int heightHint = -1) {
    if (params.depth != 1 && params.depth != 8 && params.depth != 16)
        throw std::runtime_error("unsupported image depth " + std::to_string(params.depth)
            + " bits per pixel");

    if (params.format == SANE_FRAME_GRAY)
        return std::make_unique<GrayImageBuilder>(params, imageHolder, heightHint);

    throw std::runtime_error("unable to decode image with unknown format id="
        + std::to_string(params.format));
}

//--------------------------------------------------------------------------------------------------
Capturer::Capturer(ScanWorker& scanWorker, IImageHolder& imageHolder, QObject *parent)
    : QObject{parent}
    , m_scanWorker{scanWorker}
    , m_imageHolder{imageHolder} {
    Q_ASSERT(connect(this, &Capturer::startSig, &m_scanWorker, &ScanWorker::startScanning));
    Q_ASSERT(connect(this, &Capturer::readSig, &m_scanWorker, &ScanWorker::readScanningData));
    // Note that cancelling is not supported from another thread unfortunatelly
    // Q_ASSERT(connect(this, &Capturer::cancelSig, &m_scanWorker, &ScanWorker::cancelScanning, Qt::DirectConnection));
    Q_ASSERT(connect(this, &Capturer::cancelSig, &m_scanWorker, &ScanWorker::cancelScanning));
    Q_ASSERT(connect(&m_scanWorker, &ScanWorker::gotScanningData, this, &Capturer::gotScanningData));
    Q_ASSERT(connect(&m_scanWorker, &ScanWorker::scanningStartedOrNot, this, &Capturer::scanningStartedOrNot));
}

Capturer::~Capturer() = default;

void Capturer::start() {
    m_cancellingStarted = false;
    qDebug() << "capturer initiates start scanning";
    emit startSig();
    qDebug() << "capturer initiated start scanning";
}

template<typename F, typename ...Args>
void Capturer::wrappedCall(F&& f, QString msg, Args&& ... args) {
    try {
        std::forward<F>(f)(std::forward<Args>(args) ...);
    } catch (const std::exception& e) {
        emit cancelSig();
        emit finished(false, tr("%1:\n%2").arg(msg, QString::fromLocal8Bit(e.what())));
    } catch (...) {
        emit cancelSig();
        emit finished(false, tr("%1 - unknown error").arg(msg));
    }
}

void Capturer::scanningStartedOrNot(ScanParametersOrError v) {
    if (auto e = get_if<std::exception_ptr>(&v); e) {
        wrappedCall([](auto e){ std::rethrow_exception(*e); },
            tr("Unable to start or continue scanning"), e);

        return;
    }

    m_isLastFrame = get<::SANE_Parameters>(v).last_frame == SANE_TRUE;

    wrappedCall([this, &v](){
            qDebug() << "capturer creates image builder";
            if (! m_imageBuilder)
                m_imageBuilder = createBuilder(get<::SANE_Parameters>(v), m_imageHolder);
            else
                m_imageBuilder->newFrame(get<::SANE_Parameters>(v));

            qDebug() << "capturer initiates reading of" << s_defaultReadAmount << "bytes";
            emit readSig(s_defaultReadAmount);
            qDebug() << "capturer initiated reading of" << s_defaultReadAmount << "bytes";
        },
        tr("Unable to start or continue capturing of scanning data"));
}

void Capturer::gotScanningData(ScanningDataOrError v) {
    if (auto e = get_if<std::exception_ptr>(&v); e) {
        wrappedCall([](auto e){ std::rethrow_exception(*e); },
            tr("Unexpected error during getting scanning data"), e);

        return;
    }

    const auto& buffer = get<std::vector<unsigned char>>(v);

    if (buffer.empty()) {
        if (m_cancellingStarted) {
            emit finished(false, tr("Operation cancelled"));
            return;
        }

        emit cancelSig();   // strange but the cancellation must be called even for normally finished
                            // operation
        if (m_isLastFrame) {
            // The image can grow vertically during feed scanning data but at the end its height
            // should be right amount of processed scanned lines.
            m_imageHolder.modifier().setHeight(m_imageBuilder->getFinalHeight());
            emit finished(true, {});
        } else {
            emit startSig();
        }
    } else {
        wrappedCall([this, &buffer](){
                m_imageBuilder->feedData({buffer.begin(), buffer.end()});
                qDebug() << "capturer initiates reading of" << s_defaultReadAmount << "bytes";
                emit readSig(s_defaultReadAmount);
                qDebug() << "capturer initiated reading of" << s_defaultReadAmount << "bytes";
            },
            tr("Unable to feed data from a scanner"));
    }
}

void Capturer::abort() {
    m_cancellingStarted = true;
    qDebug() << "capturer cancels current operation";
    emit cancelSig();
}
