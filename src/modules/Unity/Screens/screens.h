/*
 * Copyright (C) 2016 Canonical, Ltd.
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

#ifndef SCREENS_H
#define SCREENS_H

#include "screentypes.h"

#include <QAbstractListModel>

class QScreen;
class Screen;

namespace qtmir {

class Screens : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariant activeScreen READ activeScreen WRITE activateScreen NOTIFY activeScreenChanged)

public:
    enum ItemRoles {
        ScreenRole = Qt::UserRole + 1,
    };

    explicit Screens(QObject *parent = 0);
    virtual ~Screens() noexcept = default;

    /* QAbstractItemModel */
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    int count() const;
    QVariant activeScreen() const;

public Q_SLOTS:
    void activateScreen(const QVariant& index);

Q_SIGNALS:
    void countChanged();
    void activeScreenChanged();
    void screenAdded(Screen *screen);
    void screenRemoved(Screen *screen);

private Q_SLOTS:
    void onScreenAdded(QScreen *screen);
    void onScreenRemoved(QScreen *screen);

private:
    QList<Screen *> m_screenList;
};

} // namespace qtmir

#endif // SCREENS_H
