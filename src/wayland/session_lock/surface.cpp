#include "surface.hpp"

#include <private/qwaylandscreen_p.h>
#include <private/qwaylandshellsurface_p.h>
#include <private/qwaylandshmbackingstore_p.h>
#include <private/qwaylandsurface_p.h>
#include <private/qwaylandwindow_p.h>
#include <qlogging.h>
#include <qobject.h>
#include <wayland-client-protocol.h>

#include "lock.hpp"
#include "session_lock.hpp"

QSWaylandSessionLockSurface::QSWaylandSessionLockSurface(QtWaylandClient::QWaylandWindow* window)
    : QtWaylandClient::QWaylandShellSurface(window) {
	auto* qwindow = window->window();
	this->setExtension(LockWindowExtension::get(qwindow));

	if (this->ext == nullptr) {
		qFatal() << "QSWaylandSessionLockSurface created with null LockWindowExtension";
		throw nullptr;
	}

	if (this->ext->lock == nullptr) {
		qFatal() << "QSWaylandSessionLock for QSWaylandSessionLockSurface died";
		throw nullptr;
	}

	wl_output* output = nullptr;
	auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(qwindow->screen()->handle());

	if (waylandScreen != nullptr) {
		output = waylandScreen->output();
	} else {
		qFatal() << "Session lock screen does not corrospond to a real screen. Force closing window";
		throw nullptr;
	}

	this->init(this->ext->lock->get_lock_surface(window->waylandSurface()->object(), output));
}

QSWaylandSessionLockSurface::~QSWaylandSessionLockSurface() {
	if (this->ext != nullptr) this->ext->surface = nullptr;
	this->destroy();
}

bool QSWaylandSessionLockSurface::isExposed() const { return this->configured; }

void QSWaylandSessionLockSurface::applyConfigure() {
	this->window()->resizeFromApplyConfigure(this->size);
}

bool QSWaylandSessionLockSurface::handleExpose(const QRegion& region) {
	if (this->initBuf != nullptr) {
		// at this point qt's next commit to the surface will have a new buffer, and we can safely delete this one.
		delete this->initBuf;
		this->initBuf = nullptr;
	}

	return this->QtWaylandClient::QWaylandShellSurface::handleExpose(region);
}

void QSWaylandSessionLockSurface::setExtension(LockWindowExtension* ext) {
	if (ext == nullptr) {
		if (this->window() != nullptr) this->window()->window()->close();
	} else {
		if (this->ext != nullptr) {
			this->ext->surface = nullptr;
		}

		this->ext = ext;
		this->ext->surface = this;
	}
}

void QSWaylandSessionLockSurface::setVisible() {
	if (this->configured && !this->visible) initVisible();
	this->visible = true;
}

void QSWaylandSessionLockSurface::ext_session_lock_surface_v1_configure(
    quint32 serial,
    quint32 width,
    quint32 height
) {
	this->ack_configure(serial);

	this->size = QSize(static_cast<qint32>(width), static_cast<qint32>(height));
	if (!this->configured) {
		this->configured = true;

		this->window()->resizeFromApplyConfigure(this->size);
		this->window()->handleExpose(QRect(QPoint(), this->size));
		if (this->visible) initVisible();
	} else {
		this->window()->applyConfigureWhenPossible();
	}
}

void QSWaylandSessionLockSurface::initVisible() {
	this->visible = true;

	// qt always commits a null buffer in QWaylandWindow::initWindow.
	// We attach a dummy buffer to satisfy ext_session_lock_v1.
	this->initBuf = new QtWaylandClient::QWaylandShmBuffer(
	    this->window()->display(),
	    size,
	    QImage::Format_ARGB32
	);

	this->window()->waylandSurface()->attach(initBuf->buffer(), 0, 0);
	this->window()->window()->setVisible(true);
}