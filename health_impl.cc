#include "health_impl.h"

#include <mutex>

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpc++/server_context.h>

namespace argusdhealth {
/**
 * Performs a health status check.
 *
 * @param context
 * @param request
 * @param response
 * @return
 */
grpc::Status HealthImpl::Check(grpc::ServerContext *context [[maybe_unused]], const grpc::health::v1::HealthCheckRequest *request,
    grpc::health::v1::HealthCheckResponse *response) {

    std::lock_guard<std::mutex> lock(mux_);
    // If the service is empty we assume that the client wants to check the
    // server's status.
    if (request->service().empty()) {
        response->set_status(grpc::health::v1::HealthCheckResponse::SERVING);
        return grpc::Status::OK;
    }

    auto iter = statuses_.find(request->service());
    if (iter == statuses_.end()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "");
    }
    response->set_status(iter->second);

    return grpc::Status::OK;
}

/**
 * Sets the health status for a given service.
 *
 * @param service
 * @param status
 */
void HealthImpl::SetStatus(const grpc::string &service, grpc::health::v1::HealthCheckResponse::ServingStatus status) {
    std::lock_guard<std::mutex> lock(mux_);
    statuses_[service] = status;
}

/**
 * Sets the health status for all services.
 *
 * @param status
 */
void HealthImpl::SetAll(grpc::health::v1::HealthCheckResponse::ServingStatus status) {
    std::lock_guard<std::mutex> lock(mux_);
    for (auto iter : statuses_) {
        iter.second = status;
    }
}

/**
 * Clears the health status for a given service.
 *
 * @param service
 */
void HealthImpl::ClearStatus(const std::string &service) {
    std::lock_guard<std::mutex> lock(mux_);
    statuses_.erase(service);
}

/**
 * Clears the health status for all services.
 */
void HealthImpl::ClearAll() {
    std::lock_guard<std::mutex> lock(mux_);
    statuses_.clear();
}
} // namespace argusdhealth
