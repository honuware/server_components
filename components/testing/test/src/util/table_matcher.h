#ifndef __TABLE_MATCHER_H__
#define __TABLE_MATCHER_H__

#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_helper.h"

class PostGresValue {
public:
    enum Type {PV_TYPE_INT, PV_TYPE_BOOL, PV_TYPE_DOUBLE, PV_TYPE_STRING, PV_TYPE_NULL};

    PostGresValue();
    PostGresValue(const PostGresValue& ref);
    PostGresValue& operator=(const PostGresValue& ref);
    ~PostGresValue();

    PostGresValue(int val);
    PostGresValue(unsigned int val);
    PostGresValue(float val);
    PostGresValue(double val);
    PostGresValue(bool val);
    PostGresValue(const char* val);
    PostGresValue(const std::string& val);
    PostGresValue(std::string_view val);

    void Set(int val);
    void Set(unsigned int val);
    void Set(float val);
    void Set(double val);
    void Set(bool val);
    void Set(const char* val);
    void Set(const std::string& val);
    void Set(std::string_view val);

    Type GetType() const {return type_;}
    bool IsNull() const {return type_ == PV_TYPE_NULL;}

    int GetInt() const;
    double GetDouble() const;
    bool GetBool() const;
    const std::string& GetString() const;

    bool operator==(int val) const;
    bool operator==(unsigned int val) const;
    bool operator==(float val) const;
    bool operator==(double val) const;
    bool operator==(bool val) const;
    bool operator==(const char* val) const;
    bool operator==(const std::string& val) const;
    bool operator==(std::string_view val) const;

    bool IsMatch(int val) const;
    bool IsMatch(unsigned int val) const;
    bool IsMatch(float val) const;
    bool IsMatch(double val) const;
    bool IsMatch(bool val) const;
    bool IsMatch(const char* val) const;
    bool IsMatch(const std::string& val) const;
    bool IsMatch(std::string_view val) const;

    std::string ToString() const;
    std::string Describe() const;

private:
    Type type_ = PV_TYPE_NULL;

    int intVal_ = 0;
    double doubleVal_ = 0.0;
    bool boolVal_ = false;
    std::string stringVal_;
};

using PostGresValueRow = std::vector<PostGresValue>;
using PostGresValueTable = std::vector<PostGresValueRow>;

bool ValidateTable(
    const pqxx::result& res,
    const StringArray& columns,
    const PostGresValueTable& table,
    testing::MatchResultListener* listener);

class PostGresResultMatcher : public ::testing::MatcherInterface<pqxx::result> {
public:
    PostGresResultMatcher(const StringArray& columns, const PostGresValueTable& table);

    bool MatchAndExplain(pqxx::result res, testing::MatchResultListener* listener) const override;
    void DescribeTo(::std::ostream* os) const override;
    void DescribeNegationTo(::std::ostream* os) const override;

private:
    void StreamExpected(::std::ostream* os) const;

    StringArray columns_;
    PostGresValueTable table_;
};

::testing::Matcher<pqxx::result> TableIs(
    const StringArray& columns,
    const PostGresValueTable& table);

#endif  // __TABLE_MATCHER_H__