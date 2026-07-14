#include "table_info.h"

namespace DbSchema {

	class TableInfoImpl {
	public:
		TableInfoImpl(std::string_view tableName);
		// Copyable so TableInfo::Clone() can deep-copy. All members are value
		// types (name + vectors of value structs), so the defaulted copy is a
		// genuine deep copy. Assignment stays deleted.
		TableInfoImpl(const TableInfoImpl&) = default;
		TableInfoImpl& operator=(const TableInfoImpl&) = delete;
		~TableInfoImpl() = default;

		void AddColumn(const ColumnInfo& columnInfo);
		const ColumnInfoList& GetColumns() const;
		bool IsPrimaryKey() const;
		const std::string& GetPrimaryKey() const;
		bool IsColumn(std::string_view columnName) const;
		const ColumnInfo& GetColumn(std::string_view columnName) const;
		const std::string& GetTableName() const;

		void AddUniqueConstraintHelper(const TableUniqueValues& tableUniqueValues);
		const TableUniqueValuesArray& GetTableUniqueValues() const;

		void AddCheckConstraint(
			std::string_view name, std::string_view expression);
		const TableCheckConstraintArray& GetTableCheckConstraints() const;

	private:
		std::string tableName_;
		ColumnInfoList columnInfoList_;
		TableUniqueValuesArray tableUniqueValuesArray_;
		TableCheckConstraintArray tableCheckConstraintArray_;
	};

	TableInfoImpl::TableInfoImpl(std::string_view tableName)
		: tableName_(tableName) {
	}

	void TableInfoImpl::AddColumn(const ColumnInfo& columnInfo) {
		for (const ColumnInfo& info : columnInfoList_) {
			if (info.GetColumnName() == columnInfo.GetColumnName()) {
				std::stringstream ss;
				ss << "TableInfoImpl::AddColumn - column(" << info.GetColumnName() << ") already exists.";
				throw std::invalid_argument(ss.str());
			}
		}
		columnInfoList_.push_back(columnInfo);
	}

	const ColumnInfoList& TableInfoImpl::GetColumns() const {
		return columnInfoList_;
	}

	bool TableInfoImpl::IsPrimaryKey() const {
		for (const ColumnInfo& info : columnInfoList_) {
			if (info.IsPrimaryKey()) {
				return true;
			}
		}
		return false;
	}

	const std::string& TableInfoImpl::GetPrimaryKey() const {
		for (const ColumnInfo& info : columnInfoList_) {
			if (info.IsPrimaryKey()) {
				return info.GetColumnName();
			}
		}
		std::stringstream ss;
		ss << "TableInfoImpl::GetPrimaryKey table(" << tableName_ << ") has no primary key.";
		throw std::invalid_argument(ss.str());
	}

	bool TableInfoImpl::IsColumn(std::string_view columnName) const {
		for (const ColumnInfo& info : columnInfoList_) {
			if (info.GetColumnName() == columnName) {
				return true;
			}
		}
		return false;
	}

	const ColumnInfo& TableInfoImpl::GetColumn(std::string_view columnName) const {
		for (const ColumnInfo& info : columnInfoList_) {
			if (info.GetColumnName() == columnName) {
				return info;
			}
		}
		std::stringstream ss;
		ss << "TableInfoImpl::GetColumn column(" << columnName << ") not found.";
		throw std::invalid_argument(ss.str());
	}

	const std::string& TableInfoImpl::GetTableName() const {
		return tableName_;
	}

	void TableInfoImpl::AddUniqueConstraintHelper(const TableUniqueValues& tableUniqueValues) {
		if (tableUniqueValues.columns.empty()) {
			throw std::invalid_argument(
				"TableInfoImpl::AddUniqueConstraintHelper - "
				"unique constraint must have at least one column.");
		}
		tableUniqueValuesArray_.push_back(tableUniqueValues);
	}

	const TableUniqueValuesArray& TableInfoImpl::GetTableUniqueValues() const {
		return tableUniqueValuesArray_;
	}

	void TableInfoImpl::AddCheckConstraint(
		std::string_view name, std::string_view expression) {
		if (expression.empty()) {
			throw std::invalid_argument(
				"TableInfoImpl::AddCheckConstraint - "
				"check constraint must have a non-empty expression.");
		}
		tableCheckConstraintArray_.push_back(
			{ std::string(name), std::string(expression) });
	}

	const TableCheckConstraintArray& TableInfoImpl::GetTableCheckConstraints() const {
		return tableCheckConstraintArray_;
	}

	TableInfo::TableInfo(std::string_view tableName) : impl_(std::make_shared<TableInfoImpl>(tableName)) {}

	TableInfo TableInfo::Clone() const {
		TableInfo copy(GetTableName());
		// Replace the freshly constructed (empty) impl with a deep copy of ours.
		copy.impl_ = std::make_shared<TableInfoImpl>(*impl_);
		return copy;
	}

	void TableInfo::AddColumn(const ColumnInfo& columnInfo) {
		impl_->AddColumn(columnInfo);
	}

	const ColumnInfoList& TableInfo::GetColumns() const {
		return impl_->GetColumns();
	}

	bool TableInfo::IsPrimaryKey() const {
		return impl_->IsPrimaryKey();
	}

	const std::string& TableInfo::GetPrimaryKey() const {
		return impl_->GetPrimaryKey();
	}

	bool TableInfo::IsColumn(std::string_view columnName) const {
		return impl_->IsColumn(columnName);
	}

	const ColumnInfo& TableInfo::GetColumn(std::string_view columnName) const {
		return impl_->GetColumn(columnName);
	}

	const std::string& TableInfo::GetTableName() const {
		return impl_->GetTableName();
	}

	void TableInfo::AddUniqueConstraintHelper(const TableUniqueValues& tableUniqueValues) {
		impl_->AddUniqueConstraintHelper(tableUniqueValues);
	}

	const TableUniqueValuesArray& TableInfo::GetTableUniqueValues() const {
		return impl_->GetTableUniqueValues();
	}

	void TableInfo::AddCheckConstraint(
		std::string_view name, std::string_view expression) {
		impl_->AddCheckConstraint(name, expression);
	}

	const TableCheckConstraintArray& TableInfo::GetTableCheckConstraints() const {
		return impl_->GetTableCheckConstraints();
	}

}  // namespace DbSchema
