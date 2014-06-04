/*
 * Copyright (C) 2013,2014 Canonical, Ltd.
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
 *
 * Authors:
 *   Daniel d'Andrada <daniel.dandrada@canonical.com>
 *   Gerry Boland <gerry.boland@canonical.com>
 */

#include "screen.h"

#include "mir/geometry/size.h"

// Qt
#include <QCoreApplication>
#include <qpa/qwindowsysteminterface.h>
#include <QtSensors/QOrientationSensor>
#include <QtSensors/QOrientationReading>
#include <QThread>

namespace mg = mir::geometry;

namespace {
bool isLittleEndian() {
    unsigned int i = 1;
    char *c = (char*)&i;
    return *c == 1;
}

enum QImage::Format qImageFormatFromMirPixelFormat(MirPixelFormat mirPixelFormat) {
    switch (mirPixelFormat) {
    case mir_pixel_format_abgr_8888:
        if (isLittleEndian()) {
            // 0xRR,0xGG,0xBB,0xAA
            return QImage::Format_RGBA8888;
        } else {
            // 0xAA,0xBB,0xGG,0xRR
            qFatal("[mirserver QPA] "
                   "Qt doesn't support mir_pixel_format_abgr_8888 in a big endian architecture");
        }
        break;
    case mir_pixel_format_xbgr_8888:
        if (isLittleEndian()) {
            // 0xRR,0xGG,0xBB,0xXX
            return QImage::Format_RGBX8888;
        } else {
            // 0xXX,0xBB,0xGG,0xRR
            qFatal("[mirserver QPA] "
                   "Qt doesn't support mir_pixel_format_xbgr_8888 in a big endian architecture");
        }
        break;
        break;
    case mir_pixel_format_argb_8888:
        // 0xAARRGGBB
        return QImage::Format_ARGB32;
        break;
    case mir_pixel_format_xrgb_8888:
        // 0xffRRGGBB
        return QImage::Format_RGB32;
        break;
    case mir_pixel_format_bgr_888:
        qFatal("[mirserver QPA] Qt doesn't support mir_pixel_format_bgr_888");
        break;
    default:
        qFatal("[mirserver QPA] Unknown mir pixel format");
        break;
    }
}


class OrientationReadingEvent : public QEvent
{
public:
    OrientationReadingEvent(QOrientationReading::Orientation orientation)
        : QEvent(orientationReadingEventType)
        , orientation(orientation) {
    }

    static const QEvent::Type orientationReadingEventType;
    QOrientationReading::Orientation orientation;
};
const QEvent::Type OrientationReadingEvent::orientationReadingEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());

} // namespace {

Screen::Screen(mir::graphics::DisplayConfigurationOutput const &screen)
    : QObject(nullptr)
{
    readMirDisplayConfiguration(screen);

    // Set the default orientation based on the initial screen dimmensions.
    m_nativeOrientation = (m_geometry.width() >= m_geometry.height())
        ? Qt::LandscapeOrientation : Qt::PortraitOrientation;

    m_currentOrientation = m_nativeOrientation;

    m_pendingOrientationTimer.setSingleShot(true);
    m_pendingOrientationTimer.setInterval(1000);
    connect(&m_pendingOrientationTimer, &QTimer::timeout,
            this, &Screen::commitPendingOrientation);

    m_orientationSensor = new QOrientationSensor(this);
    connect(m_orientationSensor, &QOrientationSensor::readingChanged,
                     this, &Screen::onOrientationReadingChanged);

    // TODO: Stop when display is off
    m_orientationSensor->start();
}

void Screen::readMirDisplayConfiguration(mir::graphics::DisplayConfigurationOutput const &screen)
{
    // Physical screen size
    m_physicalSize.setWidth(screen.physical_size_mm.width.as_float());
    m_physicalSize.setHeight(screen.physical_size_mm.height.as_float());

    // Pixel Format
    m_format = qImageFormatFromMirPixelFormat(screen.current_format);

    // Pixel depth
    m_depth = 8 * MIR_BYTES_PER_PIXEL(screen.current_format);

    // Mode = Resolution & refresh rate
    mir::graphics::DisplayConfigurationMode mode = screen.modes.at(screen.current_mode_index);
    m_geometry.setWidth(mode.size.width.as_int());
    m_geometry.setHeight(mode.size.height.as_int());

    m_refreshRate = 60; //FIXME: mode.vrefresh_hz value seems to be incorrect??
}

void Screen::commitPendingOrientation()
{
    m_currentOrientation = m_pendingOrientation;

    // Raise the event signal so that client apps know the orientation changed
    QWindowSystemInterface::handleScreenOrientationChange(screen(), m_currentOrientation);
}

void Screen::customEvent(QEvent* event)
{
    Q_ASSERT(QThread::currentThread() == thread());

    OrientationReadingEvent* oReadingEvent = static_cast<OrientationReadingEvent*>(event);
    switch (oReadingEvent->orientation) {

    case QOrientationReading::TopUp:
        m_pendingOrientation = (m_nativeOrientation == Qt::LandscapeOrientation) ?
            Qt::LandscapeOrientation : Qt::PortraitOrientation;
        m_pendingOrientationTimer.start();
        break;

    case QOrientationReading::TopDown:
        m_pendingOrientation = (m_nativeOrientation == Qt::LandscapeOrientation) ?
            Qt::InvertedLandscapeOrientation : Qt::InvertedPortraitOrientation;
        m_pendingOrientationTimer.start();
        break;

    case QOrientationReading::LeftUp:
        m_pendingOrientation = (m_nativeOrientation == Qt::LandscapeOrientation) ?
            Qt::InvertedPortraitOrientation : Qt::LandscapeOrientation;
        m_pendingOrientationTimer.start();
        break;

    case QOrientationReading::RightUp:
        m_pendingOrientation = (m_nativeOrientation == Qt::LandscapeOrientation) ?
            Qt::PortraitOrientation : Qt::InvertedLandscapeOrientation;
        m_pendingOrientationTimer.start();
        break;

    case QOrientationReading::FaceUp:
    case QOrientationReading::FaceDown:
        // maintain screen orientation
        break;

    default:
        qFatal("[mirserver QPA] Unknown orientation.");
    }

}

void Screen::onOrientationReadingChanged()
{
    // Make sure to switch to the main Qt thread context
    QCoreApplication::postEvent(this,
            new OrientationReadingEvent(m_orientationSensor->reading()->orientation()));
}
