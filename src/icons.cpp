// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#include "icons.h"

#include "elfstructs.h"
#include "moduledata.h"

#include <KLocalizedString>

#include <QApplication>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QPalette>
#include <QString>

namespace {
// QIcon construction is not free and these functions are called once per
// visible cell per repaint, so the cache is required, not optional.
QIcon themedIcon(char const* pcPrimary, char const* pcFallback1 = nullptr, char const* pcFallback2 = nullptr)
{
	static QHash<QString, QIcon> s_mapCache;
	static QMutex s_mutex;

	QString const sKey = QString::fromLatin1(pcPrimary);

	QMutexLocker const locker(&s_mutex);

	auto const itFound = s_mapCache.constFind(sKey);
	if (itFound != s_mapCache.constEnd())
		return itFound.value();

	QIcon icon;

	char const* const apcNames[] = {pcPrimary, pcFallback1, pcFallback2};
	for (char const* const pcName : apcNames)
	{
		if (pcName == nullptr)
			continue;

		QString const sName = QString::fromLatin1(pcName);
		if (!QIcon::hasThemeIcon(sName))
			continue;

		icon = QIcon::fromTheme(sName);
		if (!icon.isNull())
			break;
	}

	s_mapCache.insert(sKey, icon);
	return icon;
}

// Keeps the fallback legible against the current palette's base colour.
QColor paletteAwareColor(QColor const& rFallback)
{
	if (!rFallback.isValid())
		return rFallback;
	if (qApp == nullptr)
		return rFallback;

	QColor const baseColor = qApp->palette().color(QPalette::Base);
	if (!baseColor.isValid())
		return rFallback;

	if (baseColor.lightness() >= 128)
		return rFallback;

	return rFallback.lighter(125);
}

QString hexText(unsigned int uValue)
{
	return QStringLiteral("0x%1").arg(uValue, 0, 16);
}
} //namespace

QIcon ModuleStatusIcon(EModuleStatus eStatus)
{
	switch (eStatus)
	{
	case EModuleStatus::Root: return themedIcon("application-x-executable", "applications-development");
	case EModuleStatus::Resolved: return themedIcon("dialog-ok-apply", "emblem-checked", "dialog-ok");
	case EModuleStatus::Missing: return themedIcon("dialog-error", "emblem-error", "edit-delete");
	case EModuleStatus::Duplicate: return themedIcon("edit-copy", "emblem-symbolic-link");
	case EModuleStatus::Error: return themedIcon("dialog-warning", "emblem-warning");
	case EModuleStatus::Unknown: break;
	}

	return themedIcon("dialog-question", "unknown");
}

QIcon SymbolStatusIcon(ESymbolStatus eStatus)
{
	switch (eStatus)
	{
	case ESymbolStatus::Resolved: return themedIcon("dialog-ok-apply", "emblem-checked");
	case ESymbolStatus::Unresolved: return themedIcon("dialog-error", "emblem-error");
	case ESymbolStatus::WeakUnresolved: return themedIcon("dialog-warning", "emblem-warning");
	case ESymbolStatus::Exported: return themedIcon("document-export", "arrow-right");
	case ESymbolStatus::Unknown: break;
	}

	return themedIcon("dialog-question", "unknown");
}

QIcon SymbolTypeIcon(ESymbolType eType)
{
	switch (eType)
	{
	case ESymbolType::Func: return themedIcon("code-function", "application-x-executable");
	case ESymbolType::Object: return themedIcon("code-variable", "document-properties");
	case ESymbolType::Tls: return themedIcon("code-context", "document-properties");
	default: break;
	}

	return QIcon();
}

QIcon ApplicationIcon()
{
	// Installed, the icon theme copy wins so the user's theme can override it;
	// from the build tree the theme lookup fails and the compiled-in resource
	// keeps the real icon.
	if (QIcon::hasThemeIcon(QStringLiteral("kdepends")))
		return QIcon::fromTheme(QStringLiteral("kdepends"));

	return QIcon(QStringLiteral(":/icons/sc-apps-kdepends.svg"));
}

QColor ModuleStatusColor(EModuleStatus eStatus)
{
	switch (eStatus)
	{
	case EModuleStatus::Missing: return paletteAwareColor(QColor(0xda, 0x44, 0x53));
	case EModuleStatus::Error: return paletteAwareColor(QColor(0xf6, 0x74, 0x00));
	case EModuleStatus::Duplicate: return paletteAwareColor(QColor(0x7f, 0x8c, 0x8d));
	default: break;
	}

	return QColor();
}

QColor SymbolStatusColor(ESymbolStatus eStatus)
{
	switch (eStatus)
	{
	case ESymbolStatus::Unresolved: return paletteAwareColor(QColor(0xda, 0x44, 0x53));
	case ESymbolStatus::WeakUnresolved: return paletteAwareColor(QColor(0xf6, 0x74, 0x00));
	default: break;
	}

	return QColor();
}

QString ModuleStatusText(EModuleStatus eStatus)
{
	switch (eStatus)
	{
	case EModuleStatus::Root: return i18n("Root");
	case EModuleStatus::Resolved: return i18n("OK");
	case EModuleStatus::Missing: return i18n("Missing");
	case EModuleStatus::Duplicate: return i18n("Duplicate");
	case EModuleStatus::Error: return i18n("Error");
	case EModuleStatus::Unknown: break;
	}

	return i18n("Unknown");
}

QString SymbolStatusText(ESymbolStatus eStatus)
{
	switch (eStatus)
	{
	case ESymbolStatus::Resolved: return i18n("Resolved");
	case ESymbolStatus::Unresolved: return i18n("Unresolved");
	case ESymbolStatus::WeakUnresolved: return i18n("Weak (unresolved)");
	case ESymbolStatus::Exported: return i18n("Exported");
	case ESymbolStatus::Unknown: break;
	}

	return i18n("Unknown");
}

// The symbol type/binding/visibility spellings are deliberately not translated:
// they are the standard ELF names and users match them against readelf output.
QString SymbolTypeText(ESymbolType eType)
{
	switch (eType)
	{
	case ESymbolType::NoType: return QStringLiteral("NOTYPE");
	case ESymbolType::Object: return QStringLiteral("OBJECT");
	case ESymbolType::Func: return QStringLiteral("FUNC");
	case ESymbolType::Section: return QStringLiteral("SECTION");
	case ESymbolType::File: return QStringLiteral("FILE");
	case ESymbolType::Common: return QStringLiteral("COMMON");
	case ESymbolType::Tls: return QStringLiteral("TLS");
	case ESymbolType::GnuIFunc: return QStringLiteral("IFUNC");
	}

	return hexText(static_cast<unsigned int>(eType));
}

QString SymbolBindingText(ESymbolBinding eBinding)
{
	switch (eBinding)
	{
	case ESymbolBinding::Local: return QStringLiteral("LOCAL");
	case ESymbolBinding::Global: return QStringLiteral("GLOBAL");
	case ESymbolBinding::Weak: return QStringLiteral("WEAK");
	case ESymbolBinding::GnuUnique: return QStringLiteral("UNIQUE");
	}

	return hexText(static_cast<unsigned int>(eBinding));
}

QString SymbolVisibilityText(ESymbolVisibility eVisibility)
{
	switch (eVisibility)
	{
	case ESymbolVisibility::Default: return QStringLiteral("DEFAULT");
	case ESymbolVisibility::Internal: return QStringLiteral("INTERNAL");
	case ESymbolVisibility::Hidden: return QStringLiteral("HIDDEN");
	case ESymbolVisibility::Protected: return QStringLiteral("PROTECTED");
	}

	return hexText(static_cast<unsigned int>(eVisibility));
}

QString ModuleStatusTooltip(EModuleStatus eStatus)
{
	switch (eStatus)
	{
	case EModuleStatus::Root: return i18n("The file this tab was opened on.");
	case EModuleStatus::Resolved: return i18n("This library was found and loaded successfully.");
	case EModuleStatus::Missing: return i18n("This library was not found in any search path.");
	case EModuleStatus::Duplicate: return i18n("This library already appears elsewhere in the tree and is not expanded again.");
	case EModuleStatus::Error: return i18n("This file was found but could not be read or parsed.");
	case EModuleStatus::Unknown: break;
	}

	return i18n("This module has not been analyzed yet.");
}

QString SymbolStatusTooltip(ESymbolStatus eStatus)
{
	switch (eStatus)
	{
	case ESymbolStatus::Resolved:
		return i18n("A module in the dependency closure defines this symbol.");
	case ESymbolStatus::Unresolved:
		return i18n("No module in the dependency closure defines this symbol.");
	case ESymbolStatus::WeakUnresolved:
		return i18n("No module in the dependency closure defines this symbol. The binding is weak, so this is legal at run time.");
	case ESymbolStatus::Exported:
		return i18n("This module defines this symbol.");
	case ESymbolStatus::Unknown:
		break;
	}

	return i18n("Import resolution has not run for this module yet.");
}
