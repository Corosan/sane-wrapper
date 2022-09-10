#pragma once

#include <QObject>
#include <QThread>

class Worker : public QObject
{
    Q_OBJECT

public:

};

class Controller : public QObject
{
    Q_OBJECT
    QThread m_workerThread;

public:
    Controller();
    ~Controller();
};
