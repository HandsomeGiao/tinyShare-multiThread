#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QRunnable>
#include <QFile>
#include<QTcpSocket>
#include<QTcpServer>
#include<QObject>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

struct FileHeader{
    char fileName[1024];
    quint64 fileSize;
};

class WorkerSignals : public QObject
{
    Q_OBJECT
public:
    WorkerSignals(){}
    ~WorkerSignals(){}
signals:
    void taskOver(bool success);
    void process(int bytes);
    void newFile(QString name,quint64 size);
};

class SendFileWorker : public QRunnable
{
public:
    SendFileWorker(QString ip,int port,QString path);
    ~SendFileWorker();
    void run() override;
    WorkerSignals signalsSrc;
private:
    QString ip;
    int port;
    QString filePath;

    quint64 readBufferSize=100*1024*1024;
    quint64 dataBlockSize=65536;
};

class RecvFileWorker : public QRunnable
{
public:
    RecvFileWorker(qintptr _socketDescriptor);
    ~RecvFileWorker();
    void run() override;
    WorkerSignals signalsSrc;

private:
    qintptr socketDescriptor;

    quint64 readBufferSize=100*1024*1024;
    quint64 dataBlockSize=65536;
};

class MyTcpServer : public QTcpServer
{
    Q_OBJECT
public:
    MyTcpServer(QObject* parent=nullptr);
    void incomingConnection(qintptr socketDescriptor) override;
signals:
    void newClient(qintptr socketDescriptor);
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

    // 在点击mainWindow的关闭按钮后,如果正在传输文件,则另外一方会卡死,真奇怪.
    // 貌似是因为子线程阻碍了主线程的关闭,导致主线程无法正常关闭.
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void do_taskEnd(bool s);
    void do_newClient(qintptr socketDescriptor);
    void do_newFile(QString name,quint64 size);

    void on_pbListen_clicked();
    void on_pbSend_clicked();

private:
    Ui::MainWindow *ui;

    MyTcpServer* server=nullptr;
    QTcpSocket* client=nullptr;

    // QWidget interface
protected:
};

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////


#endif // MAINWINDOW_H