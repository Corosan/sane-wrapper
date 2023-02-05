#include "mainwindow.h"

#include <sane_wrapper.h>

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QMessageBox>

#include <QtGlobal>
#include <QtDebug>

#include <exception>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "gui_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

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
