#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "mysqlite3.h"
#include "userfinder.h"

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
#include<QCryptographicHash>
#include<QNetworkInterface>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    server = new MyTcpServer(this);
    connect(server,&MyTcpServer::newClient,this,&MainWindow::do_newClient);

    //接收文件默认存储路径
    fileSavedPath = QDir::currentPath()+"/recvFiles";

    //注意!setCurrent后整个程序的相对路径都会更改!!!!!!所以尽量不要使用这个命令
    //QDir::setCurrent(fileSavedPath);

    ui->leFileSavedDir->setText(fileSavedPath);

    //sa init
    // uncompleted info
    uncopmletedVL=new QVBoxLayout;
    QGroupBox* gb=new QGroupBox(this);
    gb->setLayout(uncopmletedVL);
    ui->saShowInfo->setWidget(gb);
    ui->saShowInfo->show();

    //user finder
    userFinder = new UserFinder();
    QThreadPool::globalInstance()->start(userFinder);
    connect(&(userFinder->m_signals),&UserFinderSingals::newUser,this,&MainWindow::do_newUserInfo);

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
    SendFileWorker* worker=new SendFileWorker(goalIP,goalPort,path,rootPath);
    //connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);

    QHBoxLayout* hl=new QHBoxLayout();
    QLineEdit* le=new QLineEdit(this);
    le->setReadOnly(true);
    le->setAlignment(Qt::AlignLeft);
    //等宽字体  在数字快速变化时也不会发生抖动
    //le->setFont({"Courier"});
    le->setText(QString("准备传输 %1,正在计算文件哈希值...").arg(fileInfo.fileName()));

    auto btn = new QPushButton("取消传输",this);
    connect(&(worker->signalsSrc),&WorkerSignals::process,le,[fileInfo,le](quint64 suc,quint64 total){
        QString info = QString("%1 : %2MB/ %3MB/ %4%").arg(fileInfo.fileName()).arg(suc/1024.0/1024.0,7,'f',2).
                       arg(total/1024.0/1024.0,7,'f',2).arg(suc*100.0/total,3,'f',1);
        le->setText(info);
    });
    connect(btn,&QPushButton::clicked,&(worker->signalsSrc),&WorkerSignals::forceEnd);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,le,[hl,btn,this,le](bool s,QString info){
        btn->disconnect();
        le->setText(info);
        //qDebug()<<"setFormat:"<<info;
        btn->setText("删除消息");
        connect(btn,&QPushButton::clicked,hl,[hl,btn,le](){
            le->deleteLater();
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
    hl->addWidget(le);
    hl->addWidget(btn);
    hl->setStretchFactor(le,1);
    hl->setStretchFactor(btn,0);
    uncopmletedVL->addLayout(hl);

    // start需要在最后使用,避免worker先执行完而信号没有连接上
    QThreadPool::globalInstance()->start(worker);
}

bool MainWindow::isLocalIP(QString ip)
{
    // 获取所有网络接口
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

    // 遍历所有接口
    for (const QNetworkInterface &interface : interfaces) {
        // 获取接口的所有地址
        QList<QNetworkAddressEntry> entries = interface.addressEntries();

        // 遍历所有地址
        for (const QNetworkAddressEntry &entry : entries) {
            // 检查地址是否匹配
            if (entry.ip().toString() == ip) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::do_taskEnd(bool s,QString info)
{
    //do nothing
    //现在已经废弃了
}

void MainWindow::do_newClient(qintptr socketDescriptor)
{
    RecvFileWorker* worker=new RecvFileWorker(socketDescriptor,fileSavedPath);
    connect(&(worker->signalsSrc),&WorkerSignals::taskOver,this,&MainWindow::do_taskEnd);
    connect(&(worker->signalsSrc),&WorkerSignals::newFile,this,&MainWindow::do_newFile);

    // start需要在最后使用,避免worker先执行完而信号没有连接上
    QThreadPool::globalInstance()->start(worker);
}

void MainWindow::do_newFile(QString name, quint64 size)
{
    // memory leak if close dialog rather than cancel
    QHBoxLayout* hl=new QHBoxLayout();
    QLineEdit* le=new QLineEdit(this);
    le->setReadOnly(true);
    le->setAlignment(Qt::AlignLeft);
    //等宽字体  在数字快速变化时也不会发生抖动

    auto btn = new QPushButton("取消传输",this);
    connect(qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::process,le,[le,name](quint64 suc,quint64 total){
        QString info = QString("%1 : %2MB/ %3MB / %4 %").
                       arg(name).arg(suc/1024.0/1024.0,7,'f',2).arg(total/1024.0/1024.0,7,'f',2).arg(suc*100.0/total,3,'f',1);
        le->setText(info);
        //qDebug()<<info;
    });
    connect(qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::taskOver,le,[le,hl,btn,this](bool s,QString info){
        le->setText(info);
        btn->disconnect();
        btn->setText("删除消息");
        //qDebug()<<"task over: "+info;
        connect(btn,&QPushButton::clicked,hl,[hl,le,btn](){
            le->deleteLater();
            btn->deleteLater();
            hl->deleteLater();
        });

        //移动到completedVL
        if(s){
            uncopmletedVL->removeItem(hl);
            completedVL->addLayout(hl);
        }
    });
    hl->addWidget(le);
    hl->addWidget(btn);
    hl->setStretchFactor(le,1);
    hl->setStretchFactor(btn,0);
    connect(btn,&QPushButton::clicked,qobject_cast<WorkerSignals*>(sender()),&WorkerSignals::forceEnd);
    uncopmletedVL->addLayout(hl);

    //continue
    emit qobject_cast<WorkerSignals*>(sender())->taskContinue();
}

void MainWindow::do_newUserInfo(QString ip, int port)
{
    //192.168.0.1:9000 IPV6地址格式同IPV4
    for(int i=0;i<ui->cbGoalIP->count();++i){
        QString item = ui->cbGoalIP->itemText(i);
        //qDebug()<<"item="<<item;
        QString oldIP=item.first(item.lastIndexOf(':'));
        //qDebug()<<"oldIP="<<oldIP<<" ip="<<ip;
        int oldPort = item.last(item.size() - item.lastIndexOf(':')-1).toInt();
        //qDebug()<<"oldPort="<<oldPort<<" port="<<port;
        if(oldIP == ip){
            if(oldPort != port)
                ui->cbGoalIP->setItemText(i,ip+':'+QString::number(port));
            return;
        }
    }
    //没有找到相同IP
    QString s = ip+':'+QString::number(port);
    //qDebug()<<"s = "<<s;
    ui->cbGoalIP->addItem(s);
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
    if(QThreadPool::globalInstance()->activeThreadCount() > 1){
        event->ignore();
        QMessageBox::warning(this,"等待子线程","等待所有子线程完成!");
    }else{
        emit userFinder->m_signals.endTask();
        QThread::msleep(100);
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
    // 默认会计算hash值,用于断点续传

    QTcpSocket socket;

    //wait connect
    QEventLoop* loop = new QEventLoop;
    bool isFailed=false;
    //在出现错误时,会发送两次taskOver信号
    auto connDis = QObject::connect(&socket,&QTcpSocket::disconnected,loop,[this,&isFailed,loop,&fileInfo](){
        emit signalsSrc.taskOver(false,QString("在传输 %1 时断开连接!").arg(fileInfo.fileName()));
        //qDebug()<<"client disconnect!";
        loop->quit();
        loop->deleteLater();
        isFailed=true;
    });
    auto connErr = QObject::connect(&socket,&QTcpSocket::errorOccurred,loop,
        [this,&isFailed,loop,&fileInfo,&socket]()
        {
            emit signalsSrc.taskOver(false,QString("在传输 %1 时出现错误:%2").arg(fileInfo.fileName(),socket.errorString()));
            //qDebug()<<"error occurred:"+socket.errorString();
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
    socket.connectToHost(ip,port);
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
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.reset();
    //qDebug()<<"hash result = "<<hash.result().toHex();
    strncpy(fHeader.fileHash,hash.result().toHex().toStdString().c_str(),sizeof(FileHeader::fileHash));
    //得到文件的相对路径,这个相对路径要包括传送的文件夹名,以便接收方保存
    if(!rootPath.isEmpty()){
        //这里的rootPath(即选择传送的文件夹)必须包含至少一个'/',否则会出现错误
        rootPath = rootPath.first(rootPath.lastIndexOf('/'));
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

    //read reply
    FileHeaderReply reply;
    {
        //read fHeader
        int n=0;
        //避免数据已经发送过来
        n += socket.read((char*)&reply+n,sizeof(FileHeaderReply)-n);
        auto conn = QObject::connect(&socket,&QTcpSocket::readyRead,loop,[&reply,&n,loop,&socket](){
            n += socket.read((char*)&reply+n,sizeof(FileHeaderReply)-n);
            if(n==sizeof(FileHeaderReply))
                loop->quit();
        });
        if(n != sizeof(FileHeaderReply))
            loop->exec();
        if(isFailed)
            return;
        //避免后续数据还会调用该函数
        QObject::disconnect(conn);
    }

    //send file
    qint64 rstSize,totalSize;
    rstSize=totalSize=fileInfo.size()-reply.begPos;
    if(rstSize > 0){
        //有数据需要发送
        file.seek(reply.begPos);
        //qDebug()<<fileInfo.fileName()<<"beg at "<<reply.begPos;

        //先关联bytesWritten信号,避免数据发送完毕了还没有触发该信号
        QObject::connect(&socket,&QTcpSocket::bytesWritten,loop,[loop,&rstSize,this,totalSize,&connDis,&connErr](qint64 s){
            rstSize -= s;
            emit signalsSrc.process(totalSize-rstSize,totalSize);
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

        //等待数据发送完毕
        loop->exec();
        if(isFailed)
            return;
        //到此为止,所有数据已经写入底层Tcp协议栈,但是可能还没有发送出去,这里需要等待对方在接受完成后主动关闭连接
        // 有可能底层TCP传输失败,导致这里卡死?
        //qDebug()<<"wait for client disconnect";
        QObject::connect(&socket,&QTcpSocket::disconnected,loop,&QEventLoop::quit);
        loop->exec();
        if(isFailed)
            return;
    }

    //没有数据需要发送,直接成功
    delete loop;

    //send file complete
    //qDebug()<<"run over success!";
    file.close();
    socket.close();
    emit signalsSrc.taskOver(true,QString("文件 %1 传输成功").arg(fileInfo.fileName()));
}

///////////////////////////////////////
///////////////RecvFileWorker/////////
//////////////////////////////////////

RecvFileWorker::RecvFileWorker(qintptr _socketDescriptor,QString _fileSavedPath)
    :socketDescriptor(_socketDescriptor),fileSavedPath(_fileSavedPath)
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
    {
        //read fHeader
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
    }

    //告知主线程接受的文件名字与大小
    //主线程需要一定的时间处理该信号,否则可能会导致显示异常,因此需要休眠一段时间
    //qDebug()<<"file hash = "<<fHeader.fileHash;
    emit signalsSrc.newFile(fHeader.releativeFileName,fHeader.fileSize);
    {
        // local tcnn
        auto tcnn = QObject::connect(&signalsSrc,&WorkerSignals::taskContinue,loop,&QEventLoop::quit);
        loop->exec();
        if(isFailed)
            return;
        QObject::disconnect(tcnn);
    }

    //断点续传
    //qDebug()<<"beg send reply";
    qint64 rstSize;
    qint64 totalSize;
    //open file,maybe need to create dir !
    QFile file;
    QFileInfo fileInfo;

    FileHeaderReply reply;
    MySqlite3* db = MySqlite3::getInstance();
    //如果数据库无法打开,这里重新传输整个文件
    QString lastPath = db->getPathByHash(fHeader.fileHash);

    if(!lastPath.isEmpty()){
        //qDebug()<<"lastPath="<<lastPath;
        file.setFileName(lastPath);
        if(file.open(QIODevice::Append)){
            fileInfo.setFile(lastPath);
            rstSize=totalSize=fHeader.fileSize-file.size();
            //qDebug()<<QString("文件:%1 续传成功,已有数据为%2MB").arg(fileInfo.fileName()).arg(file.size()/1024.0/1024.0);
            reply.begPos=file.size();
        }
    }
    //如果数据库中不存在对应文件记录或者打开上次路径失败,则打开新文件
    if(!file.isOpen()){
        reply.begPos=0;
        rstSize=totalSize=fHeader.fileSize;
        QString filePath = fileSavedPath+"/"+fHeader.releativeFileName;
        fileInfo.setFile(filePath);
        file.setFileName(filePath);

        {
            //mkdir if necessary
            //在对方传送文件夹时,relativeFileName可能包括文件夹路径,因此需要首先创建文件夹
            QDir dir;
            QString path = fileInfo.absolutePath();
            if(!dir.mkpath(path)){
                //qDebug()<<"mkdir failed:"<<path;
                emit signalsSrc.taskOver(false,
                                         QString("创建文件夹 %1 失败!").arg(path));
                return;
            }
            //qDebug()<<"mkdir success:"<<dirPath;
            //mkdir end
        }

        if(!file.open(QIODevice::WriteOnly)){
            //qDebug()<<"open file failed!";
            emit signalsSrc.taskOver(false,QString("打开文件 %1 失败!").arg(fileInfo.fileName()));
            return;
        }

        //qDebug()<<QString("update %1 by hash:").arg(fileInfo.fileName())<<db->updateByHash(fHeader.fileHash,fileInfo.absoluteFilePath());
    }

    //qDebug()<<"header read success!";
    //send reply
    //qDebug()<<"reply begPos = "<<reply.begPos;
    // 默认发送一次成功,后续改进
    socket.write((char*)&reply,sizeof(reply));

    //qDebug()<<"recv file : fileName="<<fHeader.fileName<<" fileSize="<<fHeader.fileSize;

    //recv file
    QObject::connect(&socket,&QTcpSocket::readyRead,loop,[&file,&rstSize,loop,&socket,this,totalSize,&connDis,&connErr](){
        QByteArray buffer=socket.readAll();
        rstSize -= buffer.size();
        file.write(buffer);
        emit signalsSrc.process(totalSize-rstSize,totalSize);
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

    //如果没有数据需要发送,则直接认为成功
    if(rstSize<=0)
    {
        QObject::disconnect(connDis);
        QObject::disconnect(connErr);
        // 100ms后直接退出事件循环
        QTimer::singleShot(100,loop,&QEventLoop::quit);
    }
    loop->exec();

    loop->deleteLater();
    loop = nullptr;
    if(isFailed)
        return;

    //qDebug()<<"recv file success!";
    emit signalsSrc.taskOver(true,QString("文件 %1 接受成功!").arg(fileInfo.fileName()));
    //db->deleteByHash(fHeader.fileHash);
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
    if(allFiles.size()>100){
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


void MainWindow::on_pbChooseDir_clicked()
{
    QString path=QFileDialog::getExistingDirectory(this,"选择文件夹",".");
    if(path.isEmpty())
        return;
    fileSavedPath = path;
    QDir::setCurrent(fileSavedPath);
    ui->leFileSavedDir->setText(fileSavedPath);
}


void MainWindow::on_pbShowIP_clicked()
{
    QString infostr;
    auto interfaces = QNetworkInterface::allInterfaces();
    for(auto& i:interfaces){
        auto entries = i.addressEntries();
        infostr+="interface name: ";
        infostr+=i.humanReadableName();
        for(auto& j:entries){
            infostr+='\n';
            infostr += "ip:";
            infostr+=j.ip().toString();
            infostr += " netmask:";
            infostr+=j.netmask().toString();
            //qDebug()<<"ip:"<<j.ip().toString()<<" netmask:"<<j.netmask().toString();
        }
        infostr+="\n----------------\n";
    }
    QMessageBox *msg = new QMessageBox(this);
    msg->setText(infostr);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msg->exec();
    msg->deleteLater();
}


void MainWindow::on_leIPRcv_editingFinished()
{
    QString ip = ui->leIPRcv->text();
    if(isLocalIP(ip)){
        userFinder->changeIpv4(ui->leIPRcv->text());
    }else{
        QMessageBox::warning(this,"错误","请输入本机IP地址!");
    }
}


void MainWindow::on_leListenPort_editingFinished()
{
    userFinder->changePort(ui->leListenPort->text().toInt());
}


void MainWindow::on_cbGoalIP_currentTextChanged(const QString &arg1)
{
    goalIP = arg1.first(arg1.lastIndexOf(':'));
    goalPort = arg1.last(arg1.size()-arg1.lastIndexOf(':')-1).toInt();
    //qDebug()<<"goalIP="<<goalIP<<" goalPort="<<goalPort;
}

