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

#ifndef QTMIR_CREATIONHINTS_H
#define QTMIR_CREATIONHINTS_H

#include <QMetaType>
#include <QString>

#include <unity/shell/application/Mir.h>

namespace mir {
    namespace scene {
        struct SurfaceCreationParameters;
    }
}

namespace qtmir {

class CreationHints {
public:
    CreationHints() {}
    CreationHints(const mir::scene::SurfaceCreationParameters&);

    QString toString() const;

    int minWidth{0};
    int maxWidth{0};

    int minHeight{0};
    int maxHeight{0};

    int widthIncrement{0};
    int heightIncrement{0};

    Mir::ShellChrome shellChrome{Mir::ShellChrome::NormalChrome};
};

} // namespace qtmir

Q_DECLARE_METATYPE(qtmir::CreationHints)

#endif // QTMIR_CREATIONHINTS_H
