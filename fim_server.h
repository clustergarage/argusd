#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include <glob.h>
#include <string>
#include <vector>

#include "fim.grpc.pb.h"

class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status NewWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;

private:
	// @TODO: vector of watchers to use for kill/modify operations
};

// @TODO: split these out into util class

inline std::string erase_substr(const std::string &s, const std::string &t) {
    std::string str(s.c_str());
    size_t pos = s.find(t);
    if (pos != std::string::npos) {
        str.erase(pos, t.length());
    }
    return str;
}

inline std::vector<std::string> split(const std::string &s, char delim) {
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> tokens;
    while (std::getline(ss, item, delim)) {
        tokens.push_back(item);
    }
    return tokens;
}

std::vector<std::string> glob(const std::string &pattern) {
    std::vector<std::string> filenames;
    glob_t glob_result;
    int err = glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
    if (err == 0) {
		for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
			filenames.push_back(std::string(glob_result.gl_pathv[i]));
		}
	}
	globfree(&glob_result);
    return filenames;
}

#endif
