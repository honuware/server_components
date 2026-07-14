#include "types.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <boost/url/encode.hpp>
#include <boost/url/decode_view.hpp>
#include <boost/url/rfc/pchars.hpp> 

KeyValueTable MakeKeyValueTable(
    std::initializer_list<std::pair<std::string_view, std::string_view>> items) {
    KeyValueTable keyValueTable;
    for (const auto& [key, value] : items) {
        keyValueTable.insert({
            static_cast<std::string>(key),
            static_cast<std::string>(value) });
    }
    return keyValueTable;
}
void AddToKeyValueTable(
    KeyValueTable& keyValueTable,
    std::string_view key,
    std::string_view value) {
    keyValueTable.insert({
        static_cast<std::string>(key),
        static_cast<std::string>(value) });
}

DataResults MakeDataResults(
    const StringArray& columnNames, const StringTable& dataTable) {
    return MakeDataResultsParamterized(
        columnNames,
        [](const StringArray& sa, size_t i) {return sa[i]; },
        [](const StringArray& sa) {return sa.size(); },
        dataTable,
        [](const StringTable& st, size_t row, size_t col) {return st[row][col]; },
        [](const StringTable& st) {return st.size(); });
}

DataResults MakeDataResultsStringView(
    const StringViewArray& columnNames, const StringTable& dataTable) {
    StringArray cols;
    for(const auto& col : columnNames) {
        cols.push_back(std::string(col));
    }
    return MakeDataResults(cols, dataTable);
}


DataResults MakeDataResults(const KeyValueTable& keyValueTable) {
    DataResults dataResults;
    for (const auto& [key, value] : keyValueTable) {
        dataResults.sortedColumnNames.push_back(key);
    }
    StringArray row;
    for (const auto& columnName : dataResults.sortedColumnNames) {
        row.push_back(keyValueTable.at(columnName));
    }
    if (!keyValueTable.empty()) {
        dataResults.dataTable.push_back(row);
    }
    return dataResults;
}

DataResults MakeDataResultsFromKeyValueTableArray(
    const KeyValueTableArray& keyValueTableArray) {
    DataResults dataResults;
    if (keyValueTableArray.empty()) {
        return dataResults;
    }
    // Make sure that the keys are the same across all tables
    std::set<std::string> keySet;
    for (const auto& [key, value] : keyValueTableArray[0]) {
        keySet.insert(key);
    }
    StringTable stringTable;
    for (const auto& keyValueTable : keyValueTableArray) {
        StringArray row;
        for(const auto& [key, value] : keyValueTable) {
            if (keySet.find(key) == keySet.end()) {
                throw std::runtime_error("Inconsistent keys in KeyValueTableArray");
            }
            row.push_back(value);
        }
        stringTable.push_back(std::move(row));
    }
    return MakeDataResults(std::vector<std::string>(keySet.begin(), keySet.end()), stringTable);
}


std::string GetDataResultsValue(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index /* = 0 */) {
    int col = IndexOfColumn(dataResults, columnName);
    if (col < 0) {
        return std::string();
    }
    if (index >= dataResults.dataTable.size()) {
        return std::string();
    }
    const auto& row = dataResults.dataTable[index];
    size_t colIndex = static_cast<size_t>(col);
    if (colIndex >= row.size()) {
        return std::string();
    }
    return row[colIndex];
}

int GetDataResultsValueAsInt(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index /* = 0 */) {
    std::string v = GetDataResultsValue(dataResults, columnName, index);
    return std::stoi(v); // let it throw on failure
}

int64_t GetDataResultsValueAsInt64(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index /* = 0 */) {
    std::string v = GetDataResultsValue(dataResults, columnName, index);
    return std::stoll(v); // let it throw on failure
}

bool GetDataResultsValueAsBool(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index /* = 0 */) {
    return StringToBool(GetDataResultsValue(dataResults, columnName, index));
}

bool GetDataResultsValueIsEmpty(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index /* = 0 */) {
    return GetDataResultsValue(dataResults, columnName, index).empty();
}

KeyValueTableArray KeyValueTableArrayFromDataResults(
    const DataResults& dataResults) {
    KeyValueTableArray result;
    const auto& cols = dataResults.sortedColumnNames;

    for (const auto& row : dataResults.dataTable) {
        KeyValueTable kv;
        const size_t count = std::min(cols.size(), row.size());
        for (size_t i = 0; i < count; ++i) {
            kv.insert({ cols[i], row[i] });
        }
        result.push_back(std::move(kv));
    }
    return result;
}

StringArray StringArrayFromDataResultsColumn(
    const DataResults& dataResults, size_t columnIndex /* = 0 */) {
    StringArray result;
    // Validate against known column count
    if (columnIndex >= dataResults.sortedColumnNames.size()) {
        return result; // invalid column index => empty
    }

    result.reserve(dataResults.dataTable.size());
    for (const auto& row : dataResults.dataTable) {
        if (columnIndex < row.size()) {
            result.push_back(row[columnIndex]);
        } else {
            // Row shorter than expected; keep alignment by inserting empty string
            result.emplace_back();
        }
    }
    return result;
}

StringArray StringArrayFromKeyValueTableArrayColumn(
    const KeyValueTableArray& keyValueTableArray, size_t columnIndex /*= 0*/) {
    StringArray result;
    if(keyValueTableArray.empty()) {
        return result;
    }
    if(columnIndex < 0 || columnIndex >= keyValueTableArray[0].size()) {
        throw std::out_of_range("Invalid column index");
    }
    auto iter = keyValueTableArray[0].begin();
    for (size_t i = 0; i < columnIndex; ++iter, ++i);
    std::string key = iter->first;
    for(const auto& keyValueTable : keyValueTableArray) {
        auto findIter = keyValueTable.find(key);
        if(findIter == keyValueTable.end()) {
            throw std::runtime_error("Inconsistent keys in KeyValueTableArray");
        }
        result.push_back(findIter->second);
    }
    return result;
}

StringSet StringSetFromStringArray(const StringArray& stringArray) {
    StringSet ret;
    for (const std::string& value : stringArray) {
        ret.insert(value);
    }
    return ret;
}

void AddStringArrayToSet(
    StringSet& stringSet, const StringArray& stringArray) {
    for (const std::string& value : stringArray) {
        stringSet.insert(value);
    }
}

bool SetContains(const StringSet& stringSet, std::string_view value) {
    return stringSet.find(std::string(value)) != stringSet.end();
}

std::string StringArrayToCommaSeparatedString(const StringArray& stringArray) {
    std::string ret;
    bool first = true;
    for (const auto& str : stringArray) {
        if (first) {
            first = false;
        }
        else {
            ret += ", ";
        }
        ret += str;
    }
    return ret;
}

std::string StringFromInt(int n) {
    std::stringstream ss;
    ss << n;
    return ss.str();
}

std::string StringFromInt(int64_t n) {
    std::stringstream ss;
    ss << n;
    return ss.str();
}

#ifndef _MSC_VER
std::string StringFromInt(long long n) {
    std::stringstream ss;
    ss << n;
    return ss.str();
}
#endif

std::string StringToLower(std::string_view str) {
    std::string lowerStr(str);
    std::transform(
        lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

std::string StringTrim(std::string_view str) {
    auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!str.empty() && isWs(static_cast<unsigned char>(str.front()))) {
        str.remove_prefix(1);
    }
    while (!str.empty() && isWs(static_cast<unsigned char>(str.back()))) {
        str.remove_suffix(1);
    }
    return std::string(str);
}

bool StringToBool(std::string_view str) {
    std::string lowerStr = StringToLower(str);
    return (lowerStr == "t" || lowerStr == "true" || lowerStr == "1");
}

std::string FormatString(
    std::string_view format,
    const KeyValueTable& keyValueTable) {
    std::string result(format);
    for (const auto& [key, value] : keyValueTable) {
        std::string placeholder = "{" + key + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

std::string URLEncode(std::string_view str) {
    return boost::urls::encode(
        str, boost::urls::pchars, boost::urls::encoding_opts{});
}

std::string URLDecode(std::string_view str) {
    boost::urls::pct_string_view pctStringView(str);
    return pctStringView.decode();
}

std::pair<std::string_view, std::string_view> SplitString(
    std::string_view strToBeSplit, std::string_view delimiter) {
    if (delimiter.empty()) {
        return { strToBeSplit, std::string_view{} };   // no split if empty delimiter
    }

    size_t pos = strToBeSplit.find(delimiter);
    if (pos == std::string_view::npos) {
        return { strToBeSplit, std::string_view{} };   // delimiter not found
    }

    std::string_view left = strToBeSplit.substr(0, pos);
    std::string_view right = strToBeSplit.substr(pos + delimiter.size());
    return { left, right };
}

std::string NormalizeCrLf(std::string_view in) {
    std::string out;
    out.reserve(in.size() * 2);

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\n') {
            if (i == 0 || in[i - 1] != '\r')
                out += '\r';
        }
        out += in[i];
    }
    return out;
}
