/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#ifndef MIRAL_OPENGLCONTEXTFACTORY_H
#define MIRAL_OPENGLCONTEXTFACTORY_H

#include <memory>

namespace mir { namespace graphics { class Display; }}
namespace mir { class Server; }

class QPlatformOpenGLContext;
class QSurfaceFormat;

namespace qtmir
{
class OpenGLContextFactory
{
public:
    OpenGLContextFactory();

    void operator()(mir::Server& server);

    QPlatformOpenGLContext *createPlatformOpenGLContext(QSurfaceFormat format, mir::graphics::Display &mirDisplay) const;

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIRAL_OPENGLCONTEXTFACTORY_H
