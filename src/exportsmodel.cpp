#include "exportsmodel.h"

#include "demangler.h"
#include "elfstructs.h"
#include "icons.h"
#include "moduledata.h"

#include <KLocalizedString>

#include <QFontDatabase>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

CExportsModel::CExportsModel(QObject* pParent)
: QAbstractTableModel(pParent)
{
}

CExportsModel::~CExportsModel()
{
}

void CExportsModel::SetExports(std::size_t uModule, std::vector<SExportSymbol> vExports, EElfClass eClass)
{
	beginResetModel();

	m_uModule = uModule;
	m_vExports = std::move(vExports);
	m_b64Bit = eClass != EElfClass::Class32;

	endResetModel();
}

void CExportsModel::Clear()
{
	beginResetModel();

	m_vExports.clear();
	m_uModule = g_uInvalidIndex;

	endResetModel();
}

void CExportsModel::SetDemangleEnabled(bool bEnabled)
{
	if (m_bDemangle == bEnabled)
		return;

	m_bDemangle = bEnabled;

	if (m_vExports.empty())
		return;

	QModelIndex const topLeft = index(0, static_cast<int>(EColumn::Symbol));
	QModelIndex const bottomRight = index(static_cast<int>(m_vExports.size()) - 1, static_cast<int>(EColumn::Symbol));
	Q_EMIT dataChanged(topLeft, bottomRight);
}

bool CExportsModel::IsDemangleEnabled() const
{
	return m_bDemangle;
}

std::size_t CExportsModel::ModuleIndex() const
{
	return m_uModule;
}

SExportSymbol const* CExportsModel::SymbolAt(int iRow) const
{
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vExports.size())
		return nullptr;

	return &m_vExports[static_cast<std::size_t>(iRow)];
}

QString CExportsModel::RawSymbolNameAt(int iRow) const
{
	SExportSymbol const* const pSymbol = SymbolAt(iRow);
	if (pSymbol == nullptr)
		return QString();

	return QString::fromStdString(pSymbol->sName);
}

QString CExportsModel::DisplaySymbolNameAt(int iRow) const
{
	SExportSymbol const* const pSymbol = SymbolAt(iRow);
	if (pSymbol == nullptr)
		return QString();

	return symbolText(*pSymbol);
}

std::size_t CExportsModel::ExportCount() const
{
	return m_vExports.size();
}

int CExportsModel::rowCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(m_vExports.size());
}

int CExportsModel::columnCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(EColumn::Count);
}

QVariant CExportsModel::data(QModelIndex const& rIndex, int iRole) const
{
	if (!rIndex.isValid())
		return QVariant();
	if (rIndex.row() < 0 || static_cast<std::size_t>(rIndex.row()) >= m_vExports.size())
		return QVariant();

	SExportSymbol const& rSymbol = m_vExports[static_cast<std::size_t>(rIndex.row())];
	EColumn const eColumn = static_cast<EColumn>(rIndex.column());

	switch (iRole)
	{
	case Qt::DisplayRole:
		{
			switch (eColumn)
			{
			case EColumn::Status: return QString();
			case EColumn::Symbol: return symbolText(rSymbol);
			case EColumn::Version: return versionText(rSymbol);
			case EColumn::Type: return SymbolTypeText(rSymbol.eType);
			case EColumn::Binding: return SymbolBindingText(rSymbol.eBinding);
			case EColumn::Visibility: return SymbolVisibilityText(rSymbol.eVisibility);
			case EColumn::Address: return addressText(rSymbol);
			default: break;
			}

			return QVariant();
		}

	case Qt::DecorationRole:
		{
			if (eColumn != EColumn::Status)
				return QVariant();
			return SymbolStatusIcon(ESymbolStatus::Exported);
		}

	case Qt::ToolTipRole:
		{
			if (eColumn == EColumn::Status)
				return SymbolStatusTooltip(ESymbolStatus::Exported);
			if (eColumn == EColumn::Symbol && m_bDemangle)
				return QString::fromStdString(rSymbol.sName);
			if (eColumn == EColumn::Address)
				return i18n("Size: %1 bytes", QString::number(rSymbol.uSize));

			return QVariant();
		}

	case Qt::TextAlignmentRole:
		{
			if (eColumn == EColumn::Address)
				return QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
			return QVariant(static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
		}

	case Qt::FontRole:
		{
			if (eColumn != EColumn::Address)
				return QVariant();
			return QFontDatabase::systemFont(QFontDatabase::FixedFont);
		}

	case Qt::UserRole:
		return sortKey(rSymbol, eColumn);

	default:
		break;
	}

	return QVariant();
}

QVariant CExportsModel::headerData(int iSection, Qt::Orientation eOrientation, int iRole) const
{
	if (eOrientation != Qt::Horizontal)
		return QVariant();
	if (iRole != Qt::DisplayRole)
		return QVariant();

	switch (static_cast<EColumn>(iSection))
	{
	case EColumn::Status: return QString();
	case EColumn::Symbol: return i18n("Symbol");
	case EColumn::Version: return i18n("Version");
	case EColumn::Type: return i18n("Type");
	case EColumn::Binding: return i18n("Binding");
	case EColumn::Visibility: return i18n("Visibility");
	case EColumn::Address: return i18n("Address");
	default: break;
	}

	return QVariant();
}

Qt::ItemFlags CExportsModel::flags(QModelIndex const& rIndex) const
{
	if (!rIndex.isValid())
		return Qt::NoItemFlags;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QString CExportsModel::symbolText(SExportSymbol const& rSymbol) const
{
	return QString::fromStdString(CDemangler::Instance().DemangleIf(rSymbol.sName, m_bDemangle));
}

QString CExportsModel::versionText(SExportSymbol const& rSymbol) const
{
	QString sVersion = QString::fromStdString(rSymbol.sVersion);
	if (sVersion.isEmpty())
		return sVersion;

	if (!rSymbol.bDefaultVersion)
		sVersion += i18n(" (non-default)");

	return sVersion;
}

QString CExportsModel::addressText(SExportSymbol const& rSymbol) const
{
	if (rSymbol.uSectionIndex == static_cast<std::uint16_t>(ESectionIndex::Absolute))
		return QStringLiteral("ABS");
	if (rSymbol.uSectionIndex == static_cast<std::uint16_t>(ESectionIndex::Common))
		return QStringLiteral("COMMON");

	int const iWidth = m_b64Bit ? 16 : 8;

	return QStringLiteral("0x%1").arg(rSymbol.uAddress, iWidth, 16, QLatin1Char('0'));
}

QVariant CExportsModel::sortKey(SExportSymbol const& rSymbol, EColumn eColumn) const
{
	switch (eColumn)
	{
	case EColumn::Status: return 0;
	case EColumn::Symbol: return symbolText(rSymbol);
	case EColumn::Version: return versionText(rSymbol);
	case EColumn::Type: return static_cast<int>(rSymbol.eType);
	case EColumn::Binding: return static_cast<int>(rSymbol.eBinding);
	case EColumn::Visibility: return static_cast<int>(rSymbol.eVisibility);
	case EColumn::Address: return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(rSymbol.uAddress));
	default: break;
	}

	return QVariant();
}
