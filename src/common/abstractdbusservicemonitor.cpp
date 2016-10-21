/*
 * Copyright (C) 2011-2015 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "abstractdbusservicemonitor.h"

#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

// On construction QDBusInterface synchronously introspects the service, which will block the GUI
// thread if the service is busy. QDBusAbstractInterface does not perform this introspection, so
// let's subclass that and avoid the blocking scenario.
class AsyncDBusInterface : public QDBusAbstractInterface
{
    Q_OBJECT
public:
    AsyncDBusInterface(const QString &service, const QString &path,
                       const QString &interface, const QDBusConnection &connection,
                       QObject *parent = 0)
    : QDBusAbstractInterface(service, path, interface.toLatin1().data(), connection, parent)
    {}
    ~AsyncDBusInterface() = default;
};

AbstractDBusServiceMonitor::AbstractDBusServiceMonitor(const QString &service, const QString &path,
                                                       const QString &interface, const QDBusConnection &connection,
                                                       QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_path(path)
    , m_interface(interface)
    , m_busConnection(connection)
    , m_watcher(new QDBusServiceWatcher(service, m_busConnection))
    , m_dbusInterface(nullptr)
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &AbstractDBusServiceMonitor::createInterface);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &AbstractDBusServiceMonitor::destroyInterface);

    // Connect to the service if it's up already
    QDBusConnectionInterface* sessionBus = m_busConnection.interface();
    QDBusReply<bool> reply = sessionBus->isServiceRegistered(m_service);
    if (reply.isValid() && reply.value()) {
        createInterface(m_service);
    }
}

AbstractDBusServiceMonitor::~AbstractDBusServiceMonitor()
{
    delete m_watcher;
    delete m_dbusInterface;
}

void AbstractDBusServiceMonitor::createInterface(const QString &)
{
    if (m_dbusInterface != nullptr) {
        delete m_dbusInterface;
        m_dbusInterface = nullptr;
    }

    m_dbusInterface = new AsyncDBusInterface(m_service, m_path, m_interface, m_busConnection);
    Q_EMIT serviceAvailableChanged(true);
}

void AbstractDBusServiceMonitor::destroyInterface(const QString &)
{
    if (m_dbusInterface != nullptr) {
        delete m_dbusInterface;
        m_dbusInterface = nullptr;
    }

    Q_EMIT serviceAvailableChanged(false);
}

QDBusAbstractInterface* AbstractDBusServiceMonitor::dbusInterface() const
{
    return m_dbusInterface;
}

bool AbstractDBusServiceMonitor::serviceAvailable() const
{
    return m_dbusInterface != nullptr;
}

#include "abstractdbusservicemonitor.moc"
