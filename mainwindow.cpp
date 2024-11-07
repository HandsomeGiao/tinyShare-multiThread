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
#include<QGroupBox>
#include <QProgressBar>

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

    setFixedSize(600,300);

    //sa init
    // uncompleted info
    uncopmletedVL=new QVBoxLayout;
    QGroupBox* gb=new QGroupBox(this);
    gb->setLayout(uncopmletedVL);
    ui->saShowInfo->setWidget(gb);
    ui->saShowInfo->show();

    //completed info
    completedVL=new QVBoxLayout;
    gb=new QGroupBox(this);
    gb->setLayout(completedVL);
    ui->saCompleted->setWidget(gb);
    ui->saCompleted->show();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::getAllFiles(QStringList &allFiles, QString dirPath)
{
    auto files = QDir(dirPath).entryList(QDir::Files|QDir::NoDotAndDotDot);
    auto dirs = QDir(dirPath).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    for(auto& i:files){
        allFiles.push_back(dirPath+"/"+i);
    }
    for(auto& i:dirs){
        getAllFiles(allFiles,dirPath+"/"+i);
    }
}

void MainWindow::sendFile(QString path,QString rootPath)
{
    //传入的是绝对路径
    QFileInfo fileInfo(path);
    //忽略空文件
    if(fileInfo.size()==0)
        return;
    SendFileWorker* worker=new SendFileWorker(ui->leIP->text(),ui->lePort->text().toInt(),path,rootPath);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);

    QHBoxLayout* hl=new QHBoxLayout();
    QProgressBar* bar=new QProgressBar(this);
    auto btn = new QPushButton("取消传输",this);
    bar->setFormat(QString("%1 : %p%").arg(fileInfo.fileName()));
    connect(&(worker->signalsSrc),&WorkerSignals::process,bar,&QProgressBar::setValue);
    connect(btn,&QPushButton::clicked,&(worker->signalsSrc),&WorkerSignals::forceEnd);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,bar,[bar,hl,btn,this](bool s,QString info){
        btn->disconnect();
        bar->setFormat(info);
        btn->setText("删除消息");
        connect(btn,&QPushButton::clicked,hl,[hl,bar,btn](){
            bar->deleteLater();
            btn->deleteLater();
            hl->deleteLater();
        });
        //移动到completedVL
        if(s){
            uncopmletedVL->removeItem(hl);
            completedVL->addLayout(hl);
        }
    });
    //ui
    hl->addWidget(bar);
    hl->addWidget(btn);
    hl->setStretchFactor(bar,1);
    hl->setStretchFactor(btn,0);
    uncopmletedVL->addLayout(hl);
    bar->show();

    // start需要在最后使用,避免worker先执行完而信号没有连接上
    QThreadPool::globalInstance()->start(worker);
}

void MainWindow::do_taskEnd(bool s,QString info)
{
    //do nothing
    //在文件传输结束时,可以通过这个函数检测到对应信息(暂时没有删除connect函数),但是现在已经废弃了
}

void MainWindow::do_newClient(qintptr socketDescriptor)
{
    RecvFileWorker* worker=new RecvFileWorker(socketDescriptor);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);
    connect(&(worker->signalsSrc),&WorkerSignals::newFile,this,&MainWindow::do_newFile);

    // start需要在最后使用,避免worker先执行完而信号没有连接上
    QThreadPool::globalInstance()->start(worker);
}

void MainWindow::do_newFile(QString name, quint64 size)
{
    // memory leak if close dialog rather than cancel
    QHBoxLayout* hl=new QHBoxLayout();
    QProgressBar* bar=new QProgressBar(this);
    auto btn = new QPushButton("取消传输",this);
    bar->setFormat(QString("%1 : %p%").arg(name));
    connect(qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::process,bar,&QProgressBar::setValue);
    connect(qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::taskOver,bar,[bar,hl,btn,this](bool s,QString info){
        bar->setFormat(info);
        btn->disconnect();
        btn->setText("删除消息");
        connect(btn,&QPushButton::clicked,hl,[hl,bar,btn](){
            bar->deleteLater();
            btn->deleteLater();
            hl->deleteLater();
        });

        //移动到completedVL
        if(s){
            uncopmletedVL->removeItem(hl);
            completedVL->addLayout(hl);
        }
    });
    hl->addWidget(bar);
    hl->addWidget(btn);
    hl->setStretchFactor(bar,1);
    hl->setStretchFactor(btn,0);
    connect(btn,&QPushButton::clicked,qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::forceEnd);
    uncopmletedVL->addLayout(hl);
    bar->show();

    //continue
    emit qobject_cast<WorkerSignals*>(sender())->taskContinue();
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
    sendFile(path);
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

SendFileWorker::SendFileWorker(QString _ip, int _port, QString _path,QString _releativePath)
    :ip(_ip),port(_port),filePath(_path),rootPath(_releativePath)
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

    //防止传输过快导致显示异常
    emit signalsSrc.process(1);

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
        [this,&isFailed,loop,&fileInfo,&socket]()
        {
            emit signalsSrc.taskOver(false,QString("在传输 %1 时出现错误:%2").arg(fileInfo.fileName(),socket.errorString()));
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
    //得到文件的相对路径
    if(!rootPath.isEmpty()){
        QDir dir(rootPath);
        strncpy(fHeader.releativeFileName,dir.relativeFilePath(filePath).toStdString().c_str(),sizeof(FileHeader::releativeFileName));
    }else
        strncpy(fHeader.releativeFileName,fileInfo.fileName().toStdString().c_str(),sizeof(FileHeader::releativeFileName));
    socket.write((char*)&fHeader,sizeof(FileHeader));
    //qDebug()<<"send file name:"<<fHeader.releativeFileName;
    int n=0;
    conn = QObject::connect(&socket,&QTcpSocket::bytesWritten,loop,[&n,loop](qint64 s){
        n += s;
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

    //先关联bytesWritten信号,避免数据发送完毕了还没有触发该信号
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

    QTimer sendTimer;
    QByteArray buffer;
    QObject::connect(&sendTimer,&QTimer::timeout,loop,[&socket,&file,&rstSize,&sendTimer,this,loop,&buffer](){
        if(buffer.isEmpty())
            buffer=file.read(dataBlockSize);
        //qDebug()<<"before buffer size="<<buffer.size();
        buffer = buffer.last(buffer.size()-socket.write(buffer));
        //qDebug()<<"after buffer size="<<buffer.size();
        if(file.atEnd()){
            sendTimer.stop();
        }
    });
    sendTimer.start(1);

    //如果文件为空文件 则无需事件循环
    loop->exec();

    if(isFailed)
        return;

    //qDebug()<<"wait for client disconnect";

    //到此为止,所有数据已经写入底层Tcp协议栈,但是可能还没有发送出去,这里需要等待对方在接受完成后主动关闭连接
    // 有可能底层TCP传输失败,导致这里卡死?
    QObject::connect(&socket,&QTcpSocket::disconnected,loop,&QEventLoop::quit);
    loop->exec();
    if(isFailed)
        return;
    loop->deleteLater();
    loop = nullptr;

    //send file complete
    //qDebug()<<"run over success!";
    file.close();
    socket.close();
    emit signalsSrc.taskOver(true,QString("文件 %1 传输成功").arg(fileInfo.fileName()));
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

    //告知主线程接受的文件名字与大小
    //主线程需要一定的时间处理该信号,否则可能会导致显示异常,因此需要休眠一段时间
    emit signalsSrc.newFile(fHeader.releativeFileName,fHeader.fileSize);
    {
        // local tcnn
        auto tcnn = QObject::connect(&signalsSrc,&WorkerSignals::taskContinue,loop,&QEventLoop::quit);
        loop->exec();
        if(isFailed)
            return;
        QObject::disconnect(tcnn);
    }
    emit signalsSrc.process(0);

    //qDebug()<<"header read success!";

    qint64 rstSize=fHeader.fileSize;
    qint64 totalSize=fHeader.fileSize;
    //open file,maybe need to create dir !
    QFile file(fHeader.releativeFileName);
    QFileInfo fileInfo(fHeader.releativeFileName);

    //mkdir if necessary
    int index = file.fileName().lastIndexOf('/');
    if(index!=-1){
        QString dirPath;
        dirPath=file.fileName().first(index);
        //qDebug()<<"dirPath="<<dirPath;
        QDir dir;
        if(!dir.mkpath(dirPath)){
            emit signalsSrc.taskOver(false,
                                     QString("创建文件夹 %1 失败!").arg(dirPath));
            return;
        }
    }
    //mkdir end

    //qDebug()<<"recv file : fileName="<<fHeader.fileName<<" fileSize="<<fHeader.fileSize;

    if(!file.open(QIODevice::WriteOnly)){
        //qDebug()<<"open file failed!";
        emit signalsSrc.taskOver(false,QString("打开文件 %1 失败!").arg(fileInfo.fileName()));
        return;
    }

    //recv file
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
    //check small file
    if(socket.bytesAvailable()){
        QByteArray buffer=socket.readAll();
        rstSize -= buffer.size();
        file.write(buffer);
    }

    if(rstSize<=0)
    {
        QObject::disconnect(connDis);
        QObject::disconnect(connErr);
    }else
        loop->exec();

    loop->deleteLater();
    loop = nullptr;
    if(isFailed)
        return;

    //qDebug()<<"recv file success!";
    emit signalsSrc.taskOver(true,QString("文件 %1 接受成功!").arg(fileInfo.fileName()));
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

void MainWindow::on_pbSendDir_clicked()
{
    //存储全部文件的绝对路径
    QStringList allFiles;
    QString root = QFileDialog::getExistingDirectory();
    if(root.isEmpty())
        return;
    getAllFiles(allFiles, root);
    // for(auto& i:allFiles){
    //     qDebug()<<i;
    // }

    if(allFiles.isEmpty()){
        QMessageBox::critical(this,"错误","文件夹为空!");
        return;
    }
    // if(allFiles.size()>100){
    //     QMessageBox::warning(this,"文件数量过多",QString("文件数量为%1(>100),过多!请压缩后发送!").arg(allFiles.size()));
    //     return;
    // }
    if(allFiles.size()>10){
        auto rst = QMessageBox::
            question(this,"确认",QString("文件内文件数量为%1,建议以压缩包进行传输,是否继续传输?").arg(allFiles.size()),
                     QMessageBox::Yes|QMessageBox::No);
        if(rst==QMessageBox::No)
            return;
    }

    for(auto &p:allFiles){
        sendFile(p,root);
    }

    //do nothing,wait for implement
}

void MainWindow::on_pbClearCompleted_clicked()
{
    //删除所有完成记录
    // 请不要修改completedVL/uncompltedVL的子控件结构! 2024.11.7 15.18
    QLayoutItem* item0=nullptr;
    while((item0 = completedVL->takeAt(0))!=nullptr){
        QPushButton* btn = qobject_cast<QPushButton*>(item0->layout()->itemAt(1)->widget());
        btn->click();
    }
}

void MainWindow::on_pbCancelAll_clicked()
{
    // 停止所有未完成的任务
    // 请不要修改completedVL/uncompltedVL的子控件结构! 2024.11.7 15.18
    for(int i=0;i<uncopmletedVL->count();++i){
        auto item = uncopmletedVL->itemAt(i);
        QPushButton* btn = qobject_cast<QPushButton*>(item->layout()->itemAt(1)->widget());
        btn->click();
    }
}

