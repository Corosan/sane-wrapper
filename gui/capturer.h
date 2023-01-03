#pragma once

#include "scanworker.h"

#include <QObject>
#include <QString>
#include <QImage>
#include <QRect>

#include <memory>

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
    virtual QRect feedData(std::span<const char> data) = 0;
    virtual const QImage& getImage() const = 0;
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
    explicit Capturer(ScanWorker& scanWorker, QObject *parent = nullptr);
    ~Capturer();

private:
    static constexpr unsigned s_defaultReadAmount = 1024;

    ScanWorker& m_scanWorker;
    std::unique_ptr<IImageBuilder> m_imageBuilder;
    bool m_isLastFrame;

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
    void pieceOfUpdate(const QImage*, QRect);
    void finished(bool, const QImage*, QString);

    // private signals
    //
    void startSig();
    void cancelSig();
    void readSig(unsigned);
};
