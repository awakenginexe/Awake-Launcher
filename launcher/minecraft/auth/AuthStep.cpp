// SPDX-License-Identifier: GPL-3.0-only
#include "AuthStep.h"
#include "Application.h"

QNetworkAccessManager* AuthStep::network() const
{
    return m_network ? m_network : APPLICATION->network();
}
