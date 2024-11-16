#include "userfinder.h"
#include <QEventLoop>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

UserFinder::UserFinder()
{}

void UserFinder::changeIpv4(QString ipv4)
{
    rcvIPV4 = ipv4;
}

void UserFinder::changePort(int port)
{
    this->rcvPort = port;
}

void UserFinder::run()
{
    QUdpSocket udpSocket;
    //send Port为广播消息的端口,所以也应该从这个端口接受消息
    //QAbstractSocket::ShareAddress|QAbstractSocket::ReuseAddressHint 这里仅仅是为了调试,
    udpSocket.bind(QHostAddress::AnyIPv4, sendPort/*,QAbstractSocket::ShareAddress|QAbstractSocket::ReuseAddressHint*/);
    udpSocket.joinMulticastGroup(QHostAddress(groupV4));
    // For debug
    //udpSocket.setSocketOption(QAbstractSocket::MulticastLoopbackOption, QVariant(1));
    QEventLoop loop;
    QObject::connect(&m_signals,&UserFinderSingals::endTask,&loop,&QEventLoop::quit);
    //发送自己的地址与端口 并 接受他人告知的地址与端口
    QTimer timer;
    //广播IPV4地址与端口
    QObject::connect(&timer, &QTimer::timeout,&loop, [&udpSocket, this](){
        UserFinderPacket packet;
        // 无需上锁,就算读到错误数据也没事
        // 必须广播有效地址
        QHostAddress addr(rcvIPV4);
        if(addr.isNull() || rcvPort>65535 || rcvPort<1024)
            return;
        strncpy(packet.addr, rcvIPV4.toStdString().c_str(),rcvIPV4.size()+1);
        packet.port = rcvPort;
        udpSocket.writeDatagram((char*)&packet, sizeof(packet),QHostAddress(groupV4), sendPort);
        //qDebug()<<"send port ipv4 multicast";
    });
    //每秒广播一次自己的地址与端口
    timer.start(1000);
    QObject::connect(&udpSocket, &QUdpSocket::readyRead, &loop, [&udpSocket, this](){
        while(udpSocket.hasPendingDatagrams()){
            UserFinderPacket packet;
            udpSocket.readDatagram((char*)&packet, sizeof(packet));
            QString addr(packet.addr);
            emit m_signals.newUser(addr, packet.port);
            //qDebug()<< "rcv addr :"<<addr<<" port:"<<packet.port;
        }
    });
    loop.exec();
}
