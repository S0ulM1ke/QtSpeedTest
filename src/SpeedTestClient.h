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
    bool upload(long size, long chunk_size, long &millisec);
    bool download(long size, long chunk_size, long &millisec);
    float version();
    const std::pair<QString, int> hostport();
    void close();


private:
    bool mkSocket();
    ServerInfo mServerInfo;
    float mServerVersion;
    static bool readLine(QTcpSocket *socket, QString& buffer);
    static bool writeLine(QTcpSocket *socket, const QString& buffer);

    QTcpSocket* m_socket;
};

typedef bool (SpeedTestClient::*opFn)(const long size, const long chunk_size, long &millisec);
