#pragma once

#include <QTcpSocket>
#include "SpeedTest.h"
#include "DataTypes.h"

class SpeedTestClient final: public QObject
{
    Q_OBJECT
public:
    explicit SpeedTestClient(const ServerInfo& serverInfo);
    ~SpeedTestClient();

    bool connect();
    void connectToSocket();
    bool ping(long &millisec);
    void download(const long size, const long chunk_size);
    void upload(const long size, const long chunk_size);
    QString version();
    const std::pair<QString, int> hostport();
    void close();

signals:
    void socketConnected();
    void opFinisfed(double mlsec);


private:
    bool mkSocket();
    static bool readLine(QTcpSocket *socket, QString& buffer);
    bool writeLine(const QString& buffer);

    ServerInfo m_serverInfo;
    QString m_serverVersion;

    QTcpSocket* m_socket;
};

//typedef bool (SpeedTestClient::*opFn)(const long size, const long chunk_size, long &millisec);
typedef void (SpeedTestClient::*opFn)(const long size, const long chunk_size);
