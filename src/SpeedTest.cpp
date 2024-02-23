#define _USE_MATH_DEFINES

#include "TestConfigTemplate.h"
#include "SpeedTestClient.h"
#include "SpeedTest.h"

#include <cmath>
#include <algorithm>
#include <QSysInfo>
#include <QtConcurrent>
#include <QThread>
#include <QThreadPool>
#include <QMutex>
#include <QUrlQuery>
#include <QGeoCoordinate>

namespace Constants {
const char *USER_AGENT = "Mozilla/5.0";
const char *IP_INFO_URL = "http://speedtest.ookla.com/api/ipaddress.php";
const char *SERVER_LIST_URL = "https://www.speedtest.net/speedtest-servers.php";
const char *MIN_SERVER_VERSION = "2.3";
const int LATENCY_SAMPLE_SIZE = 80;
const int SAMPLE_SIZE = 10;
}

SpeedTest::SpeedTest():
    m_latency(0),
    m_downloadSpeed(0.0),
    m_uploadSpeed(0.0)
{
    m_ipInfo = IPInfo();
    m_serverList = QVector<ServerInfo>();
    m_minSupportedServer = Constants::MIN_SERVER_VERSION;

    QObject::connect(&m_QNAM, &QNetworkAccessManager::finished, this, [this]() {
        qDebug() << "reply recieved";

        m_QNAM.clearAccessCache();
    });
}

SpeedTest::~SpeedTest() {
    m_serverList.clear();
}

void SpeedTest::initialize()
{
    QObject::connect(this, &SpeedTest::ipInfoReceived, this, &SpeedTest::fetchServers);
    QObject::connect(this, &SpeedTest::serversFetched, this, &SpeedTest::findBestServer);
    QObject::connect(this, &SpeedTest::bestServerFound, this, &SpeedTest::testJitter);
    QObject::connect(this, &SpeedTest::jitterChecked, this, &SpeedTest::preflightTest);
    QObject::connect(this, &SpeedTest::preflightChecked, this, &SpeedTest::downloadSpeedTest);
    getIpInfo();
}

void SpeedTest::getIpInfo()
{
    auto reply = get(Constants::IP_INFO_URL);
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Get IP info failed: " << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toString();
            reply->deleteLater();
            return;
        }
        auto data = reply->readAll();
        reply->deleteLater();
        auto values = SpeedTest::parseQueryString(QString(data));

        m_ipInfo.ip_address = values["ip_address"];
        m_ipInfo.isp = values["isp"];
        m_ipInfo.lat = values["lat"].toFloat();
        m_ipInfo.lon = values["lon"].toFloat();

        emit ipInfoReceived();
    });
}

const IPInfo SpeedTest::ipInfo() {
    return m_ipInfo;
}

const QVector<ServerInfo> &SpeedTest::serverList() {
    return m_serverList;
}

const ServerInfo SpeedTest::bestServer(const int sample_size) {
    auto best = findBestServerWithin(serverList(), m_latency, sample_size);
    SpeedTestClient client = SpeedTestClient(best);
    testLatency(client, Constants::LATENCY_SAMPLE_SIZE, m_latency);
    client.close();
    return best;
}

double SpeedTest::downloadSpeed() const
{
    return m_downloadSpeed;
}

void SpeedTest::setDownloadSpeed(double speed)
{
    if (m_downloadSpeed == speed)
        return;
    m_downloadSpeed = speed;
    emit downloadSpeedChanged();
}

double SpeedTest::uploadSpeed() const
{
    return m_uploadSpeed;
}

void SpeedTest::setUploadSpeed(double speed)
{
    if (m_uploadSpeed == speed)
        return;
    m_uploadSpeed = speed;
    emit uploadSpeedChanged();
}

const long &SpeedTest::latency() {
    return m_latency;
}

// private slots

void SpeedTest::fetchServers()
{
    auto reply = get(Constants::SERVER_LIST_URL);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Fetch servers failed: " << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toString();
            reply->deleteLater();
            return;
        }
        QString data = reply->readAll();
        reply->deleteLater();

        QXmlStreamReader xmlReader(data);
        QVector<ServerInfo> servers;

        while (!xmlReader.atEnd() && !xmlReader.error()) {
            auto token = xmlReader.readNext();

            if (token == QXmlStreamReader::Invalid) {
                qDebug() << xmlReader.errorString();
                continue;
            }

            if (token != QXmlStreamReader::StartElement) {
                continue;
            }

            if (xmlReader.name() != QString("server")) {
                continue;
            }

            servers.append(ServerInfo());

            servers.last().url          = xmlReader.attributes().value("url").toString();
            servers.last().lat          = xmlReader.attributes().value("lat").toFloat();
            servers.last().lon          = xmlReader.attributes().value("lon").toFloat();
            servers.last().name         = xmlReader.attributes().value("name").toString();
            servers.last().country      = xmlReader.attributes().value("country").toString();
            servers.last().country_code = xmlReader.attributes().value("cc").toString();
            servers.last().host         = xmlReader.attributes().value("host").toString();
            servers.last().id           = xmlReader.attributes().value("id").toInt();
            servers.last().sponsor      = xmlReader.attributes().value("sponsor").toString();

            QGeoCoordinate serverCoord(servers.last().lat, servers.last().lon);
            servers.last().distance = serverCoord.distanceTo(QGeoCoordinate(m_ipInfo.lat, m_ipInfo.lon));
        }

        if (servers.isEmpty()) {
            qCritical() << "failed to fetch servers";
        }

        std::sort(servers.begin(), servers.end(), [](const ServerInfo &a, const ServerInfo &b) -> bool {
            return a.distance < b.distance;
        });

        m_serverList.clear();
        m_serverList = servers;

        emit serversFetched();
    });

}

void SpeedTest::findBestServer()
{
    int sample_size = Constants::SAMPLE_SIZE;
    auto best = findBestServerWithin(serverList(), m_latency, sample_size);
    SpeedTestClient client = SpeedTestClient(best);
    testLatency(client, Constants::LATENCY_SAMPLE_SIZE, m_latency);
    client.close();

    m_bestServer = best;

    qDebug() << "Found best server:" << m_bestServer.host;
    qDebug() << "Distance:" << m_bestServer.distance;
    qDebug() << "Latency:" << m_latency;

    emit bestServerFound();
}

void SpeedTest::testJitter()
{
    long jit = 0;
    jitter(m_bestServer, jit);
    qDebug() << "Jitter:" <<jit;

    emit jitterChecked();
}

void SpeedTest::preflightTest()
{
    auto ctx = new QObject;
    QObject::connect(this, &SpeedTest::downloadTestFinished, ctx, [ctx, this](double result){
        this->disconnect(ctx);
        ctx->deleteLater();

        testConfigSelector(result, m_uploadConfig, m_downloadConfig);

        qDebug() << m_downloadConfig.label;

        emit preflightChecked();
    });

    static_cast<void>(QtConcurrent::run(&SpeedTest::downloadTest, this, m_bestServer, preflightConfigDownload));
}

void SpeedTest::downloadSpeedTest()
{
    auto ctx = new QObject;
    QObject::connect(this, &SpeedTest::downloadTestFinished, ctx, [ctx, this](double result){
        this->disconnect(ctx);
        ctx->deleteLater();

        qDebug() << "Donwload speed:" << result;
    });

    static_cast<void>(QtConcurrent::run(&SpeedTest::downloadTest, this, m_bestServer, m_downloadConfig));
}

void SpeedTest::uploadSpeedTest()
{
    auto ctx = new QObject;
    QObject::connect(this, &SpeedTest::uploadTestFinished, ctx, [ctx, this](double result){
        this->disconnect(ctx);
        ctx->deleteLater();

        qDebug() << "Upload speed:" << result;
    });

    static_cast<void>(QtConcurrent::run(&SpeedTest::uploadTest, this, m_bestServer, m_downloadConfig));
}

// private

QNetworkReply *SpeedTest::get(const QString url)
{
    QNetworkRequest request(url);

    QString agent = Constants::USER_AGENT;
    agent.append(" " + QSysInfo::prettyProductName());

    request.setRawHeader("User-Agent", agent.toLocal8Bit());


    request.setTransferTimeout(15000);

    return m_QNAM.get(request);
}

QMap<QString, QString> SpeedTest::parseQueryString(const QString &query) {
    QMap<QString, QString> map;
    QUrlQuery queryUrl(QUrl("?" + query)); // Prepend '?' to make it a valid query string
    auto queryItems = queryUrl.queryItems();

    for (const auto &item : queryItems) {
        map[item.first] = item.second;
    }
    return map;
}

bool SpeedTest::compareVersion(const QString serverVersion)
{
    // Split version strings by '.'
    QStringList first = serverVersion.split('.');
    QStringList second = m_minSupportedServer.split('.');

    // Ensure both versions have major and minor parts
    if (first.size() != 2 || second.size() != 2)
        return false;

    // Extract major and minor versions
    int firstMajor = first[0].toInt();
    int firstMinor = first[1].toInt();
    int secondMajor = second[0].toInt();
    int secondMinor = second[1].toInt();

    // Compare major versions first
    if (firstMajor < secondMajor)
        return true;
    else if (firstMajor > secondMajor)
        return false;

    // If major versions are equal, compare minor versions
    return firstMinor < secondMinor;
}

bool SpeedTest::testLatency(SpeedTestClient &client, const int sample_size, long &latency) {
    if (!client.connect()) {
        qDebug() << "Test lattency failed - unable to connect";
        return false;
    }

    QVector<long> latencies;
    for (int i = 0; i < sample_size; ++i) {
        long temp_latency = 0;
        if (client.ping(temp_latency)) {
            latencies.push_back(temp_latency);
        } else {
            // Consider logging the failure or handling retries
        }
    }

    if (latencies.isEmpty()) {
        return false; // No successful pings
    }

    // Calculate the minimum latency
    auto min_latency_it = std::min_element(latencies.begin(), latencies.end());
    if (min_latency_it != latencies.end()) {
        latency = *min_latency_it;
    } else {
        latency = INT_MAX; // Just a fallback, should not happen due to the isEmpty check above
    }

    return true;
}

bool SpeedTest::jitter(const ServerInfo &server, long& result, const int sample) {
    auto client = SpeedTestClient(server);
    double current_jitter = 0;
    long previous_ms =  LONG_MAX;
    if (client.connect()){
        for (int i = 0; i < sample; i++){
            long ms = 0;
            if (client.ping(ms)){
                if (previous_ms == LONG_MAX) {
                    previous_ms = ms;
                } else {
                    current_jitter += std::abs(previous_ms - ms);
                }
            }
        }
        client.close();
    } else {
        return false;
    }

    result = (long) std::floor(current_jitter / sample);
    return true;
}

void SpeedTest::downloadTest(const ServerInfo &server, const TestConfig &config) {
    double result;
    opFn pfunc = &SpeedTestClient::download;
    result = execute(server, config, pfunc);
    setDownloadSpeed(result);

    emit downloadTestFinished(result);
}

void SpeedTest::uploadTest(const ServerInfo &server, const TestConfig &config) {
    double result;
    opFn pfunc = &SpeedTestClient::upload;
    result = execute(server, config, pfunc);
    setUploadSpeed(result);

    emit uploadTestFinished(result);
}

const ServerInfo SpeedTest::findBestServerWithin(const QVector<ServerInfo> &serverList, long &latency,
                                                 const int sample_size) {
    int i = sample_size;
    ServerInfo bestServer = serverList.first();

    latency = INT_MAX;

    for (auto &server : serverList){
        auto client = SpeedTestClient(server);

        if (!client.connect()){
            qDebug() << "Failed to connect";
            continue;
        }

        if (compareVersion(client.version())){
            qDebug() << "Server version fail";
            qDebug() << "Client:" << client.version() << "mMinSupportedServer" << m_minSupportedServer;
            client.close();
            continue;
        }

        long current_latency = LONG_MAX;
        if (testLatency(client, 20, current_latency)){
            if (current_latency < latency){
                latency = current_latency;
                bestServer = server;
            }
        }
        client.close();

        if (i-- < 0){
            break;
        }

    }
    return bestServer;
}

double SpeedTest::execute(const ServerInfo &server, const TestConfig &config, const opFn &pfunc) {
    QThreadPool pool;
    pool.setMaxThreadCount(config.concurrency);
    double overall_speed = 0;
    QMutex mtx;

    for (int i = 0; i < pool.maxThreadCount(); i++) {
        pool.start([=, &overall_speed, &mtx](){
            long start_size = config.start_size;
            long max_size   = config.max_size;
            long incr_size  = config.incr_size;
            long curr_size  = start_size;

            auto spClient = SpeedTestClient(server);

            if (spClient.connect()) {
                //long total_size = 0;
                //long total_time = 0;
                auto start = std::chrono::steady_clock::now();
                QVector<double> partial_results;
                while (curr_size < max_size){
                    long op_time = 0;
                    if ((spClient.*pfunc)(curr_size, config.buff_size, op_time)) {
                        //total_size += curr_size;
                        //total_time += op_time;
                        double metric = (curr_size * 8) / (static_cast<double>(op_time) / 1000);
                        partial_results.push_back(metric);

                    } else {
                        qDebug() << "Fail";
                    }
                    curr_size += incr_size;
                    auto stop = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count() > config.min_test_time_ms)
                        break;
                }

                spClient.close();
                std::sort(partial_results.begin(), partial_results.end());

                size_t skip = 0;
                size_t drop = 0;
                if (partial_results.size() >= 10){
                    skip = partial_results.size() / 4;
                    drop = 2;
                }

                size_t iter = 0;
                double real_sum = 0;
                for (auto it = partial_results.begin() + skip; it != partial_results.end() - drop; ++it ){
                    iter++;
                    real_sum += (*it);
                }
                mtx.lock();
                overall_speed += (real_sum / iter);
                mtx.unlock();
            } else {
                qDebug() << "Fail";
            }
        });

    }
    pool.waitForDone();

    return overall_speed / 1000 / 1000;
}
