#include "mysqlite3.h"
#include<QMutexLocker>
#include<QSqlQuery>
#include<QSqlError>
#include<QCoreApplication>

//init static variable
MySqlite3* MySqlite3::instance = nullptr;
QMutex* MySqlite3::mtxGetInstance = new QMutex();

MySqlite3::MySqlite3()
{
    db = QSqlDatabase::addDatabase("QSQLITE");
    // 1. 请不要调用QDir::setCurrent()函数,会导致整个程序的相对路径都会更改!!!!!!
    // 2. 请将数据库文件放在程序的根目录下,否则会出现找不到数据库文件的错误
    db.setDatabaseName("sqlite3/savedFilesInfo.db");
    if(!db.open()){
        qDebug()<<"in MySqlite3::MySqlite3()";
        qDebug() << db.lastError().text();
    }
}

MySqlite3::~MySqlite3()
{
    db.close();
}

MySqlite3 *MySqlite3::getInstance()
{
    //可以使用std::once_flag优化
    QMutexLocker locker(mtxGetInstance);
    if(instance == nullptr)
        instance = new MySqlite3();

    return instance;
}

bool MySqlite3::insert(QString hash, QString path)
{
    QMutexLocker locker(&mtxSql);

    QSqlQuery query(db);
    QString order = QString("insert into filesInfo values('%1','%2');").arg(hash,path);
    if(!query.exec(order)){
        qDebug()<<query.lastError().text();
        return false;
    }
    //qDebug()<<"insert success,order = "<<order;
    return true;
}

bool MySqlite3::deleteByHash(QString hash)
{
    QMutexLocker locker(&mtxSql);

    QSqlQuery query(db);
    QString order = QString("delete from filesInfo where fileHash = '%1';").arg(hash);
    if(!query.exec(order)){
        qDebug()<<query.lastError().text();
        return false;
    }
    //qDebug()<<"delete success,order = "<<order;
    return true;
}

QString MySqlite3::getPathByHash(QString hash)
{
    QMutexLocker locker(&mtxSql);
    //没有找到就返回空字符串
    QSqlQuery query(db);
    QString order = QString("select path from filesInfo where fileHash = '%1';").arg(hash);
    //qDebug()<<order;
    if(!query.exec(order)){
        qDebug()<<query.lastError().text();
        return QString();
    }
    if(!query.next())
        return QString();
    return query.value(0).toString();
}

QPair<QString, QString> MySqlite3::getHashAndPathByIndex(int index)
{
    QMutexLocker locker(&mtxSql);

    QSqlQuery query(db);
    QString order = QString("select * from filesInfo;");
    if(!query.exec(order)){
        qDebug()<<query.lastError().text();
        return QPair<QString,QString>();
    }
    query.seek(index);
    return QPair<QString,QString>(query.value(0).toString(),query.value(1).toString());
}

bool MySqlite3::updateByHash(QString hash, QString path)
{
    //更新hash对应的值为path,如果不存在则插入一条新的记录
    QString p = getPathByHash(hash);

    QMutexLocker locker(&mtxSql);
    QSqlQuery query(db);
    if(p.isEmpty()){
        return query.exec(QString("insert into filesInfo values('%1','%2');").arg(hash,path));
    }else{
        return query.exec(QString("update filesInfo set path = '%1' where fileHash = '%2';").arg(path,hash));
    }
}
