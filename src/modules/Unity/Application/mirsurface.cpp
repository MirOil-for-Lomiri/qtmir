/*
 * Copyright (C) 2015-2016 Canonical, Ltd.
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

#include "mirsurface.h"
#include "mirsurfacelistmodel.h"
#include "timer.h"
#include "timestamp.h"

// from common dir
#include <debughelpers.h>

// mirserver
#include <surfaceobserver.h>
#include "screen.h"

// Mir
#include <mir/geometry/rectangle.h>
#include <mir/events/event_builders.h>
#include <mir/shell/shell.h>
#include <mir/scene/surface.h>
#include <mir/scene/session.h>
#include <mir_toolkit/event.h>

// mirserver
#include <logging.h>

// Qt
#include <QQmlEngine>
#include <QScreen>

#define DEBUG_MSG qCDebug(QTMIR_SURFACES).nospace() << "MirSurface[" << (void*)this << "," << appId() <<"]::" << __func__
#define WARNING_MSG qCWarning(QTMIR_SURFACES).nospace() << "MirSurface[" << (void*)this << "," << appId() <<"]::" << __func__

namespace qtmir {

namespace {

// Would be better if QMouseEvent had nativeModifiers
MirInputEventModifiers
getMirModifiersFromQt(Qt::KeyboardModifiers mods)
{
    MirInputEventModifiers m_mods = mir_input_event_modifier_none;
    if (mods & Qt::ShiftModifier)
        m_mods |= mir_input_event_modifier_shift;
    if (mods & Qt::ControlModifier)
        m_mods |= mir_input_event_modifier_ctrl;
    if (mods & Qt::AltModifier)
        m_mods |= mir_input_event_modifier_alt;
    if (mods & Qt::MetaModifier)
        m_mods |= mir_input_event_modifier_meta;

    return m_mods;
}

MirPointerButtons
getMirButtonsFromQt(Qt::MouseButtons buttons)
{
    MirPointerButtons result = 0;
    if (buttons & Qt::LeftButton)
        result |= mir_pointer_button_primary;
    if (buttons & Qt::RightButton)
        result |= mir_pointer_button_secondary;
    if (buttons & Qt::MiddleButton)
        result |= mir_pointer_button_tertiary;
    if (buttons & Qt::BackButton)
        result |= mir_pointer_button_back;
    if (buttons & Qt::ForwardButton)
        result |= mir_pointer_button_forward;

    return result;
}

mir::EventUPtr makeMirEvent(QMouseEvent *qtEvent, MirPointerAction action)
{
    auto timestamp = uncompressTimestamp<qtmir::Timestamp>(qtmir::Timestamp(qtEvent->timestamp()));
    auto modifiers = getMirModifiersFromQt(qtEvent->modifiers());
    auto buttons = getMirButtonsFromQt(qtEvent->buttons());

    return mir::events::make_event(0 /*DeviceID */, timestamp, std::vector<uint8_t>{} /* cookie */, modifiers, action,
                                   buttons, qtEvent->x(), qtEvent->y(), 0, 0, 0, 0);
}

mir::EventUPtr makeMirEvent(QHoverEvent *qtEvent, MirPointerAction action)
{
    auto timestamp = uncompressTimestamp<qtmir::Timestamp>(qtmir::Timestamp(qtEvent->timestamp()));

    MirPointerButtons buttons = 0;

    return mir::events::make_event(0 /*DeviceID */, timestamp, std::vector<uint8_t>{} /* cookie */, mir_input_event_modifier_none, action,
                                   buttons, qtEvent->posF().x(), qtEvent->posF().y(), 0, 0, 0, 0);
}

mir::EventUPtr makeMirEvent(QWheelEvent *qtEvent)
{
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(qtEvent->timestamp()));
    auto modifiers = getMirModifiersFromQt(qtEvent->modifiers());
    auto buttons = getMirButtonsFromQt(qtEvent->buttons());

    return mir::events::make_event(0 /*DeviceID */, timestamp, std::vector<uint8_t>{} /* cookie */, modifiers, mir_pointer_action_motion,
                                   buttons, qtEvent->x(), qtEvent->y(),
                                   qtEvent->angleDelta().x(), qtEvent->angleDelta().y(),
                                   0, 0);
}

mir::EventUPtr makeMirEvent(QKeyEvent *qtEvent)
{
    MirKeyboardAction action = mir_keyboard_action_down;
    switch (qtEvent->type())
    {
    case QEvent::KeyPress:
        action = mir_keyboard_action_down;
        break;
    case QEvent::KeyRelease:
        action = mir_keyboard_action_up;
        break;
    default:
        break;
    }
    if (qtEvent->isAutoRepeat())
        action = mir_keyboard_action_repeat;

    return mir::events::make_event(0 /* DeviceID */, uncompressTimestamp<qtmir::Timestamp>(qtmir::Timestamp(qtEvent->timestamp())),
                           std::vector<uint8_t>{} /* cookie */, action, qtEvent->nativeVirtualKey(),
                           qtEvent->nativeScanCode(),
                           qtEvent->nativeModifiers());
}

mir::EventUPtr makeMirEvent(Qt::KeyboardModifiers qmods,
                            const QList<QTouchEvent::TouchPoint> &qtTouchPoints,
                            Qt::TouchPointStates /* qtTouchPointStates */,
                            ulong qtTimestamp)
{
    auto modifiers = getMirModifiersFromQt(qmods);
    auto ev = mir::events::make_event(0, uncompressTimestamp<qtmir::Timestamp>(qtmir::Timestamp(qtTimestamp)),
                                      std::vector<uint8_t>{} /* cookie */, modifiers);

    for (int i = 0; i < qtTouchPoints.count(); ++i) {
        auto touchPoint = qtTouchPoints.at(i);
        auto id = touchPoint.id();

        MirTouchAction action = mir_touch_action_change;
        if (touchPoint.state() == Qt::TouchPointReleased)
        {
            action = mir_touch_action_up;
        }
        if (touchPoint.state() == Qt::TouchPointPressed)
        {
            action = mir_touch_action_down;
        }

        MirTouchTooltype tooltype = mir_touch_tooltype_finger;
        if (touchPoint.flags() & QTouchEvent::TouchPoint::Pen)
            tooltype = mir_touch_tooltype_stylus;

        mir::events::add_touch(*ev, id, action, tooltype,
                               touchPoint.pos().x(), touchPoint.pos().y(),
                               touchPoint.pressure(),
                               touchPoint.rect().width(),
                               touchPoint.rect().height(),
                               0 /* size */);
    }

    return ev;
}

} // namespace {

class CompositorTexture
{
public:
    CompositorTexture()
        : m_currentFrameNumber(0)
        , m_textureUpdated(false)
    {
    }

    const QWeakPointer<QSGTexture>& texture() const { return m_texture; }
    void setTexture(const QWeakPointer<QSGTexture>& texture) {
        m_texture = texture;
    }

    int curentFrame() const { return m_currentFrameNumber; }
    void incrementFrame() { m_currentFrameNumber++; }

    bool isUpToDate() const { return m_textureUpdated; }
    void setUpToDate(bool updated) { m_textureUpdated = updated; }

private:
    QWeakPointer<QSGTexture> m_texture;
    int m_currentFrameNumber;
    bool m_textureUpdated;
};


MirSurface::MirSurface(std::shared_ptr<mir::scene::Surface> surface,
        SessionInterface* session,
        mir::shell::Shell* shell,
        std::shared_ptr<SurfaceObserver> observer,
        const CreationHints &creationHints)
    : MirSurfaceInterface()
    , m_surface(surface)
    , m_session(session)
    , m_shell(shell)
    , m_firstFrameDrawn(false)
    , m_orientationAngle(Mir::Angle0)
    , m_live(true)
    , m_shellChrome(Mir::NormalChrome)
{
    DEBUG_MSG << "()";

    m_minimumWidth = creationHints.minWidth;
    m_minimumHeight = creationHints.minHeight;
    m_maximumWidth = creationHints.maxWidth;
    m_maximumHeight = creationHints.maxHeight;
    m_widthIncrement = creationHints.widthIncrement;
    m_heightIncrement = creationHints.heightIncrement;
    m_shellChrome = creationHints.shellChrome;

    m_surfaceObserver = observer;
    if (observer) {
        connect(observer.get(), &SurfaceObserver::framesPosted, this, &MirSurface::onFramesPostedObserved);
        connect(observer.get(), &SurfaceObserver::attributeChanged, this, &MirSurface::onAttributeChanged);
        connect(observer.get(), &SurfaceObserver::nameChanged, this, &MirSurface::nameChanged);
        connect(observer.get(), &SurfaceObserver::cursorChanged, this, &MirSurface::setCursor);
        connect(observer.get(), &SurfaceObserver::minimumWidthChanged, this, &MirSurface::setMinimumWidth);
        connect(observer.get(), &SurfaceObserver::minimumHeightChanged, this, &MirSurface::setMinimumHeight);
        connect(observer.get(), &SurfaceObserver::maximumWidthChanged, this, &MirSurface::setMaximumWidth);
        connect(observer.get(), &SurfaceObserver::maximumHeightChanged, this, &MirSurface::setMaximumHeight);
        connect(observer.get(), &SurfaceObserver::widthIncrementChanged, this, &MirSurface::setWidthIncrement);
        connect(observer.get(), &SurfaceObserver::heightIncrementChanged, this, &MirSurface::setHeightIncrement);
        connect(observer.get(), &SurfaceObserver::shellChromeChanged, this, [&](MirShellChrome shell_chrome) {
            setShellChrome(static_cast<Mir::ShellChrome>(shell_chrome));
        });
        observer->setListener(this);
    }

    connect(session, &QObject::destroyed, this, &MirSurface::onSessionDestroyed);

    connect(&m_frameDropperTimer, &QTimer::timeout,
            this, &MirSurface::dropPendingBuffer);
    // Rationale behind the frame dropper and its interval value:
    //
    // We want to give ample room for Qt scene graph to have a chance to fetch and render
    // the next pending buffer before we take the drastic action of dropping it (so don't set
    // it anywhere close to our target render interval).
    //
    // We also want to guarantee a minimal frames-per-second (fps) frequency for client applications
    // as they get stuck on swap_buffers() if there's no free buffer to swap to yet (ie, they
    // are all pending consumption by the compositor, us). But on the other hand, we don't want
    // that minimal fps to be too high as that would mean this timer would be triggered way too often
    // for nothing causing unnecessary overhead as actually dropping frames from an app should
    // in practice rarely happen.
    m_frameDropperTimer.setInterval(200);
    m_frameDropperTimer.setSingleShot(false);

    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    setCloseTimer(new Timer);
}

MirSurface::~MirSurface()
{
    qCDebug(QTMIR_SURFACES).nospace() << "MirSurface[" << (void*)this << "]::~MirSurface() viewCount=" << m_views.count();

    Q_ASSERT(m_views.isEmpty());

    QMutexLocker locker(&m_mutex);
    m_surface->remove_observer(m_surfaceObserver);

    delete m_closeTimer;

    Q_EMIT destroyed(this); // Early warning, while MirSurface methods can still be accessed.
}

void MirSurface::onFramesPostedObserved()
{
    if (!m_firstFrameDrawn) {
        m_firstFrameDrawn = true;
        Q_EMIT firstFrameDrawn();
    }

    // restart the frame dropper so that items have enough time to render the next frame.
    m_frameDropperTimer.start();

    Q_EMIT framesPosted();
}

void MirSurface::onAttributeChanged(const MirSurfaceAttrib attribute, const int /*value*/)
{
    switch (attribute) {
    case mir_surface_attrib_type:
        Q_EMIT typeChanged(type());
        break;
    case mir_surface_attrib_state:
        Q_EMIT stateChanged(state());
        break;
    case mir_surface_attrib_visibility:
        Q_EMIT visibleChanged(visible());
        break;
    default:
        break;
    }
}

Mir::Type MirSurface::type() const
{
    switch (m_surface->type()) {
    case mir_surface_type_normal:
        return Mir::NormalType;

    case mir_surface_type_utility:
        return Mir::UtilityType;

    case mir_surface_type_dialog:
        return Mir::DialogType;

    case mir_surface_type_gloss:
        return Mir::GlossType;

    case mir_surface_type_freestyle:
        return Mir::FreeStyleType;

    case mir_surface_type_menu:
        return Mir::MenuType;

    case mir_surface_type_inputmethod:
        return Mir::InputMethodType;

    case mir_surface_type_satellite:
        return Mir::SatelliteType;

    case mir_surface_type_tip:
        return Mir::TipType;

    default:
        return Mir::UnknownType;
    }
}

void MirSurface::dropPendingBuffer()
{
    QMutexLocker locker(&m_mutex);

    bool allStop = true;

    auto end = m_textures.constEnd();
    for (auto iter = m_textures.constBegin(); iter != end; ++iter) {
        const qintptr userId = iter.key();
        CompositorTexture* compositorTexture = iter.value();

        int framesPending = m_surface->buffers_ready_for_compositor((void*)userId);
        if (framesPending > 0) {
            compositorTexture->setUpToDate(false);

            if (updateTextureLocked(userId, compositorTexture)) {
                allStop &= false;
                DEBUG_MSG << "() dropped=1 left=" << framesPending-1;
            } else {
                // If we haven't managed to update the texture, don't keep banging away.
                DEBUG_MSG << "() dropped=0" << " left=" << framesPending << " - failed to upate texture";
            }
            Q_EMIT frameDropped();
        } else {
            // The client can't possibly be blocked in swap buffers if the
            // queue is empty. So we can safely enter deep sleep now. If the
            // client provides any new frames, the timer will get restarted
            // via scheduleTextureUpdate()...
        }
    }


    // only stop if all textures are updated
    if (allStop) {
        m_frameDropperTimer.stop();
    }
}

void MirSurface::stopFrameDropper()
{
    DEBUG_MSG << "()";
    m_frameDropperTimer.stop();
}

void MirSurface::startFrameDropper()
{
    DEBUG_MSG << "()";
    if (!m_frameDropperTimer.isActive()) {
        m_frameDropperTimer.start();
    }
}

QSharedPointer<QSGTexture> MirSurface::texture(qintptr userId)
{
    QMutexLocker locker(&m_mutex);

    CompositorTexture* compositorTexture = compositorTextureForId(userId);
    if (!compositorTexture || !compositorTexture->texture()) {
        QSharedPointer<QSGTexture> texture(new MirBufferSGTexture);
        if (!compositorTexture) {
            compositorTexture = new CompositorTexture();
            m_textures[userId] = compositorTexture;
        }
        compositorTexture->setTexture(texture);
        return texture;
    } else {
        return compositorTexture->texture();
    }
}

QSGTexture *MirSurface::weakTexture(qintptr userId) const
{
    QMutexLocker locker(&m_mutex);
    auto compositorTexure = compositorTextureForId(userId);
    return compositorTexure ? compositorTexure->texture().data() : nullptr;
}

bool MirSurface::updateTexture(qintptr userId)
{
    QMutexLocker locker(&m_mutex);

    auto compositorTexure = compositorTextureForId(userId);
    if (!compositorTexure) return false;

    return updateTextureLocked(userId, compositorTexure);
}

bool MirSurface::numBuffersReadyForCompositor(qintptr userId)
{
    QMutexLocker locker(&m_mutex);
    return m_surface->buffers_ready_for_compositor((void*)userId);
}

void MirSurface::onCompositorSwappedBuffers()
{
    QMutexLocker locker(&m_mutex);

    Q_FOREACH(auto windowTexture, m_textures) {
        windowTexture->setUpToDate(false);
    }
}

void MirSurface::setFocused(bool value)
{
    if (m_focused == value)
        return;

    m_focused = value;
    Q_EMIT focusedChanged(value);
}

void MirSurface::setViewActiveFocus(qintptr viewId, bool value)
{
    if (value && !m_activelyFocusedViews.contains(viewId)) {
        m_activelyFocusedViews.insert(viewId);
        updateActiveFocus();
    } else if (!value && (m_activelyFocusedViews.contains(viewId) || m_neverSetSurfaceFocus)) {
        m_activelyFocusedViews.remove(viewId);
        updateActiveFocus();
    }
}

void MirSurface::updateActiveFocus()
{
    if (!m_session) {
        return;
    }

    // Temporary hotfix for http://pad.lv/1483752
    if (m_session->childSessions()->rowCount() > 0) {
        // has child trusted session, ignore any focus change attempts
        DEBUG_MSG << "() has child trusted session, ignore any focus change attempts";
        return;
    }

    if (m_activelyFocusedViews.isEmpty()) {
        DEBUG_MSG << "() unfocused";
        m_shell->set_surface_attribute(m_session->session(), m_surface, mir_surface_attrib_focus, mir_surface_unfocused);
    } else {
        DEBUG_MSG << "() focused";
        m_shell->set_surface_attribute(m_session->session(), m_surface, mir_surface_attrib_focus, mir_surface_focused);
    }

    m_neverSetSurfaceFocus = false;
}

CompositorTexture *MirSurface::compositorTextureForId(qintptr userId) const
{
    return m_textures.value(userId, nullptr);
}

bool MirSurface::updateTextureLocked(qintptr userId, CompositorTexture *compositorTexture)
{    
    auto texture = qWeakPointerCast<MirBufferSGTexture, QSGTexture>(compositorTexture->texture()).lock();
    if (!texture) return false;

    if (compositorTexture->isUpToDate()) {
        return texture->hasBuffer();
    }

    auto renderables = m_surface->generate_renderables((void*)userId);

    if (renderables.size() > 0 &&
            (m_surface->buffers_ready_for_compositor((void*)userId) > 0 || !texture->hasBuffer())
        ) {
        // Avoid holding two buffers for the compositor at the same time. Thus free the current
        // before acquiring the next
        texture->freeBuffer();
        texture->setBuffer(renderables[0]->buffer());
        compositorTexture->incrementFrame();

        if (texture->textureSize() != m_size) {
            m_size = texture->textureSize();
            QMetaObject::invokeMethod(this, "emitSizeChanged", Qt::QueuedConnection);
        }

        compositorTexture->setUpToDate(true);
    }

    if (m_surface->buffers_ready_for_compositor((void*)userId) > 0) {
        // restart the frame dropper to give MirSurfaceItems enough time to render the next frame.
        // queued since the timer lives in a different thread
        QMetaObject::invokeMethod(&m_frameDropperTimer, "start", Qt::QueuedConnection);
    }

    return texture->hasBuffer();
}

void MirSurface::close()
{
    if (m_closingState != NotClosing) {
        return;
    }

    DEBUG_MSG << "()";

    m_closingState = Closing;
    Q_EMIT closeRequested();
    m_closeTimer->start();

    if (m_surface) {
        m_surface->request_client_surface_close();
    }
}

void MirSurface::resize(int width, int height)
{
    int mirWidth = m_surface->size().width.as_int();
    int mirHeight = m_surface->size().height.as_int();

    bool mirSizeIsDifferent = width != mirWidth || height != mirHeight;

    if (clientIsRunning() && mirSizeIsDifferent) {
        mir::geometry::Size newMirSize(width, height);
        m_surface->resize(newMirSize);
        DEBUG_MSG << " old (" << mirWidth << "," << mirHeight << ")"
                  << ", new (" << width << "," << height << ")";
    }
}

QSize MirSurface::size() const
{
    return m_size;
}

Mir::State MirSurface::state() const
{
    switch (m_surface->state()) {
    case mir_surface_state_unknown:
        return Mir::UnknownState;
    case mir_surface_state_restored:
        return Mir::RestoredState;
    case mir_surface_state_minimized:
        return Mir::MinimizedState;
    case mir_surface_state_maximized:
        return Mir::MaximizedState;
    case mir_surface_state_vertmaximized:
        return Mir::VertMaximizedState;
    case mir_surface_state_fullscreen:
        return Mir::FullscreenState;
    case mir_surface_state_horizmaximized:
        return Mir::HorizMaximizedState;
    case mir_surface_state_hidden:
        return Mir::HiddenState;
    default:
        return Mir::UnknownState;
    }
}

Mir::OrientationAngle MirSurface::orientationAngle() const
{
    return m_orientationAngle;
}

void MirSurface::setOrientationAngle(Mir::OrientationAngle angle)
{
    MirOrientation mirOrientation;

    if (angle == m_orientationAngle) {
        return;
    }

    m_orientationAngle = angle;

    switch (angle) {
    case Mir::Angle0:
        mirOrientation = mir_orientation_normal;
        break;
    case Mir::Angle90:
        mirOrientation = mir_orientation_right;
        break;
    case Mir::Angle180:
        mirOrientation = mir_orientation_inverted;
        break;
    case Mir::Angle270:
        mirOrientation = mir_orientation_left;
        break;
    default:
        qCWarning(QTMIR_SURFACES, "Unsupported orientation angle: %d", angle);
        return;
    }

    if (m_surface) {
        m_surface->set_orientation(mirOrientation);
    }

    Q_EMIT orientationAngleChanged(angle);
}

QString MirSurface::name() const
{
    return QString::fromStdString(m_surface->name());
}

void MirSurface::setState(Mir::State qmlState)
{
    int mirState;

    switch (qmlState) {
    default:
    case Mir::UnknownState:
        mirState = mir_surface_state_unknown;
        break;

    case Mir::RestoredState:
        mirState = mir_surface_state_restored;
        break;

    case Mir::MinimizedState:
        mirState = mir_surface_state_minimized;
        break;

    case Mir::MaximizedState:
        mirState = mir_surface_state_maximized;
        break;

    case Mir::VertMaximizedState:
        mirState = mir_surface_state_vertmaximized;
        break;

    case Mir::FullscreenState:
        mirState = mir_surface_state_fullscreen;
        break;

    case Mir::HorizMaximizedState:
        mirState = mir_surface_state_horizmaximized;
        break;

    case Mir::HiddenState:
        mirState = mir_surface_state_hidden;
        break;
    }

    m_shell->set_surface_attribute(m_session->session(), m_surface, mir_surface_attrib_state, mirState);
}

void MirSurface::setLive(bool value)
{
    if (value != m_live) {
        DEBUG_MSG << "(" << value << ")";
        m_live = value;
        Q_EMIT liveChanged(value);
        if (m_views.isEmpty() && !m_live) {
            deleteLater();
        }
    }
}

bool MirSurface::live() const
{
    return m_live;
}

bool MirSurface::visible() const
{
    return m_surface->query(mir_surface_attrib_visibility) == mir_surface_visibility_exposed;
}

void MirSurface::mousePressEvent(QMouseEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_button_down);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::mouseMoveEvent(QMouseEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_motion);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::mouseReleaseEvent(QMouseEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_button_up);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::hoverEnterEvent(QHoverEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_enter);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::hoverLeaveEvent(QHoverEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_leave);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::hoverMoveEvent(QHoverEvent *event)
{
    auto ev = makeMirEvent(event, mir_pointer_action_motion);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::wheelEvent(QWheelEvent *event)
{
    auto ev = makeMirEvent(event);
    m_surface->consume(ev.get());
    event->accept();
}

void MirSurface::keyPressEvent(QKeyEvent *qtEvent)
{
    auto ev = makeMirEvent(qtEvent);
    m_surface->consume(ev.get());
    qtEvent->accept();
}

void MirSurface::keyReleaseEvent(QKeyEvent *qtEvent)
{
    auto ev = makeMirEvent(qtEvent);
    m_surface->consume(ev.get());
    qtEvent->accept();
}

void MirSurface::touchEvent(Qt::KeyboardModifiers mods,
                            const QList<QTouchEvent::TouchPoint> &touchPoints,
                            Qt::TouchPointStates touchPointStates,
                            ulong timestamp)
{
    auto ev = makeMirEvent(mods, touchPoints, touchPointStates, timestamp);
    m_surface->consume(ev.get());
}

bool MirSurface::clientIsRunning() const
{
    return (m_session &&
            (m_session->state() == Session::State::Running
             || m_session->state() == Session::State::Starting
             || m_session->state() == Session::State::Suspending))
        || !m_session;
}

bool MirSurface::isBeingDisplayed() const
{
    return !m_views.isEmpty();
}

void MirSurface::registerView(qintptr viewId)
{
    m_views.insert(viewId, MirSurface::View{false});
    DEBUG_MSG << "(" << viewId << ")" << " after=" << m_views.count();
    if (m_views.count() == 1) {
        Q_EMIT isBeingDisplayedChanged();
    }
}

void MirSurface::unregisterView(qintptr viewId)
{
    m_views.remove(viewId);
    DEBUG_MSG << "(" << viewId << ")" << " after=" << m_views.count() << " live=" << m_live;
    if (m_views.count() == 0) {
        Q_EMIT isBeingDisplayedChanged();
        if (m_session.isNull() || !m_live) {
            deleteLater();
        }
    }
    updateVisibility();
    setViewActiveFocus(viewId, false);
}

void MirSurface::setViewVisibility(qintptr viewId, bool visible)
{
    if (!m_views.contains(viewId)) return;

    m_views[viewId].visible = visible;
    updateVisibility();
}

void MirSurface::updateVisibility()
{
    // FIXME: https://bugs.launchpad.net/ubuntu/+source/unity8/+bug/1514556
    return;

    bool newVisible = false;
    QHashIterator<qintptr, View> i(m_views);
    while (i.hasNext()) {
        i.next();
        newVisible |= i.value().visible;
    }

    if (newVisible != visible()) {
        DEBUG_MSG << "(" << newVisible << ")";

        m_surface->configure(mir_surface_attrib_visibility,
                             newVisible ? mir_surface_visibility_exposed : mir_surface_visibility_occluded);
    }
}

unsigned int MirSurface::currentFrameNumber(qintptr userId) const
{
    QMutexLocker locker(&m_mutex);
    auto compositorTexure = compositorTextureForId(userId);
    return compositorTexure ? compositorTexure->curentFrame() : 0;
}

void MirSurface::onSessionDestroyed()
{
    if (m_views.isEmpty()) {
        deleteLater();
    }
}

void MirSurface::emitSizeChanged()
{
    Q_EMIT sizeChanged(m_size);
}

QString MirSurface::appId() const
{
    QString appId;

    if (m_session && m_session->application()) {
        appId = m_session->application()->appId();
    } else {
        appId.append("-");
    }
    return appId;
}

void MirSurface::setKeymap(const QString &layoutPlusVariant)
{
    if (m_keymap == layoutPlusVariant) {
        return;
    }

    DEBUG_MSG << "(" << layoutPlusVariant << ")";

    m_keymap = layoutPlusVariant;
    Q_EMIT keymapChanged(m_keymap);

    applyKeymap();
}

QString MirSurface::keymap() const
{
    return m_keymap;
}

void MirSurface::applyKeymap()
{
    QStringList stringList = m_keymap.split("+", QString::SkipEmptyParts);

    QString layout = stringList[0];
    QString variant;

    if (stringList.count() > 1) {
        variant = stringList[1];
    }

    if (layout.isEmpty()) {
        WARNING_MSG << "Setting keymap with empty layout is not supported";
        return;
    }

    m_surface->set_keymap(MirInputDeviceId(), "", layout.toStdString(), variant.toStdString(), "");
}

QCursor MirSurface::cursor() const
{
    return m_cursor;
}

Mir::ShellChrome MirSurface::shellChrome() const
{
    return m_shellChrome;
}

void MirSurface::setShellChrome(Mir::ShellChrome shellChrome)
{
    if (m_shellChrome != shellChrome) {
        m_shellChrome = shellChrome;

        Q_EMIT shellChromeChanged(shellChrome);
    }
}

void MirSurface::setScreen(QScreen *screen)
{
    using namespace mir::geometry;
    // in Mir, this means moving the surface in Mir's scene to the matching display
    auto targetScreenTopLeftPx = screen->geometry().topLeft(); // * screen->devicePixelRatio(); GERRY?
    DEBUG_MSG << "moved to" << targetScreenTopLeftPx << "px";
    m_surface->move_to(Point{ X{targetScreenTopLeftPx.x()}, Y{targetScreenTopLeftPx.y()} });
}

void MirSurface::setCursor(const QCursor &cursor)
{
    DEBUG_MSG << "(" << qtCursorShapeToStr(cursor.shape()) << ")";

    m_cursor = cursor;
    Q_EMIT cursorChanged(m_cursor);
}

int MirSurface::minimumWidth() const
{
    return m_minimumWidth;
}

int MirSurface::minimumHeight() const
{
    return m_minimumHeight;
}

int MirSurface::maximumWidth() const
{
    return m_maximumWidth;
}

int MirSurface::maximumHeight() const
{
    return m_maximumHeight;
}

int MirSurface::widthIncrement() const
{
    return m_widthIncrement;
}

int MirSurface::heightIncrement() const
{
    return m_heightIncrement;
}

void MirSurface::setMinimumWidth(int value)
{
    if (value != m_minimumWidth) {
        m_minimumWidth = value;
        Q_EMIT minimumWidthChanged(value);
    }
}

void MirSurface::setMinimumHeight(int value)
{
    if (value != m_minimumHeight) {
        m_minimumHeight = value;
        Q_EMIT minimumHeightChanged(value);
    }
}

void MirSurface::setMaximumWidth(int value)
{
    if (value != m_maximumWidth) {
        m_maximumWidth = value;
        Q_EMIT maximumWidthChanged(value);
    }
}

void MirSurface::setMaximumHeight(int value)
{
    if (value != m_maximumHeight) {
        m_maximumHeight = value;
        Q_EMIT maximumHeightChanged(value);
    }
}

void MirSurface::setWidthIncrement(int value)
{
    if (value != m_widthIncrement) {
        m_widthIncrement = value;
        Q_EMIT widthIncrementChanged(value);
    }
}

void MirSurface::setHeightIncrement(int value)
{
    if (value != m_heightIncrement) {
        m_heightIncrement = value;
        Q_EMIT heightIncrementChanged(value);
    }
}

bool MirSurface::focused() const
{
    return m_focused;
}

void MirSurface::requestFocus()
{
    DEBUG_MSG << "()";
    Q_EMIT focusRequested();
}

void MirSurface::raise()
{
    DEBUG_MSG << "()";
    Q_EMIT raiseRequested();
}

void MirSurface::onCloseTimedOut()
{
    Q_ASSERT(m_closingState == Closing);

    DEBUG_MSG << "()";

    m_closingState = CloseOverdue;

    m_session->session()->destroy_surface(m_surface);
}

void MirSurface::setCloseTimer(AbstractTimer *timer)
{
    bool timerWasRunning = false;

    if (m_closeTimer) {
        timerWasRunning = m_closeTimer->isRunning();
        delete m_closeTimer;
    }

    m_closeTimer = timer;
    m_closeTimer->setInterval(3000);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &AbstractTimer::timeout, this, &MirSurface::onCloseTimedOut);

    if (timerWasRunning) {
        m_closeTimer->start();
    }
}

} // namespace qtmir
