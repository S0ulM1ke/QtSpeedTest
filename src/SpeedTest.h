#pragma once

#include <QObject>
#include <QXmlStreamReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "DataTypes.h"

class SpeedTestClient;
typedef void (SpeedTestClient::*opFn)(const long size, const long chunk_size);
typedef void (*progressFn)(bool success);


class SpeedTest final: public QObject
{
    Q_OBJECT
    typedef void (SpeedTest::*callFn)(double speed);
public:
    explicit SpeedTest();
    ~SpeedTest();

    Q_PROPERTY(double downloadSpeed READ downloadSpeed WRITE setDownloadSpeed NOTIFY downloadSpeedChanged FINAL)

    Q_PROPERTY(double uploadSpeed READ uploadSpeed WRITE setUploadSpeed NOTIFY uploadSpeedChanged FINAL)

    void initialize();

    void getIpInfo();
    const IPInfo ipInfo();

    const QVector<ServerInfo>& serverList();
    const ServerInfo bestServer(int sample_size = 5);

    double downloadSpeed() const;
    void setDownloadSpeed(double speed);

    double uploadSpeed() const;
    void setUploadSpeed(double speed);

    const long &latency();

signals:
    void ipInfoReceived();
    void serversFetched();
    void bestServerFound();
    void jitterChecked();
    void downloadTestFinished(double result);
    void uploadTestFinished(double result);
    void preflightChecked();

    void uploadSpeedChanged();
    void downloadSpeedChanged();

public slots:
    void interrupt();

private slots:
    void fetchServers();
    void findBestServer();
    void testJitter();
    void preflightTest();
    void downloadSpeedTest();
    void uploadSpeedTest();
    void handleDownloadSpeed(double speed);
    void handleUploadSpeed(double speed);

private:
    QNetworkReply *get(const QString url);

    static QMap<QString, QString> parseQueryString(const QString& query);

    bool compareVersion(const QString serverVersion);

    bool testLatency(SpeedTestClient& client, int sample_size, long& latency);
    bool jitter(const ServerInfo& server, long& result, int sample = 40);
    void downloadTest(const ServerInfo& server, const TestConfig& config);
    void uploadTest(const ServerInfo& server, const TestConfig& config);

    const ServerInfo findBestServerWithin(const QVector<ServerInfo> &serverList, long& latency, int sample_size = 5);
    double calculateSpeed(QVector<double> results);
    void execute(const ServerInfo &server, const TestConfig &config, const opFn &fnc, const callFn &cfunc);

    IPInfo m_ipInfo;
    ServerInfo m_bestServer;
    QVector<ServerInfo> m_serverList;

    TestConfig m_uploadConfig;
    TestConfig m_downloadConfig;
    int m_threadsFinished = 0;
    long m_latency;
    double m_downloadSpeed;
    double m_uploadSpeed;
    bool m_isInterruptRequested { false };

    QString m_minSupportedServer;

    QNetworkAccessManager m_QNAM;
};
