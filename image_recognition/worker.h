#ifndef WORKER_H
#define WORKER_H

#include <QObject>

class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr);
    void doWork(QImage img,QThread*);

signals:
    void resultReady(QByteArray,QThread* childThread);
};

#endif // WORKER_H
