#ifndef _FIMD_UTIL_H
#define _FIMD_UTIL_H

#include <sstream>
#include <string>
#include <vector>

namespace fimd {
class FimdUtil {
public:
    static std::string findContainerRuntime(const std::string containerId);
    static int getPidForContainer(std::string id, const std::string runtime);

    /**
     * helper function to erase substring `t` from string `s`
     */
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
    static std::string findCgroupMountpoint(const std::string cgroupType);
    static std::string getThisCgroup(const std::string cgroupType, const std::string runtime);

private:
    /**
     * helper function to split string `s` by a character deliminator
     */
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
