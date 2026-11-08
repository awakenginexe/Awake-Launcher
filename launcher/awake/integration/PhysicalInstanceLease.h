// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <QMap>
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
    bool pinDirectory(const QString& relativePath, QString* error = nullptr);
    QString root() const { return m_root; }
    QString physicalId() const { return m_physicalId; }
    QString registryDirectory() const { return m_registry; }
   private:
    PhysicalInstanceLease(QString root, QString id, QString registry, void* handle);
    bool pinAbsoluteDirectory(const QString& path, QString* error);
    QString m_root, m_physicalId, m_registry;
    void* m_handle;
    QMap<QString, void*> m_pins;
};
}  // namespace Awake::Integration
