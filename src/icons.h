// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

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

QIcon ModuleStatusIcon(EModuleStatus eStatus);
QIcon SymbolStatusIcon(ESymbolStatus eStatus);
QIcon SymbolTypeIcon(ESymbolType eType);
QIcon ApplicationIcon();

// An invalid QColor means "use the view's own palette"; models must check
// isValid() and never wrap an invalid colour in a QVariant.
QColor ModuleStatusColor(EModuleStatus eStatus);
QColor SymbolStatusColor(ESymbolStatus eStatus);

QString ModuleStatusText(EModuleStatus eStatus);
QString SymbolStatusText(ESymbolStatus eStatus);
QString SymbolTypeText(ESymbolType eType);
QString SymbolBindingText(ESymbolBinding eBinding);
QString SymbolVisibilityText(ESymbolVisibility eVisibility);
QString ModuleStatusTooltip(EModuleStatus eStatus);
QString SymbolStatusTooltip(ESymbolStatus eStatus);
