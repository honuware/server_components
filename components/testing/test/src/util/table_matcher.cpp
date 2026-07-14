#include "table_matcher.h"

#include <cassert>
#include <sstream>

PostGresValue::PostGresValue() {
}

PostGresValue::PostGresValue(const PostGresValue& ref) 
: type_(ref.type_), intVal_(ref.intVal_), doubleVal_(ref.doubleVal_),
  boolVal_(ref.boolVal_), stringVal_(ref.stringVal_) {
}

PostGresValue& PostGresValue::operator=(const PostGresValue& ref) {
    if(&ref != this) {
        type_ = ref.type_;
        intVal_ = ref.intVal_;
        doubleVal_ = ref.doubleVal_;
        boolVal_ = ref.boolVal_;
        stringVal_ = ref.stringVal_;
    }
    return *this;
}

PostGresValue::~PostGresValue() {
}

PostGresValue::PostGresValue(int val) : type_(PV_TYPE_INT), intVal_(val) {
}

PostGresValue::PostGresValue(unsigned int val) : type_(PV_TYPE_INT), intVal_(static_cast<int>(val)) {
}

PostGresValue::PostGresValue(float val) : type_(PV_TYPE_DOUBLE), doubleVal_(static_cast<double>(val)) {
}

PostGresValue::PostGresValue(double val) : type_(PV_TYPE_DOUBLE), doubleVal_(val) {
}

PostGresValue::PostGresValue(bool val) : type_(PV_TYPE_BOOL), boolVal_(val) {
}

PostGresValue::PostGresValue(const char* val) : type_(PV_TYPE_STRING), stringVal_(val) {
}

PostGresValue::PostGresValue(const std::string& val) : type_(PV_TYPE_STRING), stringVal_(val) {
}

PostGresValue::PostGresValue(std::string_view val) : type_(PV_TYPE_STRING), stringVal_(std::string(val)) {
}

void PostGresValue::Set(int val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(unsigned int val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(float val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(double val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(bool val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(const char* val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(const std::string& val) {
    *this = PostGresValue(val);
}

void PostGresValue::Set(std::string_view val) {
    *this = PostGresValue(val);
}

int PostGresValue::GetInt() const {
    assert(type_ == PV_TYPE_INT);
    return intVal_;
}

double PostGresValue::GetDouble() const {
    assert(type_ == PV_TYPE_DOUBLE);
    return doubleVal_;
}

bool PostGresValue::GetBool() const {
    assert(type_ == PV_TYPE_BOOL);
    return boolVal_;
}

const std::string& PostGresValue::GetString() const {
    assert(type_ == PV_TYPE_STRING);
    return stringVal_;
}

bool PostGresValue::operator==(int val) const {
    if(type_ != PV_TYPE_INT) return false;
    return val == intVal_;
}

bool PostGresValue::operator==(unsigned int val) const {
    if(type_ != PV_TYPE_INT) return false;
    return val == intVal_;
}

bool PostGresValue::operator==(float val) const {
    if(type_ != PV_TYPE_DOUBLE) return false;
    return val == doubleVal_;
}

bool PostGresValue::operator==(double val) const {
    if(type_ != PV_TYPE_DOUBLE) return false;
    return val == doubleVal_;
}

bool PostGresValue::operator==(bool val) const {
    if(type_ != PV_TYPE_BOOL) return false;
    return val == boolVal_;
}

bool PostGresValue::operator==(const char* val) const {
    if(type_ != PV_TYPE_STRING) return false;
    return stringVal_ == val;
}

bool PostGresValue::operator==(const std::string& val) const {
    if(type_ != PV_TYPE_STRING) return false;
    return stringVal_ == val;
}

bool PostGresValue::operator==(std::string_view val) const {
    if(type_ != PV_TYPE_STRING) return false;
    return stringVal_ == val;
}

bool PostGresValue::IsMatch(int val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(unsigned int val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(float val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(double val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(bool val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(const char* val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(const std::string& val) const {
    return *this == val;
}

bool PostGresValue::IsMatch(std::string_view val) const {
    return *this == val;
}

std::string PostGresValue::ToString() const {
    std::stringstream ss;
    switch(type_) {
    case PV_TYPE_INT:
        ss << intVal_;
        break;
    case PV_TYPE_BOOL:
        if(boolVal_) ss << "true";
        else ss << "false";
        break;
    case PV_TYPE_DOUBLE:
        ss << doubleVal_;
        break;
    case PV_TYPE_STRING:
        ss << stringVal_;
        break;
    case PV_TYPE_NULL:
        ss << "null";
        break;
    }
    return ss.str();
}

std::string PostGresValue::Describe() const {
    std::stringstream ss;
    switch(type_) {
    case PV_TYPE_INT:
        ss << "int(" << ToString() << ")";
        break;
    case PV_TYPE_BOOL:
        ss << "bool(" << ToString() << ")";
        break;
    case PV_TYPE_DOUBLE:
        ss << "double(" << ToString() << ")";
        break;
    case PV_TYPE_STRING:
        ss << "string(" << ToString() << ")";
        break;
    case PV_TYPE_NULL:
        ss << ToString();
        break;
    }
    return ss.str();

}

bool TryMatchValue(
    const pqxx::row& row,
    const std::string& column,
    const PostGresValue& value) {
    const pqxx::field& pgField = row[column];
    switch(value.GetType()) {
    case PostGresValue::PV_TYPE_INT:
        return value.IsMatch(pgField.as<int>());
    case PostGresValue::PV_TYPE_BOOL:
        return value.IsMatch(pgField.as<bool>());
    case PostGresValue::PV_TYPE_DOUBLE:
        return value.IsMatch(pgField.as<double>());
    case PostGresValue::PV_TYPE_STRING:
        return value.IsMatch(pgField.as<std::string>());
    case PostGresValue::PV_TYPE_NULL:
        return true;
    default:
        return false;
    }
}

bool TryMatchRow(
    const pqxx::row& row,
    const StringArray& columns,
    const PostGresValueRow& valueRow) {
    for(size_t i = 0; i < columns.size(); ++i) {
        if(!TryMatchValue(row, columns[i], valueRow[i])) {
            return false;
        }
    }
    return true;
}

bool ValidateRow(
    const pqxx::row& row,
    std::vector<bool>& rowBitvector,
    const StringArray& columns,
    const PostGresValueTable& table) {
    // Walk the whole table for items that are not already
    // marked as matched in the bitvector and attempt to 
    // match the match table.
    for(size_t i = 0; i < table.size(); ++i) {
        // See if we have already matched this row
        if(rowBitvector[i]) continue;

        if(TryMatchRow(row, columns, table[i])) {
            rowBitvector[i] = true;
            return true;
        }
    }
    return false;
}

template <class Out>
void PrintRow(Out& out, const pqxx::row& row) {
    bool first = true;
    for(const pqxx::field& pgField : row) {
        if(first) {
            first = false;
        } else {
            out << ", ";
        }
        out << pgField.name() << "(" << pgField.c_str() << ")";
    }
}

bool ValidateTable(
    const pqxx::result& res,
    const StringArray& columns,
    const PostGresValueTable& table,
    testing::MatchResultListener* listener) {
    if(table.size() != res.size()) {
        *listener << "Expected data has " << table.size()
            << " rows of data and database result has "
            << res.size();
        return false;
    }
    // This is used to mark matched rows
    std::vector<bool> rowBitvector(table.size(), false);
    for(const pqxx::row& row : res) {
        if(!ValidateRow(row, rowBitvector, columns, table)) {
            *listener << "containing unexpected row:\n";
            PrintRow(*listener, row);
            return false;
        }
    }
    return true;
}

PostGresResultMatcher::PostGresResultMatcher(const StringArray& columns, const PostGresValueTable& table) 
: columns_(columns), table_(table) {
}

bool PostGresResultMatcher::MatchAndExplain(pqxx::result res, testing::MatchResultListener* listener) const {
    return ValidateTable(res, columns_, table_, listener);
}

void PostGresResultMatcher::DescribeTo(::std::ostream* os) const {
    *os << "Result set should match the values provided.\n";
    StreamExpected(os);
}

void PostGresResultMatcher::DescribeNegationTo(::std::ostream* os) const {
    *os << "Result set should not match the values provided.\n";
    StreamExpected(os);
}

template <class Out>
Out& operator<<(Out& out, const PostGresValue& value) {
    return out << value.ToString();
}

template <class Out, class Coll>
void PrintList(Out& out, const Coll& coll) {
    bool first = true;
    for(const auto& val : coll) {
        if(first) {
            first = false;
        } else {
            out << ", ";
        }
        out << val;
    }
    out << "\n";
}

void PostGresResultMatcher::StreamExpected(::std::ostream* os) const {
    *os << "Columns:\n";
    PrintList(*os, columns_);
    for(const auto& row : table_) {
        PrintList(*os, row);
    }
}

::testing::Matcher<pqxx::result> TableIs(
    const StringArray& columns,
    const PostGresValueTable& table) {
    return ::testing::MakeMatcher(
        new PostGresResultMatcher(columns, table));
}

