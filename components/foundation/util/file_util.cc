#include "file_util.h"

#include <iostream>
#include <fstream>
#include <filesystem>


// Running tests occurs from different locations based on os and build config
std::filesystem::path FindPath(std::string_view fileName) {
  std::filesystem::path rel{ fileName };
  std::filesystem::path cur, res = std::filesystem::weakly_canonical(rel);
  const std::filesystem::path up{ ".." };
  do {
    if (std::filesystem::exists(res)) {
      return res;
    }
    cur = res;
    rel = up / rel;
    res = std::filesystem::weakly_canonical(rel);
  } while (res != cur);
  rel = fileName;
  throw std::filesystem::filesystem_error(std::string{ "unable to find file" }, rel, std::error_code{-1, std::iostream_category()});
}

std::string ReadFile(std::string_view filePath)
{

    std::filesystem::path fixedFilePath = FindPath(filePath);
    std::ifstream file(fixedFilePath, std::ios::in);
    if (file.is_open())
    {
        size_t fileSize = std::filesystem::file_size(fixedFilePath);
        std::string contents(fileSize, 0);
        file.read(contents.data(), fileSize);
        file.close();
        return contents;
    }
    return std::string{};
}
