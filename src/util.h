#ifndef UTIL_H
#define UTIL_H

#include <algorithm>
#include <string>
#include <vector>

namespace util {
void ltrim(std::string &str) {
  str.erase(str.begin(),
            std::find_if(str.begin(), str.end(),
                         [](unsigned char ch) { return !std::isspace(ch); }));
}

void rtrim(std::string &str) {
  str.erase(std::find_if(str.rbegin(), str.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            str.end());
}

void trim(std::string &str) {
  ltrim(str);
  rtrim(str);
}

std::vector<std::string> split(std::string str, std::string delim) {
  std::vector<std::string> res;

  size_t pos_begin = 0;
  size_t pos_end;
  std::string token;

  while ((pos_end = str.find(delim, pos_begin)) != std::string::npos) {
    token = str.substr(pos_begin, pos_end - pos_begin);
    pos_begin = pos_end + delim.length();
    res.push_back(token);
  }
  res.push_back(str.substr(pos_begin));

  return res;
}
} // namespace util

#endif
