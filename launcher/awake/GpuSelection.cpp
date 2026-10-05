// SPDX-License-Identifier: GPL-3.0-only
#include "GpuSelection.h"
#include "Application.h"
#include "settings/SettingsObject.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <vector>
#include <QUrl>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace Awake::Gpu {
bool validMode(const QString& mode) { return QStringList{"automatic", "powerSaving", "highPerformance"}.contains(mode); }
bool needsPrompt(bool seen, int deviceCount) { return !seen && deviceCount > 1; }
QString preferenceValue(const QString& existing, const QString& mode)
{
    auto fields = existing.split(';', Qt::SkipEmptyParts);
    fields.removeIf([](const QString& field) { return field.startsWith("GpuPreference=", Qt::CaseInsensitive); });
    if (mode != "automatic") fields.append(mode == "highPerformance" ? "GpuPreference=2" : "GpuPreference=1");
    return fields.isEmpty() ? QString() : fields.join(';') + ';';
}
QVariantMap hardwareSettings()
{
    QVariantList devices;
    QString powerSavingName, highPerformanceName;
    bool supported = false;
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        supported = true;
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != S_OK) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            devices.append(QVariantMap{{"name", QString::fromWCharArray(desc.Description)}});
            if (highPerformanceName.isEmpty()) highPerformanceName = QString::fromWCharArray(desc.Description);
        }
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(&adapter)) != S_OK) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            powerSavingName = QString::fromWCharArray(desc.Description);
            break;
        }
    }
#endif
    return {{"ok", true}, {"supported", supported}, {"devices", devices},
            {"powerSavingName", powerSavingName}, {"highPerformanceName", highPerformanceName}};
}
QVariantMap settings()
{
    auto result = hardwareSettings();
    result.insert("mode", APPLICATION->settings()->get("AwakeGpuPreference").toString());
    return result;
}
bool confirmBeforeLaunch(QWidget* parent)
{
    if (APPLICATION->settings()->get("AwakeGpuChoiceSeen").toBool()) return true;
    return confirmBeforeLaunch(parent, settings());
}
bool confirmBeforeLaunch(QWidget* parent, const QVariantMap& info)
{
    auto* config = APPLICATION->settings();
    if (config->get("AwakeGpuChoiceSeen").toBool()) return true;
    const auto devices = info.value("devices").toList();
    if (!info.value("supported").toBool() || !needsPrompt(false, devices.size())) return true;
    QDialog dialog(parent);
    dialog.setObjectName("awakeGpuDialog");
    dialog.setWindowTitle(QObject::tr("Choose a GPU for Minecraft"));
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(QObject::tr("More than one GPU was detected. Choose a global preference before Minecraft starts. You can change it later in Settings → Game Window."), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    QStringList names;
    for (const auto& device : devices) names.append(device.toMap().value("name").toString());
    auto* deviceLabel = new QLabel(names.join('\n'), &dialog);
    deviceLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(deviceLabel);
    auto* choice = new QComboBox(&dialog);
    choice->setObjectName("gpuPreferenceChoice");
    choice->addItem(QObject::tr("Let Windows decide"), "automatic");
    choice->addItem(QObject::tr("Power saving") + " — " + info.value("powerSavingName").toString(), "powerSaving");
    choice->addItem(QObject::tr("High performance") + " — " + info.value("highPerformanceName").toString(), "highPerformance");
    choice->setCurrentIndex(qMax(0, choice->findData(config->get("AwakeGpuPreference"))));
    layout->addWidget(choice);
    auto* windowsSettings = new QPushButton(QObject::tr("Open Windows Graphics settings"), &dialog);
    QObject::connect(windowsSettings, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl(QUrl("ms-settings:display-advancedgraphics")); });
    layout->addWidget(windowsSettings);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("Save and continue"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    config->set("AwakeGpuPreference", choice->currentData());
    config->set("AwakeGpuChoiceSeen", true);
    return true;
}
bool applyBeforeJava(const QString& javaPath, QString& error)
{
#ifdef Q_OS_WIN
    if (!APPLICATION->settings()->get("AwakeGpuChoiceSeen").toBool()) return true;
    const auto mode = APPLICATION->settings()->get("AwakeGpuPreference").toString();
    if (!validMode(mode)) { error = QObject::tr("The saved GPU preference is invalid."); return false; }
    const auto executable = QFileInfo(javaPath).canonicalFilePath();
    if (executable.isEmpty()) { error = QObject::tr("Cannot apply the GPU preference: Java was not found."); return false; }
    const auto key = QDir::toNativeSeparators(executable).toStdWString();
    constexpr auto subkey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    DWORD size = 0;
    const auto readStatus = RegGetValueW(HKEY_CURRENT_USER, subkey, key.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &size);
    if (readStatus != ERROR_SUCCESS && readStatus != ERROR_FILE_NOT_FOUND) {
        error = QObject::tr("Windows could not read the GPU preference for Java."); return false;
    }
    QString previous;
    if (readStatus == ERROR_SUCCESS && size > 0) {
        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
        if (RegGetValueW(HKEY_CURRENT_USER, subkey, key.c_str(), RRF_RT_REG_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS) {
            error = QObject::tr("Windows could not read the GPU preference for Java."); return false;
        }
        previous = QString::fromWCharArray(buffer.data());
    }
    const auto updated = preferenceValue(previous, mode);
    if (updated == previous) return true;
    HKEY registry = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &registry, nullptr) != ERROR_SUCCESS) {
        error = QObject::tr("Windows could not save the GPU preference for Java."); return false;
    }
    const auto value = updated.toStdWString();
    const auto status = updated.isEmpty() ? RegDeleteValueW(registry, key.c_str())
        : RegSetValueExW(registry, key.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(registry);
    if (status != ERROR_SUCCESS && !(updated.isEmpty() && status == ERROR_FILE_NOT_FOUND)) {
        error = QObject::tr("Windows could not save the GPU preference for Java."); return false;
    }
#else
    Q_UNUSED(javaPath);
    Q_UNUSED(error);
#endif
    return true;
}
}
