#pragma once

#include "drawingsurface.h"

#include <sane_wrapper.h>

#include <QObject>
#include <QString>
#include <QVariant>

#include <exception>
#include <variant>

/*!
 * \brief An abstract interface for image builders supporting various image formats
 */
struct IImageBuilder {
    virtual ~IImageBuilder() = default;
    /*!
     * \brief prepares building for accepting data for the new frame described by specified
     *        parameters
     *
     * The method can be called a few times if an image is built from a few frames (determined by
     * specific builder. Can throw an exception if the new frame is unexpected or parameters are
     * unexpected.
     */
    virtual void newFrame(const ::SANE_Parameters& params) = 0;
    virtual void feedData(std::span<const unsigned char> data) = 0;
    virtual int getFinalHeight() = 0;

    /*!
     * \brief get progress of current image building
     * \return either double value denoting percents or integer denoting bytes processed when no
     *         information about image size
     */
    virtual std::variant<int, double> getProgress() = 0;
};

/*!
 * \brief The image Capturer handing state machine for capturing exactly one image
 *
 * The object is created just for getting one image from a scanner. It lives in main/GUI thread.
 */
class Capturer : public QObject
{
    Q_OBJECT
public:
    explicit Capturer(vg_sane::device& device, IImageHolder& imageHolder, QObject *parent = nullptr);
    ~Capturer();

private:
    static constexpr auto s_cancelScanningMode = vg_sane::device::cancel_mode::safe;

    vg_sane::device& m_scannerDevice;
    IImageHolder& m_imageHolder;
    std::unique_ptr<IImageBuilder> m_imageBuilder;
    std::exception_ptr m_lastError;
    QString m_lastErrorContext;
    int m_lineCountHint;
    bool m_isWaitingForScanningParameters;
    bool m_isLastFrame;
    bool m_isCancelRequested;

    template<typename F, typename ...Args>
    void wrappedCall(F&& f, QString msg, Args&& ... args);

    bool event(QEvent* ev) override;

    void startInner();
    void processScanningParameters();
    void processImageData();

public slots:
    void start(int);
    void cancel();

signals:
    void finished(bool, QString);
    void progress(QVariant);
};
