// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <memory>
class QProcess;
namespace Awake::Integration {
class PhysicalInstanceLease;
class GameProcessGroup final : public std::enable_shared_from_this<GameProcessGroup> {
   public:
    enum class State { Untracked, Inactive, Running, Unknown };
    static State probe(const QString& physicalId, const QString& registryDirectory, QString* error = nullptr);
    static std::shared_ptr<GameProcessGroup> create(const std::shared_ptr<PhysicalInstanceLease>& lease, QString* error = nullptr);
    ~GameProcessGroup();
    bool attach(QProcess& process, QString* error = nullptr);
    static int runKeeper(int argc, char** argv);
   private:
    struct Data;
    explicit GameProcessGroup(std::unique_ptr<Data> data);
    std::unique_ptr<Data> m_data;
};
}  // namespace Awake::Integration
