#ifndef __FILE_UTIL_H__
#define __FILE_UTIL_H__

#include <string>
#include <string_view>

// Loads the text data from the given fileName
std::string ReadFile(std::string_view fileName);

#endif  // __FILE_UTIL_H__