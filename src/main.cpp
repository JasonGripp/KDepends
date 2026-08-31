// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "icons.h"
#include "kdepends_version.h"
#include "mainwindow.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

int main(int iArgc, char** ppcArgv)
{
	QApplication app(iArgc, ppcArgv);

	KLocalizedString::setApplicationDomain("kdepends");

	KAboutData aboutData(
		QStringLiteral("kdepends"),
		i18n("KDepends"),
		QStringLiteral(KDEPENDS_VERSION_STRING),
		i18n("ELF dependency inspector"),
		KAboutLicense::MIT,
		i18n("Copyright © 2026 Jason Gripp"),
		i18n("A KDE recreation of Dependency Walker for Linux ELF binaries."),
		QStringLiteral("https://github.com/JasonGripp/KDepends"),
		QStringLiteral("https://github.com/JasonGripp/KDepends/issues"));

	// KAboutData otherwise derives this from the homepage host, giving
	// "com.github.kdepends"; the installed entry is kdepends.desktop, so say so
	// explicitly. setApplicationData() propagates it to QGuiApplication, which
	// is what the desktop portal and the Wayland app id are read from.
	aboutData.setDesktopFileName(QStringLiteral("kdepends"));

	KAboutData::setApplicationData(aboutData);

	QApplication::setWindowIcon(ApplicationIcon());

	QCommandLineParser parser;
	aboutData.setupCommandLine(&parser);
	parser.addPositionalArgument(QStringLiteral("files"), i18n("ELF files to open"), QStringLiteral("[files...]"));
	parser.process(app);
	aboutData.processCommandLine(&parser);

	QStringList vPaths;
	QTextStream errorStream(stderr);

	for (QString const& rsArgument : parser.positionalArguments())
	{
		QUrl const url(rsArgument);

		if (url.isValid() && !url.scheme().isEmpty() && url.scheme() != QLatin1String("file"))
		{
			// KDepends is a local-file inspector; downloading a remote binary
			// is a non-goal.
			errorStream << QStringLiteral("kdepends: ignoring non-local URL: %1\n").arg(rsArgument);
			continue;
		}

		if (url.isValid() && url.scheme() == QLatin1String("file"))
		{
			vPaths.append(url.toLocalFile());
			continue;
		}

		vPaths.append(QFileInfo(rsArgument).absoluteFilePath());
	}

	// Heap-allocated on purpose: KMainWindow sets WA_DeleteOnClose, and that
	// self-delete is what makes closing the last window quit the application.
	// A stack window would either double-free or, with the attribute cleared,
	// leave the process running headless after its window is gone.
	CMainWindow* const pWindow = new CMainWindow();
	pWindow->show();

	// Opening after show() means the window is on screen before any analysis
	// begins.
	pWindow->OpenFiles(vPaths);

	return app.exec();
}
