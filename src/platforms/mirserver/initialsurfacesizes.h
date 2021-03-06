/*
 * Copyright (C) 2017 Canonical, Ltd.
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

#include <QMutex>
#include <QMap>
#include <QSize>

/*
  The size that the first frame of the first top-level surface of an application with the given pid should have.

  Qt GUI thread fills it with data and mir/miral thread queries it
 */
class InitialSurfaceSizes
{
public:
    static void set(pid_t, const QSize &);
    static void remove(pid_t);
    static QSize get(pid_t);
private:
    static QMap<pid_t, QSize> sizeForSession;
    static QMutex mutex;
};
