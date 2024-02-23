#pragma once

#include <QTcpSocket>
#include "SpeedTest.h"
#include "DataTypes.h"

class SpeedTestClient final: public QObject
{
public:
    explicit SpeedTestClient(const ServerInfo& serverInfo);
    ~SpeedTestClient();

    bool connect();
    bool ping(long &millisec);
    bool download(long size, long chunk_size, long &millisec);
    bool upload(long size, long chunk_size, long &millisec);
    QString version();
    const std::pair<QString, int> hostport();
    void close();


private:
    bool mkSocket();
    static bool readLine(QTcpSocket *socket, QString& buffer);
    static bool writeLine(QTcpSocket *socket, const QString& buffer);

    ServerInfo m_serverInfo;
    QString m_serverVersion;

    QTcpSocket* m_socket;
};

typedef bool (SpeedTestClient::*opFn)(const long size, const long chunk_size, long &millisec);
