#include "mainwindow.h"

#include <sane_wrapper.h>

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QMessageBox>
#include <QSettings>
#include <QLatin1String>

#include <QtGlobal>
#include <QtDebug>

#include <exception>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // This is recommended way to initialize locale-dependend translation. It works successfully
    // only when my translation file doesn't include country part (_RU), only a language. It does
    // so because my LANGUAGE environment variable contains as the first element the language "ru"
    // without a country code: echo $LANGUAGE => "ru:en_US". I don't know yet is it right from OS
    // internationalization point of view. I would say that "ru" translation could be the same
    // for any country having "ru" people. But... the second item there "en_US" leads to a thought
    // that it can be not so in general.
    QTranslator translator;
    if (translator.load(QLocale(), QLatin1String("gui"), QLatin1String("_"), QLatin1String(":/i18n")))
        app.installTranslator(&translator);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    app.setOrganizationName(QLatin1String("SG_House"));
    app.setApplicationName(QLatin1String("sane-wrapper-gui"));

    vg_sane::lib::ptr_t saneLibWrapperPtr;

    try {
        saneLibWrapperPtr = vg_sane::lib::instance();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, app.applicationName(),
            QApplication::translate("ScannerApp", "Unable to initialize SANE lib: %1")
                .arg(QString::fromLocal8Bit(e.what())));
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, app.applicationName(),
            QApplication::translate("ScannerApp", "Unable to initialize SANE lib with unknown error"));
        return 1;
    }

    saneLibWrapperPtr->set_logger_sink([](vg_sane::LogLevel sev, std::string_view msg) {
        switch (sev) {
        case vg_sane::LogLevel::Debug:
            qDebug() << std::string{msg}.c_str();
            break;
        case vg_sane::LogLevel::Info:
            qInfo() << std::string{msg}.c_str();
            break;
        case vg_sane::LogLevel::Warn:
            qWarning() << std::string{msg}.c_str();
            break;
        default:
            qCritical() << std::string{msg}.c_str();
            break;
        }
    });

    MainWindow w{saneLibWrapperPtr};
    w.show();
    return app.exec();
}
