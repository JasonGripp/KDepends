#include "modulesmodel.h"

#include "icons.h"
#include "moduledata.h"

#include <KLocalizedString>

#include <QColor>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLatin1String>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstddef>
#include <vector>

CModulesModel::CModulesModel(QObject* pParent)
: QAbstractTableModel(pParent)
{
}

CModulesModel::~CModulesModel()
{
}

void CModulesModel::Reset()
{
	beginResetModel();

	m_vRows.clear();
	m_mapModuleToRow.clear();

	endResetModel();
}

void CModulesModel::AddModule(std::size_t uModule, SModuleInfo const& rInfo, EModuleStatus eStatus)
{
	if (uModule == g_uInvalidIndex)
		return;

	auto const itExisting = m_mapModuleToRow.find(uModule);
	if (itExisting != m_mapModuleToRow.end())
	{
		UpdateModule(uModule, rInfo);
		return;
	}

	int const iRow = static_cast<int>(m_vRows.size());

	beginInsertRows(QModelIndex(), iRow, iRow);

	SModuleRow row;
	row.uModule = uModule;
	row.info = rInfo;
	row.eStatus = eStatus;
	row.bComplete = rInfo.bParsed;

	m_vRows.push_back(std::move(row));
	m_mapModuleToRow.emplace(uModule, iRow);

	endInsertRows();
}

void CModulesModel::AddModules(std::vector<std::size_t> const& rvModules, std::vector<SModuleInfo> const& rvInfos)
{
	// A length mismatch means a malformed payload; dropping it is safer than a
	// partial insert.
	if (rvModules.size() != rvInfos.size())
		return;
	if (rvModules.empty())
		return;

	std::vector<SModuleRow> vNewRows;
	vNewRows.reserve(rvModules.size());

	for (std::size_t uIndex = 0; uIndex < rvModules.size(); ++uIndex)
	{
		std::size_t const uModule = rvModules[uIndex];
		if (uModule == g_uInvalidIndex)
			continue;
		if (m_mapModuleToRow.find(uModule) != m_mapModuleToRow.end())
			continue;

		SModuleRow row;
		row.uModule = uModule;
		row.info = rvInfos[uIndex];
		row.eStatus = EModuleStatus::Resolved;
		row.bComplete = rvInfos[uIndex].bParsed;

		vNewRows.push_back(std::move(row));
	}

	if (vNewRows.empty())
		return;

	int const iFirst = static_cast<int>(m_vRows.size());
	int const iLast = iFirst + static_cast<int>(vNewRows.size()) - 1;

	// One insert pair for the whole batch, so a wide closure does not cause one
	// view relayout per module.
	beginInsertRows(QModelIndex(), iFirst, iLast);

	for (SModuleRow& rRow : vNewRows)
	{
		m_mapModuleToRow.emplace(rRow.uModule, static_cast<int>(m_vRows.size()));
		m_vRows.push_back(std::move(rRow));
	}

	endInsertRows();
}

void CModulesModel::UpdateModule(std::size_t uModule, SModuleInfo const& rInfo)
{
	auto const itFound = m_mapModuleToRow.find(uModule);
	if (itFound == m_mapModuleToRow.end())
		return;

	int const iRow = itFound->second;
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vRows.size())
		return;

	SModuleRow& rRow = m_vRows[static_cast<std::size_t>(iRow)];
	rRow.info = rInfo;
	rRow.bComplete = rInfo.bParsed;

	Q_EMIT dataChanged(index(iRow, 0), index(iRow, static_cast<int>(EColumn::Count) - 1));
}

void CModulesModel::SetModuleStatus(std::size_t uModule, EModuleStatus eStatus, QString const& rsError)
{
	auto const itFound = m_mapModuleToRow.find(uModule);
	if (itFound == m_mapModuleToRow.end())
		return;

	int const iRow = itFound->second;
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vRows.size())
		return;

	SModuleRow& rRow = m_vRows[static_cast<std::size_t>(iRow)];
	rRow.eStatus = eStatus;
	rRow.sError = rsError;

	Q_EMIT dataChanged(index(iRow, 0), index(iRow, static_cast<int>(EColumn::Count) - 1));
}

int CModulesModel::RowForModule(std::size_t uModule) const
{
	auto const itFound = m_mapModuleToRow.find(uModule);
	if (itFound == m_mapModuleToRow.end())
		return -1;

	return itFound->second;
}

std::size_t CModulesModel::ModuleAt(int iRow) const
{
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vRows.size())
		return g_uInvalidIndex;

	return m_vRows[static_cast<std::size_t>(iRow)].uModule;
}

QString CModulesModel::PathAt(int iRow) const
{
	SModuleInfo const* const pInfo = InfoAt(iRow);
	if (pInfo == nullptr)
		return QString();

	return QString::fromStdString(pInfo->sPath);
}

SModuleInfo const* CModulesModel::InfoAt(int iRow) const
{
	if (iRow < 0 || static_cast<std::size_t>(iRow) >= m_vRows.size())
		return nullptr;

	return &m_vRows[static_cast<std::size_t>(iRow)].info;
}

int CModulesModel::ModuleCount() const
{
	return static_cast<int>(m_vRows.size());
}

int CModulesModel::MissingCount() const
{
	int iCount = 0;

	for (SModuleRow const& rRow : m_vRows)
	{
		if (rRow.eStatus == EModuleStatus::Missing || rRow.eStatus == EModuleStatus::Error)
			++iCount;
	}

	return iCount;
}

int CModulesModel::rowCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(m_vRows.size());
}

int CModulesModel::columnCount(QModelIndex const& rParent) const
{
	if (rParent.isValid())
		return 0;

	return static_cast<int>(EColumn::Count);
}

QVariant CModulesModel::data(QModelIndex const& rIndex, int iRole) const
{
	if (!rIndex.isValid())
		return QVariant();
	if (rIndex.row() < 0 || static_cast<std::size_t>(rIndex.row()) >= m_vRows.size())
		return QVariant();

	SModuleRow const& rRow = m_vRows[static_cast<std::size_t>(rIndex.row())];
	SModuleInfo const& rInfo = rRow.info;
	EColumn const eColumn = static_cast<EColumn>(rIndex.column());

	switch (iRole)
	{
	case Qt::DisplayRole:
		{
			if (eColumn == EColumn::Status)
				return QString();
			if (eColumn == EColumn::Name)
				return QFileInfo(QString::fromStdString(rInfo.sPath)).fileName();
			if (eColumn == EColumn::Path)
				return QString::fromStdString(rInfo.sPath);

			// Metadata columns stay empty while the module is a placeholder,
			// rather than showing misleading zeros.
			if (!rRow.bComplete)
				return QString();

			switch (eColumn)
			{
			case EColumn::Machine: return QString::fromStdString(rInfo.sMachineName);
			case EColumn::Class: return QString::fromStdString(rInfo.sClassName);
			case EColumn::Type: return QString::fromStdString(rInfo.sTypeName);
			case EColumn::Size: return QLocale().formattedDataSize(static_cast<qint64>(rInfo.uFileSize));
			case EColumn::Soname: return QString::fromStdString(rInfo.sSoname);

			case EColumn::EntryPoint:
				{
					if (rInfo.uEntryPoint == 0)
						return QString();
					int const iWidth = rInfo.eClass == EElfClass::Class32 ? 8 : 16;
					return QStringLiteral("0x%1").arg(rInfo.uEntryPoint, iWidth, 16, QLatin1Char('0'));
				}

			case EColumn::Interpreter: return QString::fromStdString(rInfo.sInterpreter);
			case EColumn::RunPath: return runPathText(rInfo);
			case EColumn::Stripped: return rInfo.bStripped ? i18n("yes") : i18n("no");

			default: break;
			}

			return QVariant();
		}

	case Qt::DecorationRole:
		{
			if (eColumn != EColumn::Status)
				return QVariant();
			return ModuleStatusIcon(rRow.eStatus);
		}

	case Qt::ForegroundRole:
		{
			QColor const color = ModuleStatusColor(rRow.eStatus);
			if (!color.isValid())
				return QVariant();
			return color;
		}

	case Qt::ToolTipRole:
		return rowTooltip(rRow);

	case Qt::TextAlignmentRole:
		{
			if (eColumn == EColumn::Size || eColumn == EColumn::EntryPoint)
			{
				return QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
			}

			return QVariant(static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
		}

	case Qt::FontRole:
		{
			if (eColumn != EColumn::EntryPoint)
				return QVariant();
			return QFontDatabase::systemFont(QFontDatabase::FixedFont);
		}

	case Qt::UserRole:
		return sortKey(rRow, eColumn);

	default:
		break;
	}

	return QVariant();
}

QVariant CModulesModel::headerData(int iSection, Qt::Orientation eOrientation, int iRole) const
{
	if (eOrientation != Qt::Horizontal)
		return QVariant();

	// The status column shows only an icon, so its name lives in the tooltip;
	// that is also what names it in the column chooser.
	if (iRole == Qt::ToolTipRole)
	{
		if (static_cast<EColumn>(iSection) == EColumn::Status)
			return i18n("Status");

		return QVariant();
	}

	if (iRole != Qt::DisplayRole)
		return QVariant();

	switch (static_cast<EColumn>(iSection))
	{
	case EColumn::Status: return QString();
	case EColumn::Name: return i18n("Module");
	case EColumn::Path: return i18n("Path");
	case EColumn::Machine: return i18n("Machine");
	case EColumn::Class: return i18n("Class");
	case EColumn::Type: return i18n("Type");
	case EColumn::Size: return i18n("Size");
	case EColumn::Soname: return i18n("SONAME");
	case EColumn::EntryPoint: return i18n("Entry point");
	case EColumn::Interpreter: return i18n("Interpreter");
	case EColumn::RunPath: return i18n("RPATH/RUNPATH");
	case EColumn::Stripped: return i18n("Stripped");
	default: break;
	}

	return QVariant();
}

Qt::ItemFlags CModulesModel::flags(QModelIndex const& rIndex) const
{
	if (!rIndex.isValid())
		return Qt::NoItemFlags;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant CModulesModel::sortKey(SModuleRow const& rRow, EColumn eColumn) const
{
	switch (eColumn)
	{
	case EColumn::Status: return static_cast<int>(rRow.eStatus);
	case EColumn::Size: return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(rRow.info.uFileSize));
	case EColumn::EntryPoint: return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(rRow.info.uEntryPoint));
	default: break;
	}

	return data(index(RowForModule(rRow.uModule), static_cast<int>(eColumn)), Qt::DisplayRole);
}

QString CModulesModel::runPathText(SModuleInfo const& rInfo) const
{
	QStringList vParts;

	auto const Join = [](std::vector<std::string> const& rvEntries) -> QString
	{
		QStringList vEntries;
		vEntries.reserve(static_cast<qsizetype>(rvEntries.size()));

		for (std::string const& rsEntry : rvEntries)
			vEntries.append(QString::fromStdString(rsEntry));

		return vEntries.join(QLatin1Char(':'));
	};

	if (!rInfo.vRunpath.empty())
		vParts.append(QLatin1String("RUNPATH=") + Join(rInfo.vRunpath));
	if (!rInfo.vRpath.empty())
		vParts.append(QLatin1String("RPATH=") + Join(rInfo.vRpath));

	return vParts.join(QLatin1String("  "));
}

QString CModulesModel::rowTooltip(SModuleRow const& rRow) const
{
	QStringList vLines;

	if (!rRow.info.sPath.empty())
		vLines.append(QString::fromStdString(rRow.info.sPath));

	vLines.append(ModuleStatusTooltip(rRow.eStatus));

	if (!rRow.sError.isEmpty())
		vLines.append(rRow.sError);

	return vLines.join(QLatin1Char('\n'));
}
