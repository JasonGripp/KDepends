#pragma once

#include "elfstructs.h"
#include "moduledata.h"

#include <QColor>
#include <QIcon>
#include <QString>

// The single place that decides how an analysis state looks and reads: its
// icon, its colour, and its display text. Centralised so the tree and the
// three tables never disagree about what "missing" or "weak" looks like.
//
// Every function here is UI-thread only: QIcon and the icon theme are not
// thread-safe. Nothing in the analysis layer includes this header.

QIcon ModuleStatusIcon(EModuleStatus const eStatus);
QIcon SymbolStatusIcon(ESymbolStatus const eStatus);
QIcon SymbolTypeIcon(ESymbolType const eType);
QIcon ApplicationIcon();

// An invalid QColor means "use the view's own palette"; models must check
// isValid() and never wrap an invalid colour in a QVariant.
QColor ModuleStatusColor(EModuleStatus const eStatus);
QColor SymbolStatusColor(ESymbolStatus const eStatus);

QString ModuleStatusText(EModuleStatus const eStatus);
QString SymbolStatusText(ESymbolStatus const eStatus);
QString SymbolTypeText(ESymbolType const eType);
QString SymbolBindingText(ESymbolBinding const eBinding);
QString SymbolVisibilityText(ESymbolVisibility const eVisibility);
QString ModuleStatusTooltip(EModuleStatus const eStatus);
QString SymbolStatusTooltip(ESymbolStatus const eStatus);
