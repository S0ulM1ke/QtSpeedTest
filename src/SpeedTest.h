#pragma once

#include <QObject>
#include <QXmlStreamReader>
#include <functional>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "DataTypes.h"

class SpeedTestClient;
typedef bool (SpeedTestClient::*opFn)(const long size, const long chunk_size, long &millisec);
typedef void (*progressFn)(bool success);


class SpeedTest final: public QObject
{
    Q_OBJECT
public:
    explicit SpeedTest(float minServerVersion);
    ~SpeedTest();

    void initialize();

    QNetworkReply *get(const QString url);

    static QMap<QString, QString> parseQueryString(const QString& query);
    static QString getXmlString(QString xml, QString tagName);

    void getIpInfo();
    IPInfo ipInfo();

    const QVector<ServerInfo>& serverList();
    const ServerInfo bestServer(int sample_size = 5);

    const long &latency();
    bool downloadSpeed(const ServerInfo& server, const TestConfig& config, double& result);
    bool uploadSpeed(const ServerInfo& server, const TestConfig& config, double& result);
    bool jitter(const ServerInfo& server, long& result, int sample = 40);

signals:
    void ipInfoReceived();
    void serversFetched();
    void bestServerFound();
    void jitterChecked();
    void preflightChecked();

private slots:
    void fetchServers();
    void findBestServer();
    void testJitter();
    void preflightTest();
    void downloadSpeedTest();

private:

    bool testLatency(SpeedTestClient& client, int sample_size, long& latency);

    const ServerInfo findBestServerWithin(const QVector<ServerInfo> &serverList, long& latency, int sample_size = 5);

    static size_t writeFunc(void* buf, size_t size, size_t nmemb, void* userp);

    double execute(const ServerInfo &server, const TestConfig &config, const opFn &fnc);

    IPInfo mIpInfo;
    ServerInfo m_bestServer;
    QVector<ServerInfo> mServerList;

    TestConfig m_uploadConfig;
    TestConfig m_downloadConfig;

    long mLatency;
    double mUploadSpeed;
    double mDownloadSpeed;
    float mMinSupportedServer;
    bool strict_ssl_verify;

    QNetworkAccessManager m_QNAM;
};