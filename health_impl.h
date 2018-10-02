#ifndef _HEALTHIMPL_H
#define _HEALTHIMPL_H

#include "fim-proto/c++/health.grpc.pb.h"

namespace fimdhealth {
class HealthImpl final : public grpc::health::v1::Health::Service {
public:
    explicit HealthImpl() = default;
    ~HealthImpl() = default;

    grpc::Status Check(grpc::ServerContext *context, const grpc::health::v1::HealthCheckRequest *request,
        grpc::health::v1::HealthCheckResponse *response) override;
    void SetStatus(const grpc::string &service, grpc::health::v1::HealthCheckResponse::ServingStatus status);
    void SetAll(grpc::health::v1::HealthCheckResponse::ServingStatus status);
    void ClearStatus(const std::string &service);
    void ClearAll();

private:
    std::mutex mu_;
    std::map<const grpc::string, grpc::health::v1::HealthCheckResponse::ServingStatus> statuses_;
};
} // namespace fimdhealth

#endif
