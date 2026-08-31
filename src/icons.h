// Copyright (c) 2026 Jason Gripp
// Licensed under the MIT License.

#pragma once

#include "elfstructs.h"
#include "moduledata.h"

#include <QColor>
#include <QIcon>
#include <QString>

// Centralizes status icons, colors, and text across all models.
// These functions are UI-thread only because QIcon is not thread-safe.

QIcon ModuleStatusIcon(EModuleStatus eStatus);
QIcon SymbolStatusIcon(ESymbolStatus eStatus);
QIcon SymbolTypeIcon(ESymbolType eType);
QIcon ApplicationIcon();

// An invalid QColor means "use the view's own palette." Models must check
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
