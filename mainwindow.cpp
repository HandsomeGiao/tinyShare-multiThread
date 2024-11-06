#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include<QDebug>
#include<QThreadPool>
#include<QMessageBox>
#include<QFileInfo>
#include<QFileDialog>
#include<QTimer>
#include<QDir>
#include<QProgressDialog>
#include<QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    server = new MyTcpServer(this);
    connect(server,&MyTcpServer::newClient,this,&MainWindow::do_newClient);

    QDir dir(QDir::currentPath());
    dir.mkdir("recvFiles");
    QDir::setCurrent(QDir::currentPath() + "/recvFiles");

    setFixedSize(300,130);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::do_taskEnd(bool s,QString info)
{
    if(s)
        QMessageBox::information(this,"传输文件成功",info);
    else
        QMessageBox::critical(this,"传输文件失败",info);
}

void MainWindow::do_newClient(qintptr socketDescriptor)
{
    RecvFileWorker* worker=new RecvFileWorker(socketDescriptor);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);
    connect(&(worker->signalsSrc),&WorkerSignals::newFile,this,&MainWindow::do_newFile);
    QThreadPool::globalInstance()->start(worker);
}

void MainWindow::do_newFile(QString name, quint64 size)
{
    // memory leak if close dialog rather than cancel
    QProgressDialog* dialog=
        new QProgressDialog(QString("接收文件:%1").arg(name),"中断传输",0,100,this);
    dialog->show();
    dialog->setAutoReset(false);
    connect(dialog,&QProgressDialog::canceled,dialog,&QProgressDialog::deleteLater);
    connect(dialog,&QProgressDialog::canceled,
            qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::forceEnd);
    connect(qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::process,dialog,&QProgressDialog::setValue);
}

void MainWindow::on_pbListen_clicked()
{
    if(server->isListening())
        server->close();

    int port= ui->leListenPort->text().toInt();
    if(port<1024 || port>65535){
        QMessageBox::critical(this,"listen failed","端口号应在1024-65535之间!");
        return;
    }
    if(server->listen(QHostAddress::Any,port)){
        //qDebug()<<"listen success!";
        QMessageBox::information(this,"监听端口","监听端口成功!");
    }else{
        QMessageBox::critical(this,"listen failed","监听端口失败,请检查端口是否有效!");
    }
}

void MainWindow::on_pbSend_clicked()
{
    QString path=QFileDialog::getOpenFileName(this,"选择文件",".","所有文件(*.*)");
    if(path.isEmpty())
        return;
    QFileInfo fileInfo(path);
    SendFileWorker* worker=new SendFileWorker(ui->leIP->text(),ui->lePort->text().toInt(),path);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);
    QProgressDialog* dialog=
        new QProgressDialog(QString("发送文件:%1").arg(fileInfo.fileName()),"中断连接",0,100,this);
    dialog->show();
    dialog->setAutoReset(false);
    connect(dialog,&QProgressDialog::canceled,&(worker->signalsSrc),&WorkerSignals::forceEnd);
    // memory leak if close dialog  rather  cancel
    connect(dialog,&QProgressDialog::canceled,dialog,&QProgressDialog::deleteLater);
    connect(&(worker->signalsSrc),&WorkerSignals::process,dialog,&QProgressDialog::setValue);
    QThreadPool::globalInstance()->start(worker);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(QThreadPool::globalInstance()->activeThreadCount()){
        event->ignore();
        QMessageBox::warning(this,"等待子线程","等待所有子线程完成!");
    }else{
        event->accept();
    }
}

///////////////////////////////////////////
///////////////SendFileWorker/////////////
/////////////////////////////////////////

SendFileWorker::SendFileWorker(QString _ip, int _port, QString _path)
    :ip(_ip),port(_port),filePath(_path)
{

}

SendFileWorker::~SendFileWorker()
{

}

void SendFileWorker::run()
{
    QFileInfo fileInfo(filePath);
    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly))
    {
        //qDebug()<<"file not open!";
        emit signalsSrc.taskOver(false,QString("文件 %1 打开失败!").arg(fileInfo.fileName()));
        return;
    }

    QTcpSocket socket;
    socket.connectToHost(ip,port);

    //wait connect
    QEventLoop* loop = new QEventLoop;
    bool isFailed=false;
    //在出现错误时,会发送两次taskOver信号
    auto connDis = QObject::connect(&socket,&QTcpSocket::disconnected,loop,[this,&isFailed,loop,&fileInfo](){
        emit signalsSrc.taskOver(false,QString("在传输 %1 时断开连接!").arg(fileInfo.fileName()));
        qDebug()<<"client disconnect!";
        loop->quit();
        loop->deleteLater();
        isFailed=true;
    });
    auto connErr = QObject::connect(&socket,&QTcpSocket::errorOccurred,loop,
        [this,&isFailed,loop,&fileInfo]()
        {
            emit signalsSrc.taskOver(false,QString("在传输 %1 时出现错误!").arg(fileInfo.fileName()));
            //qDebug()<<"error occurred!";
            isFailed=true;
            loop->quit();
            loop->deleteLater();
        });
    auto connForceEnd = QObject::connect
        (&signalsSrc,&WorkerSignals::forceEnd,loop,[this,&isFailed,loop](){
        //qDebug()<<"force end!";
        loop->quit();
        loop->deleteLater();
        isFailed=true;
        });
    auto conn =QObject::connect(&socket,&QTcpSocket::connected,loop,&QEventLoop::quit);
    loop->exec();
    if(isFailed){
        return;
    }
    //qDebug()<<loop->isRunning();
    //qDebug()<<"connect success";


    //qDebug()<<"file open success!";

    //send header
    FileHeader fHeader;
    fHeader.fileSize=fileInfo.size();
    strncpy(fHeader.fileName,fileInfo.fileName().toStdString().c_str(),sizeof(FileHeader::fileName));
    socket.write((char*)&fHeader,sizeof(FileHeader));
    int n=0;
    conn = QObject::connect(&socket,&QTcpSocket::bytesWritten,loop,[&n,loop](qint64 s){
        n+=s;
        if(n==sizeof(FileHeader))
            loop->quit();
    });
    loop->exec();
    if(isFailed){
        return;
    }
    //删除该连接,否则后续传输还会调用该函数
    QObject::disconnect(conn);

    //qDebug()<<"send file header success";

    //send file
    qint64 rstSize,totalSize;
    rstSize=totalSize=fileInfo.size();
    QTimer sendTimer;
    sendTimer.start(1);
    QByteArray buffer;
    QObject::connect(&sendTimer,&QTimer::timeout,loop,[&socket,&file,&rstSize,&sendTimer,this,loop,&buffer](){
        if(buffer.isEmpty())
            buffer=file.read(dataBlockSize);
        buffer = buffer.last(buffer.size()-socket.write(buffer));
        if(file.atEnd()){
            sendTimer.stop();
        }
    });
    QObject::connect(&socket,&QTcpSocket::bytesWritten,loop,[loop,&rstSize,this,totalSize,&connDis,&connErr](qint64 s){
        rstSize -= s;
        emit signalsSrc.process((double)(totalSize-rstSize)/totalSize*100);
        if(rstSize<=0){
            //避免在传输完成后收到错误提示
            QObject::disconnect(connDis);
            QObject::disconnect(connErr);
            loop->quit();
        }
    });
    loop->exec();
    loop->deleteLater();
    loop = nullptr;
    if(isFailed)
        return;

    //send file complete
    //qDebug()<<"run over success!";
    file.close();
    socket.close();
    emit signalsSrc.taskOver(true,"传输成功!");
}

///////////////////////////////////////
///////////////RecvFileWorker/////////
//////////////////////////////////////

RecvFileWorker::RecvFileWorker(qintptr _socketDescriptor)
    :socketDescriptor(_socketDescriptor)
{

}

RecvFileWorker::~RecvFileWorker()
{

}

void RecvFileWorker::run()
{
    QTcpSocket socket;
    socket.setReadBufferSize(readBufferSize);
    if(!socket.setSocketDescriptor(socketDescriptor)){
        //qDebug()<<"set socket failed!";
        emit signalsSrc.taskOver(false,"在接受文件时,打开套接字失败!");
        return;
    }
    //qDebug()<<"set socket success";

    //read header
    QEventLoop* loop=new QEventLoop();
    bool isFailed=false;
    //在出现错误时,会发送两次taskOver信号
    auto connDis = QObject::connect(&socket,&QTcpSocket::disconnected,loop,[this,&isFailed,loop](){
        emit signalsSrc.taskOver(false,"对方中断传输!");
        //qDebug()<<"client disconnect!";
        loop->quit();
        loop->deleteLater();
        isFailed=true;
    });

    auto connErr = QObject::connect(&socket,&QTcpSocket::errorOccurred,loop,[this,&isFailed,loop](){
        emit signalsSrc.taskOver(false,"文件传输出现错误!");
        //qDebug()<<"error occurred!";
        isFailed=true;
        loop->deleteLater();
        loop->quit();
    });
    auto connForceEnd = QObject::connect
        (&signalsSrc,&WorkerSignals::forceEnd,loop,[this,&isFailed,loop](){
            //qDebug()<<"force end!";
            loop->quit();
            loop->deleteLater();
            isFailed=true;
        });

    FileHeader fHeader;
    int n=0;
    auto conn = QObject::connect(&socket,&QTcpSocket::readyRead,loop,[&fHeader,&n,loop,&socket](){
         n += socket.read((char*)&fHeader+n,sizeof(FileHeader)-n);
         if(n==sizeof(FileHeader))
             loop->quit();
    });
    loop->exec();
    if(isFailed)
        return;
    //避免后续数据还会调用该函数
    QObject::disconnect(conn);

    emit signalsSrc.newFile(fHeader.fileName,fHeader.fileSize);

    //qDebug()<<"header read success!";

    qint64 rstSize=fHeader.fileSize;
    qint64 totalSize=fHeader.fileSize;
    QFile file(fHeader.fileName);
    //qDebug()<<"recv file : fileName="<<fHeader.fileName<<" fileSize="<<fHeader.fileSize;

    if(!file.open(QIODevice::WriteOnly)){
        //qDebug()<<"open file failed!";
        emit signalsSrc.taskOver(false,QString("打开文件 %1 失败!").arg(file.fileName()));
        return;
    }

    //send file
    QObject::connect(&socket,&QTcpSocket::readyRead,loop,[&file,&rstSize,loop,&socket,this,totalSize,&connDis,&connErr](){
        QByteArray buffer=socket.readAll();
        rstSize -= buffer.size();
        file.write(buffer);
        emit signalsSrc.process((double)(totalSize-rstSize)/totalSize*100);
        if(rstSize<=0){
            QObject::disconnect(connDis);
            QObject::disconnect(connErr);
            loop->quit();
        }
    });

    loop->exec();
    loop->deleteLater();
    loop = nullptr;
    if(isFailed)
        return;

    //qDebug()<<"recv file success!";
    emit signalsSrc.taskOver(true,QString("文件 %1 接受成功!").arg(file.fileName()));
    file.close();
    socket.close();
}


MyTcpServer::MyTcpServer(QObject *parent)
    :QTcpServer(parent)
{

}

void MyTcpServer::incomingConnection(qintptr socketDescriptor)
{
    emit newClient(socketDescriptor);
}
