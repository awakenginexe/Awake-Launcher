// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QHash>
#include <QWebEngineUrlSchemeHandler>

namespace Awake::Web {
class Assets final : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit Assets(QObject* parent = nullptr);
    QString putImage(const QByteArray& png);
    void removeImage(const QString& url);
    void requestStarted(QWebEngineUrlRequestJob* request) override;
private:
    QHash<QString, QByteArray> m_images;
};
void initialize();
}  // namespace Awake::Web
