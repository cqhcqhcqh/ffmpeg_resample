#ifndef RESAMPLETHREAD_H
#define RESAMPLETHREAD_H

#include <QThread>

class ResampleThread : public QThread
{
    Q_OBJECT
private:
    void run() override;
public:
    ResampleThread(QObject *parent);
    ~ResampleThread();
signals:

};

#endif // RESAMPLETHREAD_H
