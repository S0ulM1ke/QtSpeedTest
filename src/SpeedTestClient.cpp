#include "SpeedTestClient.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>

SpeedTestClient::SpeedTestClient(const ServerInfo &serverInfo): m_serverInfo(serverInfo),
    m_serverVersion("-1.0")
{
    m_socket = new QTcpSocket(this);
    QObject::connect(m_socket, &QTcpSocket::errorOccurred, this, [this]{
        qDebug() << "Socket error:" << m_socket->errorString() << m_socket->error();
    });
}
SpeedTestClient::~SpeedTestClient() {
    close();
}

// It connects and initiates client/server handshaking
bool SpeedTestClient::connect() {

    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState){
        return true;
    }

    auto ret = mkSocket();
    if (!ret) {
        qDebug() << "Socket connection timeout";
        return ret;
    }


    if (!writeLine("HI")){
        close();
        qDebug() << "No answer to 'HI'";
        return false;
    }
    
    
    QString reply;
    if (SpeedTestClient::readLine(m_socket, reply)) {
        QTextStream replyStream(&reply);
        QString hello;
        QString serverVersion;
        replyStream >> hello >> serverVersion;
        if (replyStream.status() != QTextStream::Ok) {
            qDebug() << "Text stream fail, answer:" << hello;
            close();
            return false;
        }

        if (!reply.isEmpty() && hello == "HELLO") {
            m_serverVersion = serverVersion;
            return true;
        }
    }

    close();
    return false;
}

void SpeedTestClient::connectToSocket()
{
    auto ctx = new QObject;
    QObject::connect(m_socket, &QTcpSocket::connected, ctx, [this]{
        if (!writeLine("HI")){
            close();
            return;
        }
    });
    QObject::connect(m_socket, &QTcpSocket::readyRead, ctx, [ctx, this]{
        m_socket->disconnect(ctx);
        ctx->deleteLater();

        auto data = m_socket->readAll();
        QString hello;
        QString serverVersion;
        QTextStream replyStream(data);
        replyStream >> hello >> serverVersion;

        if (!data.isEmpty() && hello == "HELLO") {
            m_serverVersion = serverVersion;
            emit socketConnected();
        }

    });
    auto hostPort = hostport();
    m_socket->connectToHost(hostPort.first, hostPort.second);
}

// It executes PING command
bool SpeedTestClient::ping(long &millisec) {
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        qDebug() << "Ping failed - socket uninitialized";
        return false;
    }

    QElapsedTimer timer;
    QString cmd = QString("PING %1").arg(timer.nsecsElapsed()); // Using high-resolution timer

    timer.start();
    if (!writeLine(cmd)) {
        close();
        qDebug() << "Ping failed - socket write issue";
        return false;
    }

    QString reply;
    if (readLine(m_socket ,reply)) {
        if (reply.startsWith("PONG ")) {
            millisec = timer.elapsed(); // Milliseconds since start
            return true;
        }
    }

    qDebug() << "Ping failed - unable to read";

    close();
    return false;
}

// It executes DOWNLOAD command
void SpeedTestClient::download(const long size, const long chunk_size)
{
    Q_UNUSED(chunk_size)
    auto totalReceived = QSharedPointer<long>::create(0);
    auto timer = QSharedPointer<QElapsedTimer>::create();

    auto ctx = new QObject;
    QObject::connect(m_socket, &QTcpSocket::readyRead, ctx, [ctx, this, totalReceived, size, timer]{
        auto data = m_socket->readAll();
        *totalReceived += data.size();
        if (*totalReceived >= size) {
            double millisec = timer->elapsed();
            emit opFinisfed(millisec);
            ctx->deleteLater();
        }
    });

    timer->start();
    QString cmd = QString("DOWNLOAD %1").arg(size);
    if (!writeLine(cmd)) {
        qDebug() << "Download fail - unable to write";
        return;
    }
}

// It executes UPLOAD command
void SpeedTestClient::upload(const long size, const long chunk_size)
{
    QString cmd = QString("UPLOAD %1\n").arg(size);

    // Write the command to the socket
    if (!SpeedTestClient::writeLine(cmd)) {
        return;
    }

    QByteArray data;
    data.fill('1', chunk_size);


    auto totalReceived = QSharedPointer<long>::create(0);
    auto timer = QSharedPointer<QElapsedTimer>::create();

    auto ctx = new QObject;
    QObject::connect(m_socket, &QTcpSocket::bytesWritten, ctx, [ctx, this, totalReceived, data, size, chunk_size, timer](quint64 bytes) mutable{
        //auto data = m_socket->readAll();
        qDebug() << "Wtitten" << bytes << "left:" << size - *totalReceived;
        *totalReceived += bytes;
        if (*totalReceived < size) {
            qint64 nextChunkSize = qMin(chunk_size, size - *totalReceived);
            QByteArray nextChunk = data.left(nextChunkSize);
            m_socket->write(nextChunk);
            //m_socket->write(data);
        } else {
            qDebug() << "finished";
            double millisec = timer->elapsed();
            emit opFinisfed(millisec);
            ctx->deleteLater();
        }
    });

    timer->start();
    qint64 initialChunkSize = qMin(chunk_size, size);
    QByteArray initialChunk = data.left(initialChunkSize);
    qint64 bytesWritten = m_socket->write(initialChunk);
    if (bytesWritten <= 0) {
        // If write fails, exit early
        return;
    }
}

QString SpeedTestClient::version() {
    return m_serverVersion;
}

const std::pair<QString, int> SpeedTestClient::hostport() {
    QString targetHost = m_serverInfo.host;
    QStringList hostPort = targetHost.split(':');

    return std::pair<QString, int>(hostPort[0], hostPort[1].toInt());
}

// It closes a connection
void SpeedTestClient::close() {
    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState){
        SpeedTestClient::writeLine("QUIT");
        m_socket->close();
        m_socket->deleteLater();
    }

}

bool SpeedTestClient::mkSocket() {
    if (!m_socket) {
        m_socket = new QTcpSocket(this); // Ensure m_socket is initialized
    }

    // Check if the socket is already connected or is in the process of connecting
    if (m_socket->state() == QTcpSocket::ConnectedState ||
        m_socket->state() == QTcpSocket::ConnectingState) {
        return true;
    }

    auto hostPort = hostport();
    m_socket->connectToHost(hostPort.first, hostPort.second);

    // Use a reasonable timeout for the connection attempt
    const int timeoutMs = 3000; // 3 seconds timeout
    return m_socket->waitForConnected(timeoutMs);
}

bool SpeedTestClient::readLine(QTcpSocket *socket, QString &buffer) {
    if (!socket || socket->state() != QTcpSocket::ConnectedState)
        return false;

    buffer.clear(); // Clear the buffer to ensure it's empty before reading

    if (!socket->waitForReadyRead()) {
        return false;
    }

    // Try to read a line from the socket
    // QIODevice::readLine() reads until a newline character is encountered
    QByteArray line = socket->readLine();
    if (line.isEmpty()) {
        // If no data was read, return false
        return false;
    }

    // Convert the QByteArray to QString and remove any trailing newline characters
    buffer = QString::fromUtf8(line).trimmed();

    return true;
}

bool SpeedTestClient::writeLine(const QString &buffer) {
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState)
        return false;

    // Check if the buffer is empty
    if (buffer.isEmpty())
        return false;

    QString buffCopy = buffer;
    // Ensure the buffer ends with a newline character
    if (!buffCopy.endsWith('\n')) {
        buffCopy += '\n';
    }

    // Convert QString to QByteArray for writing
    QByteArray data = buffCopy.toUtf8();
    qint64 len = data.size();
    qint64 n = m_socket->write(data);

    // Check if the number of bytes written matches the expected length
    return n == len;
}
