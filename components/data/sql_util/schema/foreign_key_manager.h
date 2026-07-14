#pragma once

#include <string_view>

#include "sql_util/database_common.h"

namespace DbSchema {
	
	struct TableColumnPair {
		std::string tableName;
		std::string columnName;
	};

	inline bool operator==(const TableColumnPair& ref1, const TableColumnPair& ref2) {
		return ref1.tableName == ref2.tableName
			&& ref1.columnName == ref2.columnName;
	}

	using TableColumnPairList = std::vector<TableColumnPair>;

	class ForeignKeyManagerImpl;

	// Manages foreign key references
	class ForeignKeyManager {
	public:
		ForeignKeyManager();
		ForeignKeyManager(const ForeignKeyManager&) = default;
		ForeignKeyManager& operator=(const ForeignKeyManager&) = default;
		~ForeignKeyManager() = default;

		// Deep copy. The ordinary copy shares the pImpl (a copy reflects the
		// original's later reference additions); Clone() returns a fully
		// independent ForeignKeyManager. Used by DatabaseInfo::Clone().
		ForeignKeyManager Clone() const;

		// Denotes a foreign key reference from child field to parent
		void AddForeignKeyReference(
			const TableColumnPair& parent,
			const TableColumnPair& child,
			bool nullable = false);
		// Returns a list of all foreign key child references to the given
		// parent field
		TableColumnPairList GetChildReferences(
			const TableColumnPair& parent) const;
		// Returns a list of all foreign key child references to the given
		// parent table
		TableColumnPairList GetChildReferences(
			std::string_view parentTable) const;
		// Determines if the given table has no foreign key references
		bool IsRootTable(std::string_view tableName) const;
		// Lists all the references to parent foreign keys for a given table
		TableColumnPairList GetForeignReferenceFields(
			std::string_view childTable) const;
		// Determines if the given field is a foreign key reference
		bool HasForeignReference(const TableColumnPair& fieldForeignReference) const;
		// Returns the foreign key reference to the parent for a given child
		TableColumnPair GetForeignReferenceOfField(
			const TableColumnPair& fieldForeignReference) const;
		// Returns whether the foreign key reference is nullable
		bool IsNullable(
			const TableColumnPair& parent,
			const TableColumnPair& child) const;

	private:
		std::shared_ptr< ForeignKeyManagerImpl> impl_;
	};

}  // namespace DbSchema
