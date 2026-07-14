#include "foreign_key_manager.h"

namespace DbSchema {

	class ForeignKeyManagerImpl {
	public:
		ForeignKeyManagerImpl() = default;
		// Copyable so ForeignKeyManager::Clone() can deep-copy. The single member
		// is a vector of value structs, so the defaulted copy is a genuine deep
		// copy. Assignment stays deleted.
		ForeignKeyManagerImpl(const ForeignKeyManagerImpl&) = default;
		ForeignKeyManagerImpl& operator=(const ForeignKeyManagerImpl&) = delete;
		~ForeignKeyManagerImpl() = default;

		void AddForeignKeyReference(
			const TableColumnPair& parent, const TableColumnPair& child,
			bool nullable = false);
		TableColumnPairList GetChildReferences(
			const TableColumnPair& parent) const;
		TableColumnPairList GetChildReferences(
			std::string_view parentTable) const;
		bool IsRootTable(std::string_view tableName) const;
		TableColumnPairList GetForeignReferenceFields(
			std::string_view childTable) const;
		bool HasForeignReference(
			const TableColumnPair& fieldForeignReference) const;
		TableColumnPair GetForeignReferenceOfField(
			const TableColumnPair& fieldForeignReference) const;

		bool IsNullable(
			const TableColumnPair& parent,
			const TableColumnPair& child) const;

	private:
		struct ForeignKeyReference {
			TableColumnPair parent;
			TableColumnPair child;
			bool nullable = false;
		};
		std::vector<ForeignKeyReference> foreignKeyReferences_;
	};

	void ForeignKeyManagerImpl::AddForeignKeyReference(
		const TableColumnPair& parent, const TableColumnPair& child,
		bool nullable) {
		for (const auto& ref : foreignKeyReferences_) {
			if (ref.child.tableName == child.tableName
				&& ref.child.columnName == child.columnName) {
				// Already present
				if (ref.parent.tableName != parent.tableName
					&& ref.parent.columnName != parent.columnName) {
					std::stringstream ss;
					ss << "ForeignKeyManagerImpl::AddForeignKeyReference child("
						<< child.tableName << ", " << child.columnName
						<< ") is already present for (" << ref.parent.tableName
						<< ", " << ref.parent.columnName
						<< ") and trying to add (" << parent.tableName
						<< ", " << parent.columnName << ")";
					throw std::invalid_argument(ss.str());
				}
				return;
			}
		}
		foreignKeyReferences_.push_back({ parent, child, nullable });
	}

	TableColumnPairList ForeignKeyManagerImpl::GetChildReferences(
		const TableColumnPair& parent) const {
		TableColumnPairList ret;

		for (const auto& ref : foreignKeyReferences_) {
			if (ref.parent.tableName == parent.tableName
				&& ref.parent.columnName == parent.columnName) {
				ret.push_back(ref.child);
			}
		}

		return ret;
	}

	TableColumnPairList ForeignKeyManagerImpl::GetChildReferences(
		std::string_view parentTable) const {
		TableColumnPairList ret;

		for (const auto& ref : foreignKeyReferences_) {
			if (ref.parent.tableName == parentTable) {
				ret.push_back(ref.child);
			}
		}

		return ret;
	}

	bool ForeignKeyManagerImpl::IsRootTable(std::string_view tableName) const {
		for (const auto& ref : foreignKeyReferences_) {
			if (ref.child.tableName == tableName) {
				return false;
			}
		}
		return true;
	}

	TableColumnPairList ForeignKeyManagerImpl::GetForeignReferenceFields(
		std::string_view childTable) const {
		TableColumnPairList ret;

		for (const auto& ref : foreignKeyReferences_) {
			if (ref.child.tableName == childTable) {
				ret.push_back(ref.parent);
			}
		}

		return ret;
	}

	bool ForeignKeyManagerImpl::HasForeignReference(
		const TableColumnPair& fieldForeignReference) const {
		for (const auto& ref : foreignKeyReferences_) {
			if (ref.child.tableName == fieldForeignReference.tableName
				&& ref.child.columnName == fieldForeignReference.columnName) {
				return true;
			}
		}
		return false;
	}

	TableColumnPair ForeignKeyManagerImpl::GetForeignReferenceOfField(
		const TableColumnPair& fieldForeignReference) const {
		for (const auto& ref : foreignKeyReferences_) {
			if (ref.child.tableName == fieldForeignReference.tableName
				&& ref.child.columnName == fieldForeignReference.columnName) {
				return ref.parent;
			}
		}
		std::stringstream ss;
		ss << "ForeignKeyManagerImpl::GetForeignReferenceOfField unable to find"
			<< " foreign key reference for ("
			<< fieldForeignReference.tableName
			<< ", " << fieldForeignReference.columnName << ")";
		throw std::invalid_argument(ss.str());
	}

	bool ForeignKeyManagerImpl::IsNullable(
		const TableColumnPair& parent,
		const TableColumnPair& child) const {
		for (const auto& ref : foreignKeyReferences_) {
			if (ref.parent.tableName == parent.tableName
				&& ref.parent.columnName == parent.columnName
				&& ref.child.tableName == child.tableName
				&& ref.child.columnName == child.columnName) {
				return ref.nullable;
			}
		}
		std::stringstream ss;
		ss << "ForeignKeyManagerImpl::IsNullable unable to find"
			<< " foreign key reference for parent ("
			<< parent.tableName << ", " << parent.columnName
			<< ") child (" << child.tableName
			<< ", " << child.columnName << ")";
		throw std::invalid_argument(ss.str());
	}

	ForeignKeyManager::ForeignKeyManager()
		: impl_(std::make_shared<ForeignKeyManagerImpl>()) {}

	ForeignKeyManager ForeignKeyManager::Clone() const {
		ForeignKeyManager copy;
		// Replace the freshly constructed (empty) impl with a deep copy of ours.
		copy.impl_ = std::make_shared<ForeignKeyManagerImpl>(*impl_);
		return copy;
	}

	void ForeignKeyManager::AddForeignKeyReference(
		const TableColumnPair& parent, const TableColumnPair& child,
		bool nullable) {
		impl_->AddForeignKeyReference(parent, child, nullable);
	}

	TableColumnPairList ForeignKeyManager::GetChildReferences(
		const TableColumnPair& parent) const {
		return impl_->GetChildReferences(parent);
	}

	TableColumnPairList ForeignKeyManager::GetChildReferences(
		std::string_view parentTable) const {
		return impl_->GetChildReferences(parentTable);
	}

	bool ForeignKeyManager::IsRootTable(std::string_view tableName) const {
		return impl_->IsRootTable(tableName);
	}

	TableColumnPairList ForeignKeyManager::GetForeignReferenceFields(
		std::string_view childTable) const {
		return impl_->GetForeignReferenceFields(childTable);
	}

	bool ForeignKeyManager::HasForeignReference(
		const TableColumnPair& fieldForeignReference) const {
		return impl_->HasForeignReference(fieldForeignReference);
	}

	TableColumnPair ForeignKeyManager::GetForeignReferenceOfField(
		const TableColumnPair& fieldForeignReference) const {
		return impl_->GetForeignReferenceOfField(fieldForeignReference);
	}

	bool ForeignKeyManager::IsNullable(
		const TableColumnPair& parent,
		const TableColumnPair& child) const {
		return impl_->IsNullable(parent, child);
	}

}  // namespace DbSchema
