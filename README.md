# tinyShare-multiThread

环境:
QT6.8 MINGW64 QTcreator14.0.2(community)

已知存在的问题:
1. ~~发送方发送过快会导致数据丢失,双方死锁~~
2. ~~在发送完成后,双方都会提示发送出错,因为断开连接的信号被检测到了.~~
3. ~~不能够终止传输过程,否则会产生僵尸线程.~~
4. 传输小文件偶尔会失败,这在传输文件夹时尤为明显.
