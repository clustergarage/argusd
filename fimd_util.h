#ifndef _FIMD_UTIL_H
#define _FIMD_UTIL_H

#include <sstream>
#include <string>
#include <vector>

namespace fimd {
class FimdUtil {
public:
    static int getPidForContainer(std::string id);

    static inline std::string eraseSubstr(const std::string &s, const std::string &t) {
        std::string str(s.c_str());
        size_t pos = s.find(t);
        if (pos != std::string::npos) {
            str.erase(pos, t.length());
        }
        return str;
    }

private:
    static std::vector<std::string> fglob(const std::string &pattern);
    static std::string findCgroupMountpoint(std::string cgroup_type);
    static std::string getThisCgroup(std::string cgroup_type);

private:
    static inline std::vector<std::string> split(const std::string &s, char delim) {
        std::stringstream ss(s);
        std::string item;
        std::vector<std::string> tokens;
        while (std::getline(ss, item, delim)) {
            tokens.push_back(item);
        }
        return tokens;
    }
};
} // namespace fimd

#endif
