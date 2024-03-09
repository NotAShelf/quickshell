#include "socket.hpp"
#include <utility>

#include <qfile.h>
#include <qlocalserver.h>
#include <qlocalsocket.h>
#include <qlogging.h>
#include <qobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qtmetamacros.h>

#include "datastream.hpp"

void Socket::setSocket(QLocalSocket* socket) {
	if (this->socket != nullptr) this->socket->deleteLater();
	this->socket = socket;

	if (socket != nullptr) {
		socket->setParent(this);

		// clang-format off
		QObject::connect(this->socket, &QLocalSocket::connected, this, &Socket::onSocketConnected);
		QObject::connect(this->socket, &QLocalSocket::disconnected, this, &Socket::onSocketDisconnected);
		QObject::connect(this->socket, &QLocalSocket::errorOccurred, this, &Socket::error);
		QObject::connect(this->socket, &QLocalSocket::readyRead, this, &DataStream::onBytesAvailable);
		// clang-format on

		if (socket->isOpen()) this->onSocketConnected();
	}
}

QString Socket::path() const { return this->mPath; }

void Socket::setPath(QString path) {
	if ((this->connected && !this->disconnecting) || path == this->mPath) return;
	this->mPath = std::move(path);
	emit this->pathChanged();

	if (this->targetConnected && this->socket == nullptr) this->connectPathSocket();
}

void Socket::onSocketConnected() {
	this->buffer.clear();
	this->connected = true;
	this->targetConnected = false;
	this->disconnecting = false;
	emit this->connectionStateChanged();
}

void Socket::onSocketDisconnected() {
	this->connected = false;
	this->disconnecting = false;
	this->socket->deleteLater();
	this->socket = nullptr;
	this->buffer.clear();
	emit this->connectionStateChanged();

	if (this->targetConnected) this->connectPathSocket();
}

bool Socket::isConnected() const { return this->connected; }

void Socket::setConnected(bool connected) {
	this->targetConnected = connected;

	if (!connected) {
		if (this->socket != nullptr && !this->disconnecting) {
			this->disconnecting = true;
			this->socket->disconnectFromServer();
		}
	} else if (this->socket == nullptr) this->connectPathSocket();
}

QIODevice* Socket::ioDevice() const { return this->socket; }

void Socket::connectPathSocket() {
	if (!this->mPath.isEmpty()) {
		auto* socket = new QLocalSocket();
		socket->setServerName(this->mPath);
		this->setSocket(socket);
		this->socket->connectToServer(QIODevice::ReadWrite);
	}
}

void Socket::write(const QString& data) {
	if (this->socket != nullptr) {
		this->socket->write(data.toUtf8());
	}
}

SocketServer::~SocketServer() { this->disableServer(); }

void SocketServer::onPostReload() {
	this->postReload = true;
	if (this->isActivatable()) this->enableServer();
}

bool SocketServer::isActive() const { return this->server != nullptr; }

void SocketServer::setActive(bool active) {
	this->activeTarget = active;
	if (active == (this->server != nullptr)) return;

	if (active) {
		if (this->isActivatable()) this->enableServer();
	} else this->disableServer();
}

QString SocketServer::path() const { return this->mPath; }

void SocketServer::setPath(QString path) {
	if (this->mPath == path) return;
	this->mPath = std::move(path);
	emit this->pathChanged();

	if (this->isActivatable()) this->enableServer();
}

QQmlComponent* SocketServer::handler() const { return this->mHandler; }

void SocketServer::setHandler(QQmlComponent* handler) {
	if (this->mHandler != nullptr) this->mHandler->deleteLater();
	this->mHandler = handler;

	if (handler != nullptr) {
		handler->setParent(this);
	}
}

bool SocketServer::isActivatable() {
	return this->server == nullptr && this->postReload && this->activeTarget && !this->mPath.isEmpty()
	    && this->handler() != nullptr;
}

void SocketServer::enableServer() {
	this->disableServer();

	this->server = new QLocalServer(this);
	QObject::connect(
	    this->server,
	    &QLocalServer::newConnection,
	    this,
	    &SocketServer::onNewConnection
	);

	if (!this->server->listen(this->mPath)) {
		qWarning() << "could not start socket server at" << this->mPath;
		this->disableServer();
	}

	this->activeTarget = false;
	emit this->activeStatusChanged();
}

void SocketServer::disableServer() {
	auto wasActive = this->server != nullptr;

	if (this->server != nullptr) {
		for (auto* socket: this->mSockets) {
			socket->deleteLater();
		}

		this->mSockets.clear();
		this->server->deleteLater();
		this->server = nullptr;
	}

	if (this->mPath != nullptr) {
		if (QFile::exists(this->mPath) && !QFile::remove(this->mPath)) {
			qWarning() << "failed to delete socket file at" << this->mPath;
		}
	}

	if (wasActive) emit this->activeStatusChanged();
}

void SocketServer::onNewConnection() {
	if (auto* connection = this->server->nextPendingConnection()) {
		auto* instanceObj = this->mHandler->create(QQmlEngine::contextForObject(this->mHandler));
		auto* instance = qobject_cast<Socket*>(instanceObj);

		if (instance == nullptr) {
			qWarning() << "SocketServer.handler does not create a Socket. Dropping connection.";
			if (instanceObj != nullptr) instanceObj->deleteLater();
			connection->deleteLater();
			return;
		}

		QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);

		this->mSockets.append(instance);
		instance->setParent(this);

		if (instance->isConnected()) {
			qWarning() << "SocketServer.handler created a socket with an existing connection. Dropping "
			              "new connection.";
			connection->deleteLater();
		} else {
			instance->setSocket(connection);
		}
	}
}
