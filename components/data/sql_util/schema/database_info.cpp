#include "sql_util/schema/database_info.h"

namespace DbSchema {

	class DatabaseInfoImpl {
	public:
		DatabaseInfoImpl(std::string_view databaseName);
		// Deep copy for DatabaseInfo::Clone(). The table list and foreign-key
		// manager hold pImpl handles that copy SHALLOWLY by default, so a defaulted
		// copy would leave the copy sharing the original's tables/FKs. We clone each
		// element explicitly to produce a genuinely independent schema. Assignment
		// stays deleted.
		DatabaseInfoImpl(const DatabaseInfoImpl& ref);
		DatabaseInfoImpl& operator=(const DatabaseInfoImpl&) = delete;
		~DatabaseInfoImpl() = default;

		// Accessors
		const std::string& GetDatabaseName() const;
		const ForeignKeyManager& GetForeignKeyManager() const;
		const TableInfo& GetTableInfo(std::string_view tableName) const;
		StringArray GetRootTables() const;
		StringArray GetAllTables() const;

		// Modifiers
		void AddTable(std::string_view tableName);
		void AddColumnSimple(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnPrimaryKey(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnUnique(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnNullable(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnForeignKeyRef(
			std::string_view tableNameParent,
			std::string_view columnNameParent,
			std::string_view tableNameChild,
			std::string_view columnNameChild);
		void AddColumnForeignKeyRefNullable(
			std::string_view tableNameParent,
			std::string_view columnNameParent,
			std::string_view tableNameChild,
			std::string_view columnNameChild);
		void AddColumnNotNullableWithDefault(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType,
			std::string_view defaultValue);
		void AddUniqueConstraintHelper(
			std::string_view tableName,
			const TableUniqueValues& tableUniqueValues);
		void AddCheckConstraint(
			std::string_view tableName,
			std::string_view name,
			std::string_view expression);

	private:
		std::string databaseName_;
		TableInfoList tableInfoList_;
		ForeignKeyManager foreignKeyManager_;

		TableInfo& GetTableInfo(std::string_view tableName);
	};

	DatabaseInfoImpl::DatabaseInfoImpl(std::string_view databaseName)
		: databaseName_(databaseName) {}

	DatabaseInfoImpl::DatabaseInfoImpl(const DatabaseInfoImpl& ref)
		: databaseName_(ref.databaseName_),
		  foreignKeyManager_(ref.foreignKeyManager_.Clone()) {
		for (const TableInfo& tableInfo : ref.tableInfoList_) {
			tableInfoList_.push_back(tableInfo.Clone());
		}
	}

	const std::string& DatabaseInfoImpl::GetDatabaseName() const {
		return databaseName_;
	}

	const ForeignKeyManager& DatabaseInfoImpl::GetForeignKeyManager() const {
		return foreignKeyManager_;
	}

	const TableInfo& DatabaseInfoImpl::GetTableInfo(std::string_view tableName) const {
		for (const TableInfo& tableInfo : tableInfoList_) {
			if (tableInfo.GetTableName() == tableName) {
				return tableInfo;
			}
		}
		std::stringstream ss;
		ss << "DatabaseInfoImpl::GetTableInfo - table not found: " << tableName;
		throw std::invalid_argument(ss.str());
	}

	TableInfo& DatabaseInfoImpl::GetTableInfo(std::string_view tableName) {
		const DatabaseInfoImpl* constThis = this;
		return const_cast<TableInfo&>(constThis->GetTableInfo(tableName));
	}

	StringArray DatabaseInfoImpl::GetRootTables() const {
		StringArray ret;
		for (const TableInfo& tableInfo : tableInfoList_) {
			if (foreignKeyManager_.IsRootTable(tableInfo.GetTableName())) {
				ret.push_back(tableInfo.GetTableName());
			}
		}
		return ret;
	}

	StringArray DatabaseInfoImpl::GetAllTables() const {
		StringArray ret;
		for (const TableInfo& tableInfo : tableInfoList_) {
			ret.push_back(tableInfo.GetTableName());
		}
		return ret;
	}

	void DatabaseInfoImpl::AddTable(std::string_view tableName) {
		for (const TableInfo& tableInfo : tableInfoList_) {
			if (tableInfo.GetTableName() == tableName) {
				std::stringstream ss;
				ss << "DatabaseInfoImpl::AddTable - table already exists: "
					<< tableName;
				throw std::invalid_argument(ss.str());
			}
		}
		tableInfoList_.emplace_back(tableName);
	}

	void DatabaseInfoImpl::AddColumnSimple(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddColumn(ColumnInfo(columnName, databaseType));
	}

	void DatabaseInfoImpl::AddColumnPrimaryKey(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddColumn(ColumnInfo(columnName, databaseType, true));
	}

	void DatabaseInfoImpl::AddColumnUnique(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddColumn(ColumnInfo(columnName, databaseType, false, true));
	}

	void DatabaseInfoImpl::AddColumnNullable(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddColumn(ColumnInfo(columnName, databaseType, false, false, true));
	}

	void DatabaseInfoImpl::AddColumnForeignKeyRef(
		std::string_view tableNameParent,
		std::string_view columnNameParent,
		std::string_view tableNameChild,
		std::string_view columnNameChild) {
		const TableInfo& tableInfoParent = GetTableInfo(tableNameParent);
		TableInfo& tableInfoChild = GetTableInfo(tableNameChild);
		const ColumnInfo& columnInfoParent
			= tableInfoParent.GetColumn(columnNameParent);
		DatabaseTypes databaseType = columnInfoParent.GetDatabaseType();
		if (databaseType == DB_TYPE_SERIAL) {
			// Convert a reference to a 4 byte auto increment to an INT
			databaseType = DB_TYPE_INT;
		}
		else if (databaseType == DB_TYPE_BIGSERIAL) {
			// Convert a reference to an 8 byte auto increment to a BIGINT
			databaseType = DB_TYPE_BIGINT;
		}
		foreignKeyManager_.AddForeignKeyReference(
			{ std::string(tableNameParent), std::string(columnNameParent) },
			{ std::string(tableNameChild), std::string(columnNameChild) });
		tableInfoChild.AddColumn(ColumnInfo(columnNameChild, databaseType));
	}

	void DatabaseInfoImpl::AddColumnForeignKeyRefNullable(
		std::string_view tableNameParent,
		std::string_view columnNameParent,
		std::string_view tableNameChild,
		std::string_view columnNameChild) {
		const TableInfo& tableInfoParent = GetTableInfo(tableNameParent);
		TableInfo& tableInfoChild = GetTableInfo(tableNameChild);
		const ColumnInfo& columnInfoParent
			= tableInfoParent.GetColumn(columnNameParent);
		DatabaseTypes databaseType = columnInfoParent.GetDatabaseType();
		if (databaseType == DB_TYPE_SERIAL) {
			// Convert a reference to a 4 byte auto increment to an INT
			databaseType = DB_TYPE_INT;
		}
		else if (databaseType == DB_TYPE_BIGSERIAL) {
			// Convert a reference to an 8 byte auto increment to a BIGINT
			databaseType = DB_TYPE_BIGINT;
		}
		foreignKeyManager_.AddForeignKeyReference(
			{ std::string(tableNameParent), std::string(columnNameParent) },
			{ std::string(tableNameChild), std::string(columnNameChild) },
			true);  // nullable
		// isPrimaryKey=false, isUnique=false, isNullable=true
		tableInfoChild.AddColumn(ColumnInfo(columnNameChild, databaseType, false, false, true));
	}

	void DatabaseInfoImpl::AddColumnNotNullableWithDefault(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType,
		std::string_view defaultValue) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddColumn(ColumnInfo(
			columnName,
			databaseType,
			false,
			false,
			false,
            std::string(defaultValue)));
	}

	void DatabaseInfoImpl::AddUniqueConstraintHelper(
		std::string_view tableName,
		const TableUniqueValues& tableUniqueValues) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddUniqueConstraintHelper(tableUniqueValues);
	}

	void DatabaseInfoImpl::AddCheckConstraint(
		std::string_view tableName,
		std::string_view name,
		std::string_view expression) {
		TableInfo& tableInfo = GetTableInfo(tableName);
		tableInfo.AddCheckConstraint(name, expression);
	}

	DatabaseInfo::DatabaseInfo(std::string_view databaseName)
		: impl_(std::make_shared<DatabaseInfoImpl>(databaseName)) {}

	DatabaseInfo DatabaseInfo::Clone() const {
		DatabaseInfo copy(GetDatabaseName());
		// Replace the freshly constructed (empty) impl with a deep copy of ours.
		copy.impl_ = std::make_shared<DatabaseInfoImpl>(*impl_);
		return copy;
	}

	const std::string& DatabaseInfo::GetDatabaseName() const {
		return impl_->GetDatabaseName();
	}

	const ForeignKeyManager& DatabaseInfo::GetForeignKeyManager() const {
		return impl_->GetForeignKeyManager();
	}

	const TableInfo& DatabaseInfo::GetTableInfo(std::string_view tableName) const {
		const DatabaseInfoImpl* constImpl = impl_.get();
		return constImpl->GetTableInfo(tableName);
	}

	StringArray DatabaseInfo::GetRootTables() const {
		return impl_->GetRootTables();
	}

	StringArray DatabaseInfo::GetAllTables() const {
		return impl_->GetAllTables();
	}

	void DatabaseInfo::AddTable(std::string_view tableName) {
		impl_->AddTable(tableName);
	}

	void DatabaseInfo::AddColumnSimple(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		impl_->AddColumnSimple(tableName, columnName, databaseType);
	}

	void DatabaseInfo::AddColumnPrimaryKey(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		impl_->AddColumnPrimaryKey(tableName, columnName, databaseType);
	}

	void DatabaseInfo::AddColumnUnique(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		impl_->AddColumnUnique(tableName, columnName, databaseType);
	}

	void DatabaseInfo::AddColumnNullable(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType) {
		impl_->AddColumnNullable(tableName, columnName, databaseType);
	}

	void DatabaseInfo::AddColumnForeignKeyRef(
		std::string_view tableNameParent,
		std::string_view columnNameParent,
		std::string_view tableNameChild,
		std::string_view columnNameChild) {
		impl_->AddColumnForeignKeyRef(
			tableNameParent,
			columnNameParent,
			tableNameChild,
			columnNameChild);
	}

	void DatabaseInfo::AddColumnForeignKeyRefNullable(
		std::string_view tableNameParent,
		std::string_view columnNameParent,
		std::string_view tableNameChild,
		std::string_view columnNameChild) {
		impl_->AddColumnForeignKeyRefNullable(
			tableNameParent,
			columnNameParent,
			tableNameChild,
			columnNameChild);
	}

	void DatabaseInfo::AddColumnNotNullableWithDefault(
		std::string_view tableName,
		std::string_view columnName,
		DatabaseTypes databaseType,
		std::string_view defaultValue) {
		impl_->AddColumnNotNullableWithDefault(
            tableName, columnName, databaseType, defaultValue);
	}

	void DatabaseInfo::AddUniqueConstraintHelper(
		std::string_view tableName,
		const TableUniqueValues& tableUniqueValues) {
		impl_->AddUniqueConstraintHelper(tableName, tableUniqueValues);
	}

	void DatabaseInfo::AddCheckConstraint(
		std::string_view tableName,
		std::string_view name,
		std::string_view expression) {
		impl_->AddCheckConstraint(tableName, name, expression);
	}

}  // namespace DbSchema
