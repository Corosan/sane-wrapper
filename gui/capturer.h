#pragma once

#include "scanworker.h"
#include "drawingsurface.h"

#include <QObject>
#include <QString>
#include <QImage>
#include <QRect>

#include <memory>

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
    virtual unsigned short getFinalHeight() = 0;
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
    explicit Capturer(ScanWorker& scanWorker, IImageHolder& imageHolder, QObject *parent = nullptr);
    ~Capturer();

private:
    static constexpr unsigned s_defaultReadAmount = 1024*1024;

    ScanWorker& m_scanWorker;
    IImageHolder& m_imageHolder;
    std::unique_ptr<IImageBuilder> m_imageBuilder;
    bool m_isLastFrame;
    bool m_cancellingStarted;

    template<typename F, typename ...Args>
    void wrappedCall(F&& f, QString msg, Args&& ... args);

private slots:
    void scanningStartedOrNot(ScanParametersOrError);
    void gotScanningData(ScanningDataOrError);

public slots:
    void start();
    void abort();

signals:
    // Public signals
    //
    void finished(bool, QString);

    // private signals
    //
    void startSig();
    void cancelSig();
    void readSig(unsigned);
};
