#include "SpeedTestClient.h"

#include <QElapsedTimer>
#include <QRandomGenerator>

#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

SpeedTestClient::SpeedTestClient(const ServerInfo &serverInfo): mServerInfo(serverInfo),
                                                                                  mServerVersion(-1.0){
    m_socket = new QTcpSocket(this);
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
        qDebug() << "Possible timeout";
        return ret;
    }


    if (!SpeedTestClient::writeLine(m_socket, "HI")){
        close();
        qDebug() << "No answer to 'HI'";
        return false;
    }
    
    
    QString reply;
    if (SpeedTestClient::readLine(m_socket, reply)) {
        QTextStream replyStream(&reply);
        QString hello;
        float serverVersion;
        replyStream >> hello >> serverVersion;
        if (replyStream.status() != QTextStream::Ok) {
            qDebug() << "Text stream fail, answer:" << hello;
            close();
            return false;
        }

        if (!reply.isEmpty() && hello == "HELLO") {
            mServerVersion = serverVersion; // Ensure mServerVersion is an appropriate type
            return true;
        }
    }

    close();
    return false;
}

// It closes a connection
void SpeedTestClient::close() {
    if (m_socket && m_socket->state() == QTcpSocket::ConnectedState){
        SpeedTestClient::writeLine(m_socket, "QUIT");
        m_socket->close();
    }

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
    if (!writeLine(m_socket, cmd)) {
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
bool SpeedTestClient::download(const long size, const long chunk_size, long &millisec) {
    QString cmd = QString("DOWNLOAD %1").arg(size);
    if (!writeLine(m_socket, cmd)) {
        qDebug() << "Download fail - unable to write";
        return false;
    }

    QByteArray buffer; // Use QByteArray for ease of use and safety
    buffer.resize(chunk_size);

    long totalReceived = 0;
    QElapsedTimer timer;
    timer.start();

    while (totalReceived < size) {
        if (!m_socket->waitForReadyRead(10000)) {
            qDebug() << "Download fail - timeout waiting for data";
            return false;
        }

        while (m_socket->bytesAvailable() > 0 && totalReceived < size) {
            qint64 bytesReceived = m_socket->read(buffer.data(), qMin(static_cast<long>(chunk_size), size - totalReceived));
            if (bytesReceived < 0) {
                qDebug() << "Download fail - read error";
                return false;
            }
            totalReceived += bytesReceived;
        }
    }

    millisec = timer.elapsed();
    return true;
}

// It executes UPLOAD command
bool SpeedTestClient::upload(const long size, const long chunk_size, long &millisec) {
    QString cmd = QString("UPLOAD %1\n").arg(size);

    // Write the command to the socket
    if (!SpeedTestClient::writeLine(m_socket, cmd)) {
        return false;
    }

    QByteArray buffer;
    buffer.resize(chunk_size); // Pre-allocate the buffer to the chunk size

    // Fill the buffer with random data
    std::generate(buffer.begin(), buffer.end(), []() -> char {
        return static_cast<char>(QRandomGenerator::global()->bounded(256));
    });

    long missing = size - cmd.length(); // Adjust for the length of the command sent
    QElapsedTimer timer;
    timer.start(); // Start timing the upload operation

    while (missing > 0) {
        qint64 toWrite = qMin(missing, static_cast<long>(chunk_size));
        if (toWrite < chunk_size) {
            // Resize buffer for the last chunk if it's smaller than the chunk size
            buffer.resize(toWrite);
            buffer[toWrite - 1] = '\n'; // Ensure the last byte is a newline character
        }

        qint64 bytesWritten = m_socket->write(buffer.constData(), toWrite);
        if (bytesWritten <= 0) {
            // If write fails, exit early
            return false;
        }
        m_socket->waitForBytesWritten(); // Ensure data is written before continuing
        missing -= bytesWritten;
    }

    QString reply;
    if (!SpeedTestClient::readLine(m_socket, reply)) {
        return false;
    }

    millisec = timer.elapsed(); // Calculate the elapsed time in milliseconds

    // Check if the server's response starts with the expected acknowledgement
    QString expectedReplyStart = QString("OK %1 ").arg(size);
    return reply.startsWith(expectedReplyStart);

    /* std::stringstream cmd;
    cmd << "UPLOAD " << size << "\n";
    auto cmd_len = cmd.str().length();

    char *buff = new char[chunk_size];
    for(size_t i = 0; i < static_cast<size_t>(chunk_size); i++)
        buff[i] = static_cast<char>(rand() % 256);

    long missing = size;
    auto start = std::chrono::steady_clock::now();

    if (!SpeedTestClient::writeLine(m_socket, cmd.str())){
        delete[] buff;
        return false;
    }

    ssize_t w = cmd_len;
    missing -= w;

    while(missing > 0){
        if (missing - chunk_size > 0){
            //w = _write(mSocketFd, buff, static_cast<size_t>(chunk_size));
            //w = send(mSocketFd, buff, static_cast<size_t>(chunk_size), 0);
            w = m_socket->write(buff, static_cast<size_t>(chunk_size));
            if (w != chunk_size){
                delete[] buff;
                return false;
            }
            missing -= w;
        } else {
            buff[missing - 1] = '\n';
            //w = _write(mSocketFd, buff, static_cast<size_t>(missing));
            //w = send(mSocketFd, buff, static_cast<size_t>(missing), 0);
            w = m_socket->write(buff, static_cast<size_t>(missing));
            if (w != missing){
                delete[] buff;
                return false;
            }
            missing -= w;
        }

    }
    QString reply;
    if (!SpeedTestClient::readLine(m_socket, reply)){
        delete[] buff;
        return false;
    }
    auto stop = std::chrono::steady_clock::now();

    std::stringstream ss;
    ss << "OK " << size << " ";
    millisec = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    delete[] buff;
    return reply.substr(0, ss.str().length()) == ss.str(); */

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


float SpeedTestClient::version() {
    return mServerVersion;
}

const std::pair<QString, int> SpeedTestClient::hostport() {
    QString targetHost = mServerInfo.host;
    QStringList hostPort = targetHost.split(':');

    return std::pair<QString, int>(hostPort[0], hostPort[1].toInt());
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

bool SpeedTestClient::writeLine(QTcpSocket *socket, const QString &buffer) {
    if (!socket) // Check if the socket is valid
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
    qint64 n = socket->write(data);

    // Check if the number of bytes written matches the expected length
    return n == len;
}
