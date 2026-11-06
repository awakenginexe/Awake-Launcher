// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <memory>
namespace Awake::Integration {
class PhysicalInstanceLease final {
   public:
    static std::shared_ptr<PhysicalInstanceLease> acquire(const QString& root, const QString& physicalId,
                                                           const QString& lockDirectory, QString* error = nullptr);
    ~PhysicalInstanceLease();
    PhysicalInstanceLease(const PhysicalInstanceLease&) = delete;
    PhysicalInstanceLease& operator=(const PhysicalInstanceLease&) = delete;
    bool pinRoot(QString* error = nullptr);
    QString root() const { return m_root; }
    QString physicalId() const { return m_physicalId; }
   private:
    PhysicalInstanceLease(QString root, QString id, void* handle);
    QString m_root, m_physicalId;
    void* m_handle;
    void* m_rootPin = nullptr;
};
}  // namespace Awake::Integration
