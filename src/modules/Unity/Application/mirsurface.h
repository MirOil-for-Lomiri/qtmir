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

#ifndef QTMIR_MIRSURFACE_H
#define QTMIR_MIRSURFACE_H

#include "mirsurfaceinterface.h"
#include "mirsurfacelistmodel.h"

// Qt
#include <QCursor>
#include <QMutex>
#include <QPointer>
#include <QSharedPointer>
#include <QWeakPointer>
#include <QSet>

#include "mirbuffersgtexture.h"
#include "session.h"

// mirserver
#include "creationhints.h"

// mir
#include <mir_toolkit/common.h>

namespace mir {
namespace shell { class Shell; }
namespace scene {class Surface; }
}

class SurfaceObserver;

namespace qtmir {

class AbstractTimer;

class MirSurface : public MirSurfaceInterface
{
    Q_OBJECT

public:
    MirSurface(std::shared_ptr<mir::scene::Surface> surface,
            SessionInterface* session,
            mir::shell::Shell *shell,
            std::shared_ptr<SurfaceObserver> observer,
            const CreationHints &);
    virtual ~MirSurface();

    ////
    // unity::shell::application::MirSurfaceInterface

    Mir::Type type() const override;

    QString name() const override;

    QSize size() const override;
    void resize(int width, int height) override;
    void resize(const QSize &size) override { resize(size.width(), size.height()); }

    Mir::State state() const override;
    void setState(Mir::State qmlState) override;

    bool live() const override;

    bool visible() const override;

    Mir::OrientationAngle orientationAngle() const override;
    void setOrientationAngle(Mir::OrientationAngle angle) override;

    int minimumWidth() const override;
    int minimumHeight() const override;
    int maximumWidth() const override;
    int maximumHeight() const override;
    int widthIncrement() const override;
    int heightIncrement() const override;

    bool focused() const override;

    Q_INVOKABLE void requestFocus() override;
    Q_INVOKABLE void close() override;
    Q_INVOKABLE void raise() override;

    ////
    // qtmir::MirSurfaceInterface

    void setLive(bool value) override;

    bool isFirstFrameDrawn() const override { return m_firstFrameDrawn; }

    void stopFrameDropper() override;
    void startFrameDropper() override;

    bool isBeingDisplayed() const override;

    void registerView(qintptr viewId) override;
    void unregisterView(qintptr viewId) override;
    void setViewVisibility(qintptr viewId, bool visible) override;

    // methods called from the rendering (scene graph) thread:
    QSharedPointer<QSGTexture> texture(qintptr userId) override;
    QSGTexture *weakTexture(qintptr userId) const override;
    bool updateTexture(qintptr userId) override;
    unsigned int currentFrameNumber(qintptr userId) const override;
    bool numBuffersReadyForCompositor(qintptr userId) override;
    // end of methods called from the rendering (scene graph) thread

    void setFocused(bool focus) override;

    void setViewActiveFocus(qintptr viewId, bool value) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void touchEvent(Qt::KeyboardModifiers qmods,
            const QList<QTouchEvent::TouchPoint> &qtTouchPoints,
            Qt::TouchPointStates qtTouchPointStates,
            ulong qtTimestamp) override;

    QString appId() const override;

    QCursor cursor() const override;

    void setKeymap(const QString &) override;
    QString keymap() const override;

    Mir::ShellChrome shellChrome() const override;

    SessionInterface* session() override { return m_session.data(); }

    void setScreen(QScreen *screen) override;

    ////
    // Own API

    // useful for tests
    void setCloseTimer(AbstractTimer *timer);

public Q_SLOTS:
    void onCompositorSwappedBuffers() override;

    void setMinimumWidth(int) override;
    void setMinimumHeight(int) override;
    void setMaximumWidth(int) override;
    void setMaximumHeight(int) override;
    void setWidthIncrement(int) override;
    void setHeightIncrement(int) override;
    void setShellChrome(Mir::ShellChrome shellChrome) override;

private Q_SLOTS:
    void dropPendingBuffer();
    void onAttributeChanged(const MirSurfaceAttrib, const int);
    void onFramesPostedObserved();
    void onSessionDestroyed();
    void emitSizeChanged();
    void setCursor(const QCursor &cursor);
    void onCloseTimedOut();

private:
    void syncSurfaceSizeWithItemSize();
    bool clientIsRunning() const;
    void updateVisibility();
    void applyKeymap();
    void updateActiveFocus();

    std::shared_ptr<mir::scene::Surface> m_surface;
    QPointer<SessionInterface> m_session;
    mir::shell::Shell *const m_shell;
    bool m_firstFrameDrawn;

    //FIXME -  have to save the state as Mir has no getter for it (bug:1357429)
    Mir::OrientationAngle m_orientationAngle;

    QTimer m_frameDropperTimer;

    mutable QMutex m_mutex;

    struct WindowTexture {
        WindowTexture(): textureUpdated(false),currentFrameNumber(0) {}
        // Lives in the rendering (scene graph) thread
        QWeakPointer<QSGTexture> texture;
        bool textureUpdated;
        unsigned int currentFrameNumber;
    };
    QHash<qintptr, WindowTexture*> m_textures;

//    QWeakPointer<QSGTexture> m_texture;
//    bool m_textureUpdated;

    bool m_live;
    struct View {
        bool visible;
    };
    QHash<qintptr, View> m_views;

    QSet<qintptr> m_activelyFocusedViews;
    bool m_neverSetSurfaceFocus{true};

    std::shared_ptr<SurfaceObserver> m_surfaceObserver;

    QSize m_size;
    QString m_keymap;

    QCursor m_cursor;
    Mir::ShellChrome m_shellChrome;

    int m_minimumWidth{0};
    int m_minimumHeight{0};
    int m_maximumWidth{0};
    int m_maximumHeight{0};
    int m_widthIncrement{0};
    int m_heightIncrement{0};

    bool m_focused{false};

    enum ClosingState {
        NotClosing = 0,
        Closing = 1,
        CloseOverdue = 2
    };
    ClosingState m_closingState{NotClosing};
    AbstractTimer *m_closeTimer{nullptr};
};

} // namespace qtmir

#endif // QTMIR_MIRSURFACE_H
