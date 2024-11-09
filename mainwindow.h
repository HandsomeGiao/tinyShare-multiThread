#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QRunnable>
#include <QFile>
#include<QTcpSocket>
#include<QTcpServer>
#include<QObject>
#include<QVBoxLayout>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

//发送方头部,包含文件名与文件大小
struct FileHeader{
    char releativeFileName[1024];
    quint64 fileSize;
    char fileHash[65];
};

//接收方回复:接收文件的起始位置与结束位置
struct FileHeaderReply{
    quint64 begPos;
};

class WorkerSignals : public QObject
{
    Q_OBJECT
    //func
public:
    WorkerSignals(){}
    ~WorkerSignals(){}
signals:
    void taskOver(bool success,QString info);
    void process(quint64 finishedBytes,quint64 totalBytes);
    void newFile(QString name,quint64 size);
    void forceEnd();
    void taskContinue();
};

class SendFileWorker : public QRunnable
{
public:
    SendFileWorker(QString ip,int port,QString path,QString releativePath = QString());
    ~SendFileWorker();
    void run() override;
    WorkerSignals signalsSrc;
private:
    QString ip;
    int port;
    QString filePath;
    QString rootPath;

    quint64 readBufferSize=100*1024*1024;
    quint64 dataBlockSize=65536;
};

class RecvFileWorker : public QRunnable
{
public:
    RecvFileWorker(qintptr _socketDescriptor,QString _fileSavedPath);
    ~RecvFileWorker();
    void run() override;
    WorkerSignals signalsSrc;

private:
    qintptr socketDescriptor;

    quint64 readBufferSize=100*1024*1024;
    quint64 dataBlockSize=65536;

    QString fileSavedPath;
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
private:
    void getAllFiles(QStringList& allFiles,QString dirPath);
    void sendFile(QString path,QString rootPath=QString());
private slots:
    void do_taskEnd(bool s,QString info);
    void do_newClient(qintptr socketDescriptor);
    void do_newFile(QString name,quint64 size);

    void on_pbListen_clicked();
    void on_pbSend_clicked();

    void on_pbSendDir_clicked();

    void on_pbClearCompleted_clicked();

    void on_pbCancelAll_clicked();

    void on_pbChooseDir_clicked();

private:
    Ui::MainWindow *ui;

    MyTcpServer* server=nullptr;
    //存储所有文件传输信息的布局
    QVBoxLayout* uncopmletedVL=nullptr;
    QVBoxLayout* completedVL=nullptr;

    QString fileSavedPath;

    // QWidget interface
protected:

    // QWidget interface
protected:
    virtual void closeEvent(QCloseEvent *event) override;
};

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////


#endif // MAINWINDOW_H
