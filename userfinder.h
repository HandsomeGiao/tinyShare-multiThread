#ifndef USERFINDER_H
#define USERFINDER_H

#include <QRunnable>
#include <QObject>

struct UserFinderSingals: public QObject
{
    Q_OBJECT
signals:
    void newUser(QString addr, int port);
    void endTask();
};

struct UserFinderPacket{
    char addr[130];
    int port;
};

class UserFinder : public QRunnable
{
public:
    UserFinder();
    void changeIpv4(QString ipv4);
    void changePort(int port);

    // QRunnable interface
public:
    virtual void run() override;
    UserFinderSingals m_signals;

private:
    const QString groupV4 = "239.111.111.111";
    const int sendPort = 8888;

    QString rcvIPV4 = "";
    int rcvPort = -1;
    //QString groupV6 = "ff05::1:3";
};

#endif // USERFINDER_H
