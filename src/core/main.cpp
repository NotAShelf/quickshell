#include "main.hpp"
#include <iostream>

#include <qapplication.h>
#include <qcommandlineoption.h>
#include <qcommandlineparser.h>
#include <qcoreapplication.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qlogging.h>
#include <qquickwindow.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qtextstream.h>
#include <qtpreprocessorsupport.h>

#include "plugin.hpp"
#include "rootwrapper.hpp"

int qs_main(int argc, char** argv) {
	QString configFilePath;
	QString workingDirectory;

	auto useQApplication = false;
	auto nativeTextRendering = false;
	auto desktopSettingsAware = true;
	QHash<QString, QString> envOverrides;

	{
		const auto app = QCoreApplication(argc, argv);
		QCoreApplication::setApplicationName("quickshell");
		QCoreApplication::setApplicationVersion("0.1.0 (" GIT_REVISION ")");

		QCommandLineParser parser;
		parser.addHelpOption();
		parser.addVersionOption();

		// clang-format off
		auto currentOption = QCommandLineOption("current", "Print information about the manifest and defaults.");
		auto manifestOption = QCommandLineOption({"m", "manifest"}, "Path to a configuration manifest.", "path");
		auto configOption = QCommandLineOption({"c", "config"}, "Name of a configuration in the manifest.", "name");
		auto pathOption = QCommandLineOption({"p", "path"}, "Path to a configuration file.", "path");
		auto workdirOption = QCommandLineOption({"d", "workdir"}, "Initial working directory.", "path");
		// clang-format on

		parser.addOption(currentOption);
		parser.addOption(manifestOption);
		parser.addOption(configOption);
		parser.addOption(pathOption);
		parser.addOption(workdirOption);
		parser.process(app);

		{
			auto printCurrent = parser.isSet(currentOption);

			// NOLINTBEGIN
#define CHECK(rname, name, level, label, expr)                                                     \
	QString name = expr;                                                                             \
	if (rname.isEmpty() && !name.isEmpty()) {                                                        \
		rname = name;                                                                                  \
		rname##Level = level;                                                                          \
		if (!printCurrent) goto label;                                                                 \
	}

#define OPTSTR(name) (name.isEmpty() ? "(unset)" : name.toStdString())
			// NOLINTEND

			QString basePath;
			int basePathLevel = 0;
			Q_UNUSED(basePathLevel);
			{
				// NOLINTBEGIN
				// clang-format off
				CHECK(basePath, envBasePath, 0, foundbase, qEnvironmentVariable("QS_BASE_PATH"));
				CHECK(basePath, defaultBasePath, 0, foundbase, QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath("quickshell"));
				// clang-format on
				// NOLINTEND

				if (printCurrent) {
					// clang-format off
					std::cout << "Base path: " << OPTSTR(basePath) << "\n";
					std::cout << " - Environment (QS_BASE_PATH): " << OPTSTR(envBasePath) << "\n";
					std::cout << " - Default: " << OPTSTR(defaultBasePath) << "\n";
					// clang-format on
				}
			}
		foundbase:;

			QString configPath;
			int configPathLevel = 10;
			{
				// NOLINTBEGIN
				CHECK(configPath, optionConfigPath, 0, foundpath, parser.value(pathOption));
				CHECK(configPath, envConfigPath, 1, foundpath, qEnvironmentVariable("QS_CONFIG_PATH"));
				// NOLINTEND

				if (printCurrent) {
					// clang-format off
					std::cout << "\nConfig path: " << OPTSTR(configPath) << "\n";
					std::cout << " - Option: " << OPTSTR(optionConfigPath) << "\n";
					std::cout << " - Environment (QS_CONFIG_PATH): " << OPTSTR(envConfigPath) << "\n";
					// clang-format on
				}
			}
		foundpath:;

			QString manifestPath;
			int manifestPathLevel = 10;
			{
				// NOLINTBEGIN
				// clang-format off
				CHECK(manifestPath, optionManifestPath, 0, foundmf, parser.value(manifestOption));
				CHECK(manifestPath, envManifestPath, 1, foundmf, qEnvironmentVariable("QS_MANIFEST"));
				CHECK(manifestPath, defaultManifestPath, 2, foundmf, QDir(basePath).filePath("manifest.conf"));
				// clang-format on
				// NOLINTEND

				if (printCurrent) {
					// clang-format off
					std::cout << "\nManifest path: " << OPTSTR(manifestPath) << "\n";
					std::cout << " - Option: " << OPTSTR(optionManifestPath) << "\n";
					std::cout << " - Environment (QS_MANIFEST): " << OPTSTR(envManifestPath) << "\n";
					std::cout << " - Default: " << OPTSTR(defaultManifestPath) << "\n";
					// clang-format on
				}
			}
		foundmf:;

			QString configName;
			int configNameLevel = 10;
			{
				// NOLINTBEGIN
				CHECK(configName, optionConfigName, 0, foundname, parser.value(configOption));
				CHECK(configName, envConfigName, 1, foundname, qEnvironmentVariable("QS_CONFIG_NAME"));
				// NOLINTEND

				if (printCurrent) {
					// clang-format off
					std::cout << "\nConfig name: " << OPTSTR(configName) << "\n";
					std::cout << " - Option: " << OPTSTR(optionConfigName) << "\n";
					std::cout << " - Environment (QS_CONFIG_NAME): " << OPTSTR(envConfigName) << "\n\n";
					// clang-format on
				}
			}
		foundname:;

			if (configPathLevel == 0 && configNameLevel == 0) {
				qCritical() << "Pass only one of --path or --config";
				return -1;
			}

			if (!configPath.isEmpty() && configPathLevel <= configNameLevel) {
				configFilePath = configPath;
			} else if (!configName.isEmpty()) {
				if (!manifestPath.isEmpty()) {
					auto file = QFile(manifestPath);
					if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
						auto stream = QTextStream(&file);
						while (!stream.atEnd()) {
							auto line = stream.readLine();
							if (line.trimmed().startsWith("#")) continue;
							if (line.trimmed().isEmpty()) continue;

							auto split = line.split('=');
							if (split.length() != 2) {
								qCritical() << "manifest line not in expected format 'name = relativepath':"
								            << line;
								return -1;
							}

							if (split[0].trimmed() == configName) {
								configFilePath = QDir(QFileInfo(file).canonicalPath()).filePath(split[1].trimmed());
								goto haspath; // NOLINT
							}
						}

						qCritical() << "configuration" << configName << "not found in manifest" << manifestPath;
						return -1;
					} else if (manifestPathLevel < 2) {
						qCritical() << "cannot open config manifest at" << manifestPath;
						return -1;
					}
				}

				{
					auto basePathInfo = QFileInfo(basePath);
					if (!basePathInfo.exists()) {
						qCritical() << "base path does not exist:" << basePath;
						return -1;
					} else if (!QFileInfo(basePathInfo.canonicalFilePath()).isDir()) {
						qCritical() << "base path is not a directory" << basePath;
						return -1;
					}

					auto dir = QDir(basePath);
					for (auto& entry: dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot)) {
						if (entry == configName) {
							configFilePath = dir.filePath(entry);
							goto haspath; // NOLINT
						}
					}

					qCritical() << "no directory named " << configName << "found in base path" << basePath;
					return -1;
				}
			haspath:;
			} else {
				configFilePath = basePath;
			}

			auto configFile = QFileInfo(configFilePath);
			if (!configFile.exists()) {
				qCritical() << "config path does not exist:" << configFilePath;
				return -1;
			}

			if (configFile.isDir()) {
				configFilePath = QDir(configFilePath).filePath("shell.qml");
			}

			configFile = QFileInfo(configFilePath);
			if (!configFile.exists()) {
				qCritical() << "no shell.qml found in config path:" << configFilePath;
				return -1;
			} else if (configFile.isDir()) {
				qCritical() << "shell.qml is a directory:" << configFilePath;
				return -1;
			}

			configFilePath = QFileInfo(configFilePath).canonicalFilePath();
			configFile = QFileInfo(configFilePath);
			if (!configFile.exists()) {
				qCritical() << "config file does not exist:" << configFilePath;
				return -1;
			} else if (configFile.isDir()) {
				qCritical() << "config file is a directory:" << configFilePath;
				return -1;
			}

#undef CHECK
#undef OPTSTR

			qInfo() << "config file path:" << configFilePath;

			if (printCurrent) return 0;
		}

		if (!QFile(configFilePath).exists()) {
			qCritical() << "config file does not exist";
			return -1;
		}

		if (parser.isSet(workdirOption)) {
			workingDirectory = parser.value(workdirOption);
		}

		auto file = QFile(configFilePath);
		if (!file.open(QFile::ReadOnly | QFile::Text)) {
			qCritical() << "could not open config file";
			return -1;
		}

		auto stream = QTextStream(&file);
		while (!stream.atEnd()) {
			auto line = stream.readLine().trimmed();
			if (line.startsWith("//@ pragma ")) {
				auto pragma = line.sliced(11).trimmed();

				if (pragma == "UseQApplication") useQApplication = true;
				else if (pragma == "NativeTextRendering") nativeTextRendering = true;
				else if (pragma == "IgnoreSystemSettings") desktopSettingsAware = false;
				else if (pragma.startsWith("Env ")) {
					auto envPragma = pragma.sliced(4);
					auto splitIdx = envPragma.indexOf('=');

					if (splitIdx == -1) {
						qCritical() << "Env pragma" << pragma << "not in the form 'VAR = VALUE'";
						return -1;
					}

					auto var = envPragma.sliced(0, splitIdx).trimmed();
					auto val = envPragma.sliced(splitIdx + 1).trimmed();
					envOverrides.insert(var, val);
				} else {
					qCritical() << "Unrecognized pragma" << pragma;
					return -1;
				}
			} else if (line.startsWith("import")) break;
		}

		file.close();
	}

	for (auto [var, val]: envOverrides.asKeyValueRange()) {
		qputenv(var.toUtf8(), val.toUtf8());
	}

	QGuiApplication::setDesktopSettingsAware(desktopSettingsAware);

	QGuiApplication* app = nullptr;

	if (useQApplication) {
		app = new QApplication(argc, argv);
	} else {
		app = new QGuiApplication(argc, argv);
	}

	if (!workingDirectory.isEmpty()) {
		QDir::setCurrent(workingDirectory);
	}

	QuickshellPlugin::initPlugins();

	// Base window transparency appears to be additive.
	// Use a fully transparent window with a colored rect.
	QQuickWindow::setDefaultAlphaBuffer(true);

	if (nativeTextRendering) {
		QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
	}

	auto root = RootWrapper(configFilePath);
	QGuiApplication::setQuitOnLastWindowClosed(false);

	auto code = QGuiApplication::exec();
	delete app;
	return code;
}
