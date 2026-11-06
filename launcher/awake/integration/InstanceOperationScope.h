// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PhysicalInstanceLease.h"
#include <utility>
namespace Awake::Integration {
class InstanceOperationScope final {
   public:
    explicit InstanceOperationScope(std::shared_ptr<PhysicalInstanceLease> lease) : m_previous(std::exchange(s_current, std::move(lease))) {}
    ~InstanceOperationScope() { s_current = std::move(m_previous); }
    InstanceOperationScope(const InstanceOperationScope&) = delete;
    InstanceOperationScope& operator=(const InstanceOperationScope&) = delete;
    static std::shared_ptr<PhysicalInstanceLease> current() { return s_current; }
   private:
    inline static thread_local std::shared_ptr<PhysicalInstanceLease> s_current;
    std::shared_ptr<PhysicalInstanceLease> m_previous;
};
}  // namespace Awake::Integration
