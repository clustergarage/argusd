/**
 * MIT License
 *
 * Copyright (c) 2018 ClusterGarage
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __ARGUSD_UTIL_H__
#define __ARGUSD_UTIL_H__

#include <sstream>
#include <string>
#include <vector>

namespace argusd {
class ArgusdUtil {
public:
    static std::string findContainerRuntime(std::string containerId);
    static int getPidForContainer(std::string id, std::string runtime);

    /**
     * Helper function to erase substring `sub` from string `str`.
     *
     * @param str
     * @param sub
     */
    static inline void eraseSubstr(std::string &str, const std::string &sub) {
        size_t pos = str.find(sub);
        if (pos != std::string::npos) {
            str.erase(pos, sub.length());
        }
    }

private:
    static std::vector<std::string> fglob(const std::string &pattern);
    static std::string findCgroupMountpoint(std::string cgroupType);
    static std::string getThisCgroup(std::string cgroupType);

private:
    /**
     * Helper function to split string `str` by a character deliminator.
     *
     * @param str
     * @param delim
     * @return
     */
    static inline std::vector<std::string> split(const std::string &str, const char delim) {
        std::stringstream ss(str);
        std::string item;
        std::vector<std::string> tokens;
        while (std::getline(ss, item, delim)) {
            tokens.push_back(item);
        }
        return tokens;
    }
};
} // namespace argusd

#endif
