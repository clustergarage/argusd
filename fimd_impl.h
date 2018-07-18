#ifndef _FIMDIMPL_H
#define _FIMDIMPL_H

#include <future>
#include <thread>
#include <vector>

#include "fimd_util.h"
#include "fim-proto/c++/fim.grpc.pb.h"

class FimdImpl final : public fim::Fimd::Service {
public:
    explicit FimdImpl() = default;
    ~FimdImpl() = default;

    grpc::Status CreateWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::FimdHandle *response) override;
    grpc::Status DestroyWatch(grpc::ServerContext *context, const fim::FimdConfig *request, fim::Empty *response) override;

private:
    std::vector<int> getPidsFromRequest(const fim::FimdConfig *request);
    std::shared_ptr<fim::FimdHandle> findFimdWatcherByPids(const std::string hostUid, const std::vector<int> pids);
	char **getPathArrayFromSubject(const int pid, const fim::FimWatcherSubject subject);
	uint32_t getEventMaskFromSubject(const fim::FimWatcherSubject subject);
	void createInotifyWatcher(const fim::FimWatcherSubject subject, char **patharr, uint32_t event_mask,
		google::protobuf::RepeatedField<google::protobuf::int32> *procFds);
	void sendKillSignalToWatcher(std::shared_ptr<fim::FimdHandle> watcher);
    void eraseEventProcessfd(google::protobuf::RepeatedField<google::protobuf::int32> *eventProcessfds, const int processfd);

	inline const std::string cleanContainerId(const std::string &containerId) const {
		return FimdUtil::eraseSubstr(containerId, "docker://");
	}

private:
    std::vector<std::shared_ptr<fim::FimdHandle>> m_watchers;
};

#endif