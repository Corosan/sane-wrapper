#include "capturer.h"

#include <QtGlobal>

#include <exception>
#include <memory>
#include <variant>
#include <stdexcept>
#include <string>
#include <algorithm>

class GrayImageBuilder : public IImageBuilder {
    ::SANE_Parameters m_scanParams;
    QImage m_image;
    unsigned short m_scanLine = 0;
    unsigned short m_linePos = 0;

public:
    GrayImageBuilder(const ::SANE_Parameters& params, int heightHint)
        : m_scanParams(params) {
        int width = params.pixels_per_line;
        // If height is not known, let's start from square image and adjust height on the flight
        int height = params.lines > 0 ? params.lines : heightHint > 0 ? heightHint : width;

        //m_image = QImage(width, height,
        //    params.depth == 1 ? QImage::Format_Mono :
        //    params.depth == 8 ? QImage::Format_Grayscale8 :
        //        QImage::Format_Grayscale16);
        if (params.depth == 1) {
            m_image = QImage(width, height, QImage::Format_Mono);
            m_image.setColor(0, qRgb(255, 255, 255));
            m_image.setColor(1, qRgb(0, 0, 0));
        } else
            throw std::runtime_error("GrayImageBuilder doesn't support "
                + std::to_string(params.depth) + " bits-per-pixel depth");
    }

    void newFrame(const ::SANE_Parameters& params) override {
        throw std::runtime_error("unexpected new frame for gray image");
    }

    const QImage& getImage() const override { return m_image; }

    QRect feedData(std::span<const char> data) override {
        const auto endPos = m_image.width() / 8 + (m_image.width() % 8 ? 1 : 0);
        QRect res;

        while (! data.empty()) {
            auto toCopyBytes = std::min(endPos - m_linePos, (int)data.size());
            std::memcpy(m_image.scanLine(m_scanLine), data.data(), toCopyBytes);
            data = data.subspan(toCopyBytes);
            res |= QRect(m_linePos * 8, m_scanLine, toCopyBytes * 8, m_scanLine);
            m_linePos = (unsigned short)(m_linePos + toCopyBytes);
            if (m_linePos == (unsigned short)endPos) {
                m_linePos = 0;
                if (++m_scanLine == m_image.height() && ! data.empty())
                    throw std::runtime_error("unexpected "
                        + std::to_string(data.size()) + " trailing bytes");
            }
        }

        return res;
    }
};

std::unique_ptr<IImageBuilder> createBuilder(const ::SANE_Parameters& params, int heightHint = -1) {
    if (params.depth != 1 && params.depth != 8 && params.depth != 16)
        throw std::runtime_error("unsupported image depth " + std::to_string(params.depth)
            + " bits per pixel");

    if (params.format == SANE_FRAME_GRAY)
        std::make_unique<GrayImageBuilder>(params, heightHint);

    throw std::runtime_error("unable to decode image from unknown format id="
        + std::to_string(params.format));
}

//--------------------------------------------------------------------------------------------------
Capturer::Capturer(ScanWorker& scanWorker, QObject *parent)
    : QObject{parent}
    , m_scanWorker{scanWorker} {
    Q_ASSERT(connect(this, &Capturer::startSig, &m_scanWorker, &ScanWorker::startScanning));
    Q_ASSERT(connect(this, &Capturer::readSig, &m_scanWorker, &ScanWorker::readScanningData));
    Q_ASSERT(connect(this, &Capturer::cancelSig, &m_scanWorker, &ScanWorker::cancelScanning, Qt::DirectConnection));
    Q_ASSERT(connect(&m_scanWorker, &ScanWorker::gotScanningData, this, &Capturer::gotScanningData));
    Q_ASSERT(connect(&m_scanWorker, &ScanWorker::scanningStartedOrNot, this, &Capturer::scanningStartedOrNot));
}

Capturer::~Capturer() = default;

void Capturer::start() {
    emit startSig();
}

template<typename F, typename ...Args>
void Capturer::wrappedCall(F&& f, QString msg, Args&& ... args) {
    try {
        std::forward<F>(f)(std::forward<Args>(args) ...);
    } catch (const std::exception& e) {
        emit cancelSig();
        emit finished(false, nullptr, tr("%1:\n%2").arg(msg, QString::fromLocal8Bit(e.what())));
    } catch (...) {
        emit cancelSig();
        emit finished(false, nullptr, tr("%1 - unknown error").arg(msg));
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
            if (! m_imageBuilder)
                m_imageBuilder = createBuilder(get<::SANE_Parameters>(v));
            else
                m_imageBuilder->newFrame(get<::SANE_Parameters>(v));

            emit readSig(s_defaultReadAmount);
        },
        tr("Unable to start or continue capturing of scanning data"));
}

void Capturer::gotScanningData(ScanningDataOrError v) {
    if (auto e = get_if<std::exception_ptr>(&v); e) {
        wrappedCall([](auto e){ std::rethrow_exception(*e); },
            tr("Unexpected error during getting scanning data"), e);

        return;
    }

    const auto& buffer = get<std::vector<char>>(v);

    if (buffer.empty()) {
        emit cancelSig();   // strange but the cancellation must be called even for normally finished
                            // operation
        if (m_isLastFrame) {
            emit finished(true, &m_imageBuilder->getImage(), {});
        } else {
            emit startSig();
        }
    } else {
        wrappedCall([this, &buffer](){
                QRect updateRect = m_imageBuilder->feedData({buffer.begin(), buffer.end()});
                emit pieceOfUpdate(&m_imageBuilder->getImage(), updateRect);
            },
            tr("Unable to feed data from a scanner"));
    }
}

void Capturer::abort() {
    emit cancelSig();
}
