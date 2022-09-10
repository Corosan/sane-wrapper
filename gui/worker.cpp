#include "worker.h"

Controller::Controller()
{
    m_workerThread.setObjectName("Controller_thread");

    auto worker = new Worker;
    worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, worker, &QObject::deleteLater);
//    connect(this, &Controller::operate, worker, &Worker::doWork);
//    connect(worker, &Worker::resultReady, this, &Controller::handleResults);
    m_workerThread.start();
}

Controller::~Controller()
{
    m_workerThread.quit();
    m_workerThread.wait();
}
