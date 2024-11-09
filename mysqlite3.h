#ifndef MYSQLITE3_H
#define MYSQLITE3_H

#include <QMutex>
#include<QSqlDatabase>

//存储着文件的hash与绝对路径信息
class MySqlite3
{
public:
    explicit MySqlite3();
    ~MySqlite3();
    static MySqlite3* getInstance();

    bool insert(QString hash,QString path);
    bool deleteByHash(QString hash);
    QString getPathByHash(QString hash);
    QPair<QString,QString> getHashAndPathByIndex(int index);
    bool updateByHash(QString hash,QString path);

private:
    QMutex mtxSql;
    QSqlDatabase db;

    static QMutex* mtxGetInstance;
    static MySqlite3* instance;
};

#endif // MYSQLITE3_H
