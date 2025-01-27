#include "rootwrapper.hpp"
#include <cstdlib>
#include <utility>

#include <qdir.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qurl.h>

#include "generation.hpp"
#include "qmlglobal.hpp"
#include "scan.hpp"
#include "shell.hpp"

RootWrapper::RootWrapper(QString rootPath)
    : QObject(nullptr)
    , rootPath(std::move(rootPath))
    , originalWorkingDirectory(QDir::current().absolutePath()) {
	// clang-format off
	QObject::connect(QuickshellSettings::instance(), &QuickshellSettings::watchFilesChanged, this, &RootWrapper::onWatchFilesChanged);
	// clang-format on

	this->reloadGraph(true);

	if (this->generation == nullptr) {
		qCritical() << "could not create scene graph, exiting";
		exit(-1); // NOLINT
	}
}

RootWrapper::~RootWrapper() {
	// event loop may no longer be running so deleteLater is not an option
	if (this->generation != nullptr) {
		delete this->generation->root;
		this->generation->root = nullptr;
	}

	delete this->generation;
}

void RootWrapper::reloadGraph(bool hard) {
	auto scanner = QmlScanner();
	scanner.scanQmlFile(this->rootPath);

	auto* generation = new EngineGeneration(std::move(scanner));
	generation->wrapper = this;

	// todo: move into EngineGeneration
	if (this->generation != nullptr) {
		QuickshellSettings::reset();
	}

	QDir::setCurrent(this->originalWorkingDirectory);

	auto url = QUrl::fromLocalFile(this->rootPath);
	// unless the original file comes from the qsintercept scheme
	url.setScheme("qsintercept");
	auto component = QQmlComponent(generation->engine, url);

	auto* obj = component.beginCreate(generation->engine->rootContext());

	if (obj == nullptr) {
		qWarning() << component.errorString().toStdString().c_str();
		qWarning() << "failed to create root component";
		delete generation;
		return;
	}

	auto* newRoot = qobject_cast<ShellRoot*>(obj);
	if (newRoot == nullptr) {
		qWarning() << "root component was not a Quickshell.ShellRoot";
		delete obj;
		delete generation;
		return;
	}

	generation->root = newRoot;

	component.completeCreate();

	generation->onReload(hard ? nullptr : this->generation);
	if (hard) delete this->generation;
	this->generation = generation;

	qInfo() << "Configuration Loaded";

	QObject::connect(
	    this->generation,
	    &EngineGeneration::filesChanged,
	    this,
	    &RootWrapper::onWatchedFilesChanged
	);

	this->onWatchFilesChanged();
}

void RootWrapper::onWatchFilesChanged() {
	auto watchFiles = QuickshellSettings::instance()->watchFiles();
	if (this->generation != nullptr) {
		this->generation->setWatchingFiles(watchFiles);
	}
}

void RootWrapper::onWatchedFilesChanged() { this->reloadGraph(false); }
