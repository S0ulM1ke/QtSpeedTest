#include "src/TestConfigTemplate.h"
#define _USE_MATH_DEFINES

#include "SpeedTestClient.h"
#include "SpeedTestConfig.h"

#include <cmath>
#include <algorithm>
#include "SpeedTest.h"

#include <QThread>
#include <QThreadPool>
#include <QMutex>
#include <QUrlQuery>
#include <QGeoCoordinate>

namespace CONSTANTS {
const int SAMPLE_SIZE = 10;
}

SpeedTest::SpeedTest(float minServerVersion):
        mLatency(0),
        mUploadSpeed(0),
        mDownloadSpeed(0) {
    mIpInfo = IPInfo();
    mServerList = QVector<ServerInfo>();
    mMinSupportedServer = minServerVersion;

    QObject::connect(&m_QNAM, &QNetworkAccessManager::finished, this, [this]() {
        qDebug() << "reply recieved";

        m_QNAM.clearAccessCache();
    });
}

SpeedTest::~SpeedTest() {
    //curl_global_cleanup();

    mServerList.clear();
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

const QVector<ServerInfo> &SpeedTest::serverList() {
    // if (!mServerList.empty())
    //     return mServerList;

    // int http_code = 0;
    // if (fetchServers(SPEED_TEST_SERVER_LIST_URL, mServerList, http_code) && http_code == 200){
    //     return mServerList;
    // }
    return mServerList;
}


const ServerInfo SpeedTest::bestServer(const int sample_size) {
    auto best = findBestServerWithin(serverList(), mLatency, sample_size);
    SpeedTestClient client = SpeedTestClient(best);
    testLatency(client, SPEED_TEST_LATENCY_SAMPLE_SIZE, mLatency);
    client.close();
    return best;
}

/* bool SpeedTest::setServer(ServerInfo& server){
    SpeedTestClient client = SpeedTestClient(server);
    if (client.connect() && client.version() >= mMinSupportedServer){
        if (!testLatency(client, SPEED_TEST_LATENCY_SAMPLE_SIZE, mLatency)){
            return false;
        }
    } else {
        client.close();
        return false;
    }
    client.close();
    return true;

} */

bool SpeedTest::downloadSpeed(const ServerInfo &server, const TestConfig &config, double& result) {
    opFn pfunc = &SpeedTestClient::download;
    mDownloadSpeed = execute(server, config, pfunc);
    result = mDownloadSpeed;
    return true;
}

bool SpeedTest::uploadSpeed(const ServerInfo &server, const TestConfig &config, double& result) {
    opFn pfunc = &SpeedTestClient::upload;
    mUploadSpeed = execute(server, config, pfunc);
    result = mUploadSpeed;
    return true;
}

const long &SpeedTest::latency() {
    return mLatency;
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

// private

double SpeedTest::execute(const ServerInfo &server, const TestConfig &config, const opFn &pfunc) {
    QThreadPool pool;
    pool.setMaxThreadCount(config.concurrency);
    double overall_speed = 0;
    QMutex mtx;

    for (int i = 0; i < config.concurrency; i++) {


        pool.start([=, &overall_speed, &mtx](){
            long start_size = config.start_size;
            long max_size   = config.max_size;
            long incr_size  = config.incr_size;
            long curr_size  = start_size;

            auto spClient = SpeedTestClient(server);

            if (spClient.connect()) {
                long total_size = 0;
                long total_time = 0;
                auto start = std::chrono::steady_clock::now();
                QVector<double> partial_results;
                while (curr_size < max_size){
                    long op_time = 0;
                    if ((spClient.*pfunc)(curr_size, config.buff_size, op_time)) {
                        total_size += curr_size;
                        total_time += op_time;
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

/* CURLcode SpeedTest::httpGet(const std::string &url, std::stringstream &ss, CURL *handler, long timeout) {

    CURLcode code(CURLE_FAILED_INIT);
    CURL* curl = SpeedTest::curl_setup(handler);


    if (curl){
        if (CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FILE, &ss))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, this->strict_ssl_verify))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str()))) {
            code = curl_easy_perform(curl);
        }
        if (handler == nullptr)
            curl_easy_cleanup(curl);
    }
    return code;
} */

QNetworkReply *SpeedTest::get(const QString url)
{
    QNetworkRequest request(url);
    //request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    request.setRawHeader("User-Agent", SPEED_TEST_USER_AGENT);


    request.setTransferTimeout(15000);

    return m_QNAM.get(request);
}

/* CURLcode SpeedTest::httpPost(const std::string &url, const std::string &postdata, std::stringstream &os, void *handler, long timeout) {

    CURLcode code(CURLE_FAILED_INIT);
    CURL* curl = SpeedTest::curl_setup(handler);

    if (curl){
        if (CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FILE, &os))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str()))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, this->strict_ssl_verify))
            && CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str()))) {
            code = curl_easy_perform(curl);
        }
        if (handler == nullptr)
            curl_easy_cleanup(curl);
    }
    return code;
} */

/* CURL *SpeedTest::curl_setup(CURL *handler) {
    CURL* curl = handler == nullptr ? curl_easy_init() : handler;
    if (curl){
        if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeFunc) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_USERAGENT, SPEED_TEST_USER_AGENT) == CURLE_OK){
            return curl;
        } else {
            curl_easy_cleanup(handler);
            return nullptr;
        }
    }
    return nullptr;


} */

size_t SpeedTest::writeFunc(void *buf, size_t size, size_t nmemb, void *userp) {
    if (userp) {
        QByteArray *byteArray = static_cast<QByteArray *>(userp);
        qint64 len = size * nmemb;
        QDataStream stream(byteArray, QIODevice::WriteOnly);
        stream.writeRawData(static_cast<char *>(buf), len);
        return static_cast<size_t>(len);
    }
    return 0;
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

QString SpeedTest::getXmlString(QString xml, QString tagName)
{
    QXmlStreamReader xmlReader(xml);

    while (!xmlReader.atEnd())
    {
        auto token = xmlReader.readNext();
        if (token != QXmlStreamReader::StartElement)
        {
            continue;
        }

        auto name = xmlReader.name();
        if (name == tagName)
        {
            return xmlReader.readElementText();
        }
    }

    return nullptr;
}

void SpeedTest::getIpInfo()
{
    auto reply = get(SPEED_TEST_IP_INFO_API_URL);
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        auto data = reply->readAll();
        reply->deleteLater();
        auto values = SpeedTest::parseQueryString(QString(data));

        mIpInfo.ip_address = values["ip_address"];
        mIpInfo.isp = values["isp"];
        mIpInfo.lat = values["lat"].toFloat();
        mIpInfo.lon = values["lon"].toFloat();

        emit ipInfoReceived();
        ///emit sessionDataReceived(data);
    });
    /* std::stringstream oss;
    auto code = httpGet(SPEED_TEST_IP_INFO_API_URL, oss);
    if (code == CURLE_OK){
        auto values = SpeedTest::parseQueryString(oss.str());
        mIpInfo = IPInfo();
        mIpInfo.ip_address = values["ip_address"];
        mIpInfo.isp = values["isp"];
        mIpInfo.lat = std::stof(values["lat"]);
        mIpInfo.lon = std::stof(values["lon"]);
        values.clear();
        oss.clear();
        info = mIpInfo;
        return true;
    }

    return false; */
}

IPInfo SpeedTest::ipInfo() {
    return mIpInfo;
}

void SpeedTest::fetchServers() {
    // std::stringstream oss;
    // target.clear();

    // auto isHttpSchema = url.find_first_of("http") == 0;

    //CURL* curl = curl_easy_init();
    // auto cres = httpGet(url, oss, curl, 20);

    // if (cres != CURLE_OK)
    //     return false;

    // if (isHttpSchema) {
    //     int req_status;
    //     curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &req_status);
    //     http_code = req_status;

    //     if (http_code != 200){
    //         curl_easy_cleanup(curl);
    //         return false;
    //     }
    // } else {
    //     http_code = 200;
    // }

    //mServerList

    auto reply = get(SPEED_TEST_SERVER_LIST_URL);
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        QString data = reply->readAll();
        reply->deleteLater();
        QXmlStreamReader xmlReader(data);

        //qDebug() << data;

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
            servers.last().distance = serverCoord.distanceTo(QGeoCoordinate(mIpInfo.lat, mIpInfo.lon));
        }

        if (servers.isEmpty()) {
            qCritical() << "failed to fetch servers";
        }

        std::sort(servers.begin(), servers.end(), [](const ServerInfo &a, const ServerInfo &b) -> bool {
            return a.distance < b.distance;
        });

        mServerList.clear();
        mServerList = servers;

        emit serversFetched();

        ///emit sessionDataReceived(data);
    });

}

void SpeedTest::findBestServer()
{
    int sample_size = CONSTANTS::SAMPLE_SIZE;
    auto best = findBestServerWithin(serverList(), mLatency, sample_size);
    SpeedTestClient client = SpeedTestClient(best);
    testLatency(client, SPEED_TEST_LATENCY_SAMPLE_SIZE, mLatency);
    client.close();

    m_bestServer = best;

    qDebug() << "Found best server:" << m_bestServer.host;
    qDebug() << "Distance:" << m_bestServer.distance;
    qDebug() << "Lattency:" << mLatency;

    emit bestServerFound();
    // int i = CONSTANTS::SAMPLE_SIZE;
    // ServerInfo bestServer = mServerList.first();

    // mLatency = INT_MAX;

    // for (auto &server : mServerList){
    //     auto client = SpeedTestClient(server);

    //     if (!client.connect()){
    //         // if (cb)
    //         //     cb(false);
    //         continue;
    //     }

    //     if (client.version() < mMinSupportedServer){
    //         client.close();
    //         continue;
    //     }

    //     long current_latency = LONG_MAX;
    //     if (testLatency(client, 20, current_latency)){
    //         if (current_latency < mLatency){
    //             mLatency = current_latency;
    //             bestServer = server;
    //         }
    //     }
    //     client.close();

    //     if (i-- < 0){
    //         break;
    //     }

    // }
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
    double preSpeed = 0;
    if (!downloadSpeed(m_bestServer, preflightConfigDownload, preSpeed)) {
        qDebug() << "Pre flight failed";
        return;
    }

    testConfigSelector(preSpeed, m_uploadConfig, m_downloadConfig);

    qDebug() << m_downloadConfig.label;

    emit preflightChecked();
}

void SpeedTest::downloadSpeedTest()
{
    double speed = 0;
    if (!downloadSpeed(m_bestServer, m_downloadConfig, speed)) {
        qDebug() << "Download test failed";
        return;
    }

    qDebug() << "Donwload speed:" << speed;
}

const ServerInfo SpeedTest::findBestServerWithin(const QVector<ServerInfo> &serverList, long &latency,
                                                 const int sample_size) {
    int i = sample_size;
    ServerInfo bestServer = serverList.first();

    latency = INT_MAX;

    for (auto &server : serverList){
        auto client = SpeedTestClient(server);

        if (!client.connect()){
            // if (cb)
            //     cb(false);
            qDebug() << "Failed to connect";
            continue;
        }

        // if (client.version() < mMinSupportedServer){
        //     qDebug() << "Server version fail";
        //     qDebug() << "Client:" << client.version() << "mMinSupportedServer" << mMinSupportedServer;
        //     client.close();
        //     continue;
        // }

        long current_latency = LONG_MAX;
        if (testLatency(client, 20, current_latency)){
            if (current_latency < latency){
                latency = current_latency;
                bestServer = server;
            }
        }
        client.close();
        // if (cb)
        //     cb(true);

        if (i-- < 0){
            break;
        }

    }
    return bestServer;
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
    // if (!client.connect()){
    //     return false;
    // }
    // latency = INT_MAX;
    // long temp_latency = 0;
    // for (int i = 0; i < sample_size; i++){
    //     if (client.ping(temp_latency)){
    //         if (temp_latency < latency){
    //             latency = temp_latency;
    //         }
    //     } else {
    //         return false;
    //     }
    // }
    // return true;
}
