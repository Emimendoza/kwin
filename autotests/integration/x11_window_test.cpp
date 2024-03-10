/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "atoms.h"
#include "compositor.h"
#include "cursor.h"
#include "effect/effectloader.h"
#include "wayland_server.h"
#include "workspace.h"
#include "x11window.h"

#include <KWayland/Client/surface.h>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

using namespace KWin;
static const QString s_socketName = QStringLiteral("wayland_test_x11_window-0");

class X11WindowTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase_data();
    void initTestCase();
    void init();
    void cleanup();

    void testMaximizedFull();
    void testInitiallyMaximizedFull();
    void testRequestMaximizedFull();
    void testMaximizedVertical();
    void testInitiallyMaximizedVertical();
    void testRequestMaximizedVertical();
    void testMaximizedHorizontal();
    void testInitiallyMaximizedHorizontal();
    void testRequestMaximizedHorizontal();
    void testFullScreen();
    void testInitiallyFullScreen();
    void testRequestFullScreen();
    void testFullscreenLayerWithActiveWaylandWindow();
    void testFullscreenWindowGroups();
    void testKeepBelow();
    void testInitiallyKeepBelow();
    void testKeepAbove();
    void testInitiallyKeepAbove();
    void testMinimized();
    void testInitiallyMinimized();
    void testRequestMinimized();
    void testMinimumSize();
    void testMaximumSize();
    void testTrimCaption_data();
    void testTrimCaption();
    void testFocusInWithWaylandLastActiveWindow();
    void testCaptionChanges();
    void testCaptionWmName();
    void testActivateFocusedWindow();
    void testReentrantMoveResize();
};

void X11WindowTest::initTestCase_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("normal") << 1.0;
    QTest::newRow("scaled2x") << 2.0;
}

void X11WindowTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(waylandServer()->init(s_socketName));
    Test::setOutputConfig({QRect(0, 0, 1280, 1024)});
    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    QVERIFY(KWin::Compositor::self());
}

void X11WindowTest::init()
{
    QVERIFY(Test::setupWaylandConnection());
}

void X11WindowTest::cleanup()
{
    Test::destroyWaylandConnection();
}

static X11Window *createWindow(xcb_connection_t *connection, const QRect &geometry, std::function<void(xcb_window_t)> setup = {})
{
    xcb_window_t windowId = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      geometry.x(),
                      geometry.y(),
                      geometry.width(),
                      geometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);

    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, geometry.x(), geometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, geometry.width(), geometry.height());
    xcb_icccm_set_wm_normal_hints(connection, windowId, &hints);

    if (setup) {
        setup(windowId);
    }

    xcb_map_window(connection, windowId);
    xcb_flush(connection);

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    if (!windowCreatedSpy.wait()) {
        return nullptr;
    }
    return windowCreatedSpy.last().first().value<X11Window *>();
}

void X11WindowTest::testMaximizedFull()
{
    // This test verifies that toggling maximized mode works as expected and state changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Make the window maximized.
    const QRectF originalGeometry = window->frameGeometry();
    const QRectF workArea = workspace()->clientArea(MaximizeArea, window);
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    window->maximize(MaximizeFull);
    QCOMPARE(maximizedChangedSpy.count(), 1);
    QCOMPARE(window->maximizeMode(), MaximizeFull);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeFull);
    QCOMPARE(window->frameGeometry(), workArea);
    QCOMPARE(window->geometryRestore(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY((winInfo.state() & NET::Max) == NET::Max);
    }

    // Restore the window.
    window->maximize(MaximizeRestore);
    QCOMPARE(maximizedChangedSpy.count(), 2);
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
    QCOMPARE(window->frameGeometry(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::Max));
    }
}

void X11WindowTest::testInitiallyMaximizedFull()
{
    // This test verifies that a window can be shown already in the maximized state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::Max, NET::Max);
    });
    QCOMPARE(window->maximizeMode(), MaximizeFull);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeFull);
}

void X11WindowTest::testRequestMaximizedFull()
{
    // This test verifies that the client can toggle the maximized state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::Max, NET::Max);
        xcb_flush(c.get());
    }
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeFull);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeFull);

    // Unset maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::State(), NET::Max);
        xcb_flush(c.get());
    }
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
}

void X11WindowTest::testMaximizedVertical()
{
    // This test verifies that toggling maximized vertically mode works as expected and state changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Make the window maximized.
    const QRectF originalGeometry = window->frameGeometry();
    const QRectF workArea = workspace()->clientArea(MaximizeArea, window);
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    window->maximize(MaximizeVertical);
    QCOMPARE(maximizedChangedSpy.count(), 1);
    QCOMPARE(window->maximizeMode(), MaximizeVertical);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeVertical);
    QCOMPARE(window->frameGeometry(), QRectF(originalGeometry.x(), workArea.y(), originalGeometry.width(), workArea.height()));
    QCOMPARE(window->geometryRestore(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY((winInfo.state() & NET::Max) == NET::MaxVert);
    }

    // Restore the window.
    window->maximize(MaximizeRestore);
    QCOMPARE(maximizedChangedSpy.count(), 2);
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
    QCOMPARE(window->frameGeometry(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::Max));
    }
}

void X11WindowTest::testInitiallyMaximizedVertical()
{
    // This test verifies that a window can be shown already in the maximized vertically state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::MaxVert, NET::MaxVert);
    });
    QCOMPARE(window->maximizeMode(), MaximizeVertical);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeVertical);
}

void X11WindowTest::testRequestMaximizedVertical()
{
    // This test verifies that the client can toggle the maximized vertically state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::MaxVert, NET::MaxVert);
        xcb_flush(c.get());
    }
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeVertical);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeVertical);

    // Unset maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::State(), NET::MaxVert);
        xcb_flush(c.get());
    }
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
}

void X11WindowTest::testMaximizedHorizontal()
{
    // This test verifies that toggling maximized horizontally mode works as expected and state changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Make the window maximized.
    const QRectF originalGeometry = window->frameGeometry();
    const QRectF workArea = workspace()->clientArea(MaximizeArea, window);
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    window->maximize(MaximizeHorizontal);
    QCOMPARE(maximizedChangedSpy.count(), 1);
    QCOMPARE(window->maximizeMode(), MaximizeHorizontal);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeHorizontal);
    QCOMPARE(window->frameGeometry(), QRectF(workArea.x(), originalGeometry.y(), workArea.width(), originalGeometry.height()));
    QCOMPARE(window->geometryRestore(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY((winInfo.state() & NET::Max) == NET::MaxHoriz);
    }

    // Restore the window.
    window->maximize(MaximizeRestore);
    QCOMPARE(maximizedChangedSpy.count(), 2);
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
    QCOMPARE(window->frameGeometry(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::Max));
    }
}

void X11WindowTest::testInitiallyMaximizedHorizontal()
{
    // This test verifies that a window can be shown already in the maximized horizontally state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::MaxHoriz, NET::MaxHoriz);
    });
    QCOMPARE(window->maximizeMode(), MaximizeHorizontal);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeHorizontal);
}

void X11WindowTest::testRequestMaximizedHorizontal()
{
    // This test verifies that the client can toggle the maximized horizontally state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::MaxHoriz, NET::MaxHoriz);
        xcb_flush(c.get());
    }
    QSignalSpy maximizedChangedSpy(window, &Window::maximizedChanged);
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeHorizontal);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeHorizontal);

    // Unset maximized state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::State(), NET::MaxHoriz);
        xcb_flush(c.get());
    }
    QVERIFY(maximizedChangedSpy.wait());
    QCOMPARE(window->maximizeMode(), MaximizeRestore);
    QCOMPARE(window->requestedMaximizeMode(), MaximizeRestore);
}

void X11WindowTest::testFullScreen()
{
    // This test verifies that the fullscreen mode can be toggled and state changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Make the window maximized.
    const QRectF originalGeometry = window->frameGeometry();
    const QRectF screenArea = workspace()->clientArea(ScreenArea, window);
    QSignalSpy fullScreenChangedSpy(window, &Window::fullScreenChanged);
    window->setFullScreen(true);
    QCOMPARE(fullScreenChangedSpy.count(), 1);
    QCOMPARE(window->isFullScreen(), true);
    QCOMPARE(window->isRequestedFullScreen(), true);
    QCOMPARE(window->frameGeometry(), screenArea);
    QCOMPARE(window->fullscreenGeometryRestore(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(winInfo.state() & NET::FullScreen);
    }

    // Restore the window.
    window->setFullScreen(false);
    QCOMPARE(fullScreenChangedSpy.count(), 2);
    QCOMPARE(window->isFullScreen(), false);
    QCOMPARE(window->isRequestedFullScreen(), false);
    QCOMPARE(window->frameGeometry(), originalGeometry);

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::FullScreen));
    }
}

void X11WindowTest::testInitiallyFullScreen()
{
    // This test verifies that a window can be shown already in the fullscreen state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::FullScreen, NET::FullScreen);
    });
    QCOMPARE(window->isFullScreen(), true);
    QCOMPARE(window->isRequestedFullScreen(), true);
}

void X11WindowTest::testRequestFullScreen()
{
    // This test verifies that the client can toggle the fullscreen state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set fullscreen state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::FullScreen, NET::FullScreen);
        xcb_flush(c.get());
    }
    QSignalSpy fullScreenChangedSpy(window, &Window::fullScreenChanged);
    QVERIFY(fullScreenChangedSpy.wait());
    QCOMPARE(window->isFullScreen(), true);
    QCOMPARE(window->isRequestedFullScreen(), true);

    // Unset fullscreen state.
    {
        NETWinInfo info(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::State(), NET::FullScreen);
        xcb_flush(c.get());
    }
    QVERIFY(fullScreenChangedSpy.wait());
    QCOMPARE(window->isFullScreen(), false);
    QCOMPARE(window->isRequestedFullScreen(), false);
}

void X11WindowTest::testKeepBelow()
{
    // This test verifies that keep below state can be toggled and its changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set keep below.
    QSignalSpy keepBelowChangedSpy(window, &Window::keepBelowChanged);
    window->setKeepBelow(true);
    QCOMPARE(keepBelowChangedSpy.count(), 1);
    QVERIFY(window->keepBelow());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(winInfo.state() & NET::KeepBelow);
    }

    // Unset keep below.
    window->setKeepBelow(false);
    QCOMPARE(keepBelowChangedSpy.count(), 2);
    QVERIFY(!window->keepBelow());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::KeepBelow));
    }
}

void X11WindowTest::testInitiallyKeepBelow()
{
    // This test verifies that a window can be shown already with the keep below state set.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::KeepBelow, NET::KeepBelow);
    });
    QVERIFY(window->keepBelow());
}

void X11WindowTest::testKeepAbove()
{
    // This test verifies that keep above state can be toggled and its changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set keep above.
    QSignalSpy keepAboveChangedSpy(window, &Window::keepAboveChanged);
    window->setKeepAbove(true);
    QCOMPARE(keepAboveChangedSpy.count(), 1);
    QVERIFY(window->keepAbove());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(winInfo.state() & NET::KeepAbove);
    }

    // Unset keep above.
    window->setKeepAbove(false);
    QCOMPARE(keepAboveChangedSpy.count(), 2);
    QVERIFY(!window->keepAbove());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::KeepAbove));
    }
}

void X11WindowTest::testInitiallyKeepAbove()
{
    // This test verifies that a window can be shown already with the keep above state set.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        info.setState(NET::KeepAbove, NET::KeepAbove);
    });
    QVERIFY(window->keepAbove());
}

void X11WindowTest::testMinimized()
{
    // This test verifies that a window can be minimized/unminimized and its changes are propagated to the client.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Minimize.
    QSignalSpy minimizedChangedSpy(window, &Window::minimizedChanged);
    window->setMinimized(true);
    QCOMPARE(minimizedChangedSpy.count(), 1);
    QVERIFY(window->isMinimized());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(winInfo.state() & NET::Hidden);
    }

    // Unminimize.
    window->setMinimized(false);
    QCOMPARE(minimizedChangedSpy.count(), 2);
    QVERIFY(!window->isMinimized());

    {
        xcb_flush(kwinApp()->x11Connection());
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(!(winInfo.state() & NET::Hidden));
    }
}

void X11WindowTest::testInitiallyMinimized()
{
    // This test verifies that a window can be shown already in the minimized state.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200), [&c](xcb_window_t windowId) {
        xcb_icccm_wm_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_wm_hints_set_iconic(&hints);
        xcb_icccm_set_wm_hints(c.get(), windowId, &hints);
    });
    QVERIFY(window->isMinimized());

    {
        NETWinInfo winInfo(c.get(), window->window(), kwinApp()->x11RootWindow(), NET::WMState, NET::Properties2());
        QVERIFY(winInfo.state() & NET::Hidden);
    }
}

void X11WindowTest::testRequestMinimized()
{
    // This test verifies that the client can set the minimized state.
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    X11Window *window = createWindow(c.get(), QRect(0, 0, 100, 200));

    // Set minimized state.
    {
        xcb_client_message_event_t event;
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.sequence = 0;
        event.window = window->window();
        event.type = atoms->wm_change_state;
        event.data.data32[0] = XCB_ICCCM_WM_STATE_ICONIC;
        event.data.data32[1] = 0;
        event.data.data32[2] = 0;
        event.data.data32[3] = 0;
        event.data.data32[4] = 0;

        xcb_send_event(c.get(), 0, kwinApp()->x11RootWindow(),
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       reinterpret_cast<const char *>(&event));
        xcb_flush(c.get());
    }
    QSignalSpy minimizedChangedSpy(window, &Window::minimizedChanged);
    QVERIFY(minimizedChangedSpy.wait());
    QVERIFY(window->isMinimized());
}

void X11WindowTest::testMinimumSize()
{
    // This test verifies that the minimum size constraint is correctly applied.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_size_hints_set_min_size(&hints, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window);
    QVERIFY(window->isDecorated());

    QSignalSpy interactiveMoveResizeStartedSpy(window, &Window::interactiveMoveResizeStarted);
    QSignalSpy interactiveMoveResizeSteppedSpy(window, &Window::interactiveMoveResizeStepped);
    QSignalSpy interactiveMoveResizeFinishedSpy(window, &Window::interactiveMoveResizeFinished);
    QSignalSpy frameGeometryChangedSpy(window, &Window::frameGeometryChanged);

    // Begin resize.
    QCOMPARE(workspace()->moveResizeWindow(), nullptr);
    QVERIFY(!window->isInteractiveResize());
    workspace()->slotWindowResize();
    QCOMPARE(workspace()->moveResizeWindow(), window);
    QCOMPARE(interactiveMoveResizeStartedSpy.count(), 1);
    QVERIFY(window->isInteractiveResize());

    const QPointF cursorPos = KWin::Cursors::self()->mouse()->pos();

    window->keyPressEvent(Qt::Key_Left);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(-8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 0);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().width(), 100 / scale);

    window->keyPressEvent(Qt::Key_Right);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos);
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 0);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().width(), 100 / scale);

    window->keyPressEvent(Qt::Key_Right);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(frameGeometryChangedSpy.wait());
    // whilst X11 window size goes through scale, the increment is a logical value kwin side
    QCOMPARE(window->clientSize().width(), 100 / scale + 8);

    window->keyPressEvent(Qt::Key_Up);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(8, -8));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().height(), 200 / scale);

    window->keyPressEvent(Qt::Key_Down);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().height(), 200 / scale);

    window->keyPressEvent(Qt::Key_Down);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(8, 8));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 2);
    QVERIFY(frameGeometryChangedSpy.wait());
    QCOMPARE(window->clientSize().height(), 200 / scale + 8);

    // Finish the resize operation.
    QCOMPARE(interactiveMoveResizeFinishedSpy.count(), 0);
    window->keyPressEvent(Qt::Key_Enter);
    QCOMPARE(interactiveMoveResizeFinishedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeWindow(), nullptr);
    QVERIFY(!window->isInteractiveResize());

    // Destroy the window.
    QSignalSpy windowClosedSpy(window, &X11Window::closed);
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
    c.reset();
}

void X11WindowTest::testMaximumSize()
{
    // This test verifies that the maximum size constraint is correctly applied.
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create an xcb window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_size_hints_set_max_size(&hints, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window);
    QVERIFY(window->isDecorated());

    QSignalSpy interactiveMoveResizeStartedSpy(window, &Window::interactiveMoveResizeStarted);
    QSignalSpy interactiveMoveResizeSteppedSpy(window, &Window::interactiveMoveResizeStepped);
    QSignalSpy interactiveMoveResizeFinishedSpy(window, &Window::interactiveMoveResizeFinished);
    QSignalSpy frameGeometryChangedSpy(window, &Window::frameGeometryChanged);

    // Begin resize.
    QCOMPARE(workspace()->moveResizeWindow(), nullptr);
    QVERIFY(!window->isInteractiveResize());
    workspace()->slotWindowResize();
    QCOMPARE(workspace()->moveResizeWindow(), window);
    QCOMPARE(interactiveMoveResizeStartedSpy.count(), 1);
    QVERIFY(window->isInteractiveResize());

    const QPointF cursorPos = KWin::Cursors::self()->mouse()->pos();

    window->keyPressEvent(Qt::Key_Right);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 0);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().width(), 100 / scale);

    window->keyPressEvent(Qt::Key_Left);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos);
    QVERIFY(!interactiveMoveResizeSteppedSpy.wait(10));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 0);
    QCOMPARE(window->clientSize().width(), 100 / scale);

    window->keyPressEvent(Qt::Key_Left);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(-8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(frameGeometryChangedSpy.wait());
    QCOMPARE(window->clientSize().width(), 100 / scale - 8);

    window->keyPressEvent(Qt::Key_Down);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(-8, 8));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().height(), 200 / scale);

    window->keyPressEvent(Qt::Key_Up);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(-8, 0));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 1);
    QVERIFY(!frameGeometryChangedSpy.wait(10));
    QCOMPARE(window->clientSize().height(), 200 / scale);

    window->keyPressEvent(Qt::Key_Up);
    window->updateInteractiveMoveResize(KWin::Cursors::self()->mouse()->pos());
    QCOMPARE(KWin::Cursors::self()->mouse()->pos(), cursorPos + QPoint(-8, -8));
    QCOMPARE(interactiveMoveResizeSteppedSpy.count(), 2);
    QVERIFY(frameGeometryChangedSpy.wait());
    QCOMPARE(window->clientSize().height(), 200 / scale - 8);

    // Finish the resize operation.
    QCOMPARE(interactiveMoveResizeFinishedSpy.count(), 0);
    window->keyPressEvent(Qt::Key_Enter);
    QCOMPARE(interactiveMoveResizeFinishedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeWindow(), nullptr);
    QVERIFY(!window->isInteractiveResize());

    // Destroy the window.
    QSignalSpy windowClosedSpy(window, &X11Window::closed);
    xcb_unmap_window(c.get(), windowId);
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
    c.reset();
}

void X11WindowTest::testTrimCaption_data()
{
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    QTest::addColumn<QByteArray>("originalTitle");
    QTest::addColumn<QByteArray>("expectedTitle");

    QTest::newRow("simplified")
        << QByteArrayLiteral("Was tun, wenn Schüler Autismus haben?\342\200\250\342\200\250\342\200\250 – Marlies Hübner - Mozilla Firefox")
        << QByteArrayLiteral("Was tun, wenn Schüler Autismus haben? – Marlies Hübner - Mozilla Firefox");

    QTest::newRow("with emojis")
        << QByteArrayLiteral("\bTesting non\302\255printable:\177, emoij:\360\237\230\203, non-characters:\357\277\276")
        << QByteArrayLiteral("Testing nonprintable:, emoij:\360\237\230\203, non-characters:");
}

void X11WindowTest::testTrimCaption()
{
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // this test verifies that caption is properly trimmed

    // create an xcb window
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    NETWinInfo winInfo(c.get(), windowId, rootWindow(), NET::Properties(), NET::Properties2());
    QFETCH(QByteArray, originalTitle);
    winInfo.setName(originalTitle);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    // we should get a window for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->window(), windowId);
    QFETCH(QByteArray, expectedTitle);
    QCOMPARE(window->caption(), QString::fromUtf8(expectedTitle));

    // and destroy the window again
    xcb_unmap_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowClosedSpy(window, &X11Window::closed);
    QVERIFY(windowClosedSpy.wait());
    xcb_destroy_window(c.get(), windowId);
    c.reset();
}

void X11WindowTest::testFullscreenLayerWithActiveWaylandWindow()
{
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // this test verifies that an X11 fullscreen window does not stay in the active layer
    // when a Wayland window is active, see BUG: 375759
    QCOMPARE(workspace()->outputs().count(), 1);

    // first create an X11 window
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    // we should get a window for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->window(), windowId);
    QVERIFY(!window->isFullScreen());
    QVERIFY(window->isActive());
    QCOMPARE(window->layer(), NormalLayer);

    workspace()->slotWindowFullScreen();
    QVERIFY(window->isFullScreen());
    QCOMPARE(window->layer(), ActiveLayer);
    QCOMPARE(workspace()->stackingOrder().last(), window);

    // now let's open a Wayland window
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto waylandWindow = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(waylandWindow);
    QVERIFY(waylandWindow->isActive());
    QCOMPARE(waylandWindow->layer(), NormalLayer);
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(window->layer(), NormalLayer);

    // now activate fullscreen again
    workspace()->activateWindow(window);
    QTRY_VERIFY(window->isActive());
    QCOMPARE(window->layer(), ActiveLayer);
    QCOMPARE(workspace()->stackingOrder().last(), window);
    QCOMPARE(workspace()->stackingOrder().last(), window);

    // activate wayland window again
    workspace()->activateWindow(waylandWindow);
    QTRY_VERIFY(waylandWindow->isActive());
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);

    // back to x window
    workspace()->activateWindow(window);
    QTRY_VERIFY(window->isActive());
    // remove fullscreen
    QVERIFY(window->isFullScreen());
    workspace()->slotWindowFullScreen();
    QVERIFY(!window->isFullScreen());
    // and fullscreen again
    workspace()->slotWindowFullScreen();
    QVERIFY(window->isFullScreen());
    QCOMPARE(workspace()->stackingOrder().last(), window);
    QCOMPARE(workspace()->stackingOrder().last(), window);

    // activate wayland window again
    workspace()->activateWindow(waylandWindow);
    QTRY_VERIFY(waylandWindow->isActive());
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);

    // back to X11 window
    workspace()->activateWindow(window);
    QTRY_VERIFY(window->isActive());
    // remove fullscreen
    QVERIFY(window->isFullScreen());
    workspace()->slotWindowFullScreen();
    QVERIFY(!window->isFullScreen());
    // and fullscreen through X API
    NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info.setState(NET::FullScreen, NET::FullScreen);
    NETRootInfo rootInfo(c.get(), NET::Properties());
    rootInfo.setActiveWindow(windowId, NET::FromApplication, XCB_CURRENT_TIME, XCB_WINDOW_NONE);
    xcb_flush(c.get());
    QTRY_VERIFY(window->isFullScreen());
    QCOMPARE(workspace()->stackingOrder().last(), window);
    QCOMPARE(workspace()->stackingOrder().last(), window);

    // activate wayland window again
    workspace()->activateWindow(waylandWindow);
    QTRY_VERIFY(waylandWindow->isActive());
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(workspace()->stackingOrder().last(), waylandWindow);
    QCOMPARE(window->layer(), NormalLayer);

    // close the window
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::waitForWindowClosed(waylandWindow));
    QTRY_VERIFY(window->isActive());
    QCOMPARE(window->layer(), ActiveLayer);

    // and destroy the window again
    xcb_unmap_window(c.get(), windowId);
    xcb_flush(c.get());
}

void X11WindowTest::testFocusInWithWaylandLastActiveWindow()
{
    // this test verifies that Workspace::allowWindowActivation does not crash if last client was a Wayland client
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // create an X11 window
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    // we should get a window for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->window(), windowId);
    QVERIFY(window->isActive());

    // create Wayland window
    std::unique_ptr<KWayland::Client::Surface> surface(Test::createSurface());
    std::unique_ptr<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.get()));
    auto waylandWindow = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(waylandWindow);
    QVERIFY(waylandWindow->isActive());
    // activate no window
    workspace()->setActiveWindow(nullptr);
    QVERIFY(!waylandWindow->isActive());
    QVERIFY(!workspace()->activeWindow());
    // and close Wayland window again
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::waitForWindowClosed(waylandWindow));

    // and try to activate the x11 window through X11 api
    const auto cookie = xcb_set_input_focus_checked(c.get(), XCB_INPUT_FOCUS_NONE, windowId, XCB_CURRENT_TIME);
    auto error = xcb_request_check(c.get(), cookie);
    QVERIFY(!error);
    // this accesses m_lastActiveWindow on trying to activate
    QTRY_VERIFY(window->isActive());

    // and destroy the window again
    xcb_unmap_window(c.get(), windowId);
    xcb_flush(c.get());
}

void X11WindowTest::testCaptionChanges()
{
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // verifies that caption is updated correctly when the X11 window updates it
    // BUG: 383444
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    NETWinInfo info(c.get(), windowId, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info.setName("foo");
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    // we should get a window for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->window(), windowId);
    QCOMPARE(window->caption(), QStringLiteral("foo"));

    QSignalSpy captionChangedSpy(window, &X11Window::captionChanged);
    info.setName("bar");
    xcb_flush(c.get());
    QVERIFY(captionChangedSpy.wait());
    QCOMPARE(window->caption(), QStringLiteral("bar"));

    // and destroy the window again
    QSignalSpy windowClosedSpy(window, &X11Window::closed);
    xcb_unmap_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
    xcb_destroy_window(c.get(), windowId);
    c.reset();
}

void X11WindowTest::testCaptionWmName()
{
    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // this test verifies that a caption set through WM_NAME is read correctly

    // open glxgears as that one only uses WM_NAME
    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(windowAddedSpy.wait());
    QCOMPARE(windowAddedSpy.count(), 1);
    QCOMPARE(workspace()->windows().count(), 1);
    Window *glxgearsWindow = workspace()->windows().first();
    QCOMPARE(glxgearsWindow->caption(), QStringLiteral("glxgears"));

    glxgears.terminate();
    QVERIFY(glxgears.waitForFinished());
}

void X11WindowTest::testFullscreenWindowGroups()
{
    // this test creates an X11 window and puts it to full screen
    // then a second window is created which is in the same window group
    // BUG: 388310

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_change_property(c.get(), XCB_PROP_MODE_REPLACE, windowId, atoms->wm_client_leader, XCB_ATOM_WINDOW, 32, 1, &windowId);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->window(), windowId);
    QCOMPARE(window->isActive(), true);

    QCOMPARE(window->isFullScreen(), false);
    QCOMPARE(window->layer(), NormalLayer);
    workspace()->slotWindowFullScreen();
    QCOMPARE(window->isFullScreen(), true);
    QCOMPARE(window->layer(), ActiveLayer);

    // now let's create a second window
    windowCreatedSpy.clear();
    xcb_window_t w2 = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, w2, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints2;
    memset(&hints2, 0, sizeof(hints2));
    xcb_icccm_size_hints_set_position(&hints2, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints2, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w2, &hints2);
    xcb_change_property(c.get(), XCB_PROP_MODE_REPLACE, w2, atoms->wm_client_leader, XCB_ATOM_WINDOW, 32, 1, &windowId);
    xcb_map_window(c.get(), w2);
    xcb_flush(c.get());

    QVERIFY(windowCreatedSpy.wait());
    X11Window *window2 = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window2);
    QVERIFY(window != window2);
    QCOMPARE(window2->window(), w2);
    QCOMPARE(window2->isActive(), true);
    QCOMPARE(window2->group(), window->group());
    // first window should be moved back to normal layer
    QCOMPARE(window->isActive(), false);
    QCOMPARE(window->isFullScreen(), true);
    QCOMPARE(window->layer(), NormalLayer);

    // activating the fullscreen window again, should move it to active layer
    workspace()->activateWindow(window);
    QTRY_COMPARE(window->layer(), ActiveLayer);
}

void X11WindowTest::testActivateFocusedWindow()
{
    // The window manager may call XSetInputFocus() on a window that already has focus, in which
    // case no FocusIn event will be generated and the window won't be marked as active. This test
    // verifies that we handle that subtle case properly.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    Test::XcbConnectionPtr connection = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(connection.get()));

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);

    const QRect windowGeometry(0, 0, 100, 200);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());

    // Create the first test window.
    const xcb_window_t windowId1 = xcb_generate_id(connection.get());
    xcb_create_window(connection.get(), XCB_COPY_FROM_PARENT, windowId1, rootWindow(),
                      windowGeometry.x(), windowGeometry.y(),
                      windowGeometry.width(), windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_icccm_set_wm_normal_hints(connection.get(), windowId1, &hints);
    xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, windowId1,
                        atoms->wm_client_leader, XCB_ATOM_WINDOW, 32, 1, &windowId1);
    xcb_map_window(connection.get(), windowId1);
    xcb_flush(connection.get());
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window1 = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window1);
    QCOMPARE(window1->window(), windowId1);
    QCOMPARE(window1->isActive(), true);

    // Create the second test window.
    const xcb_window_t windowId2 = xcb_generate_id(connection.get());
    xcb_create_window(connection.get(), XCB_COPY_FROM_PARENT, windowId2, rootWindow(),
                      windowGeometry.x(), windowGeometry.y(),
                      windowGeometry.width(), windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_icccm_set_wm_normal_hints(connection.get(), windowId2, &hints);
    xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, windowId2,
                        atoms->wm_client_leader, XCB_ATOM_WINDOW, 32, 1, &windowId2);
    xcb_map_window(connection.get(), windowId2);
    xcb_flush(connection.get());
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window2 = windowCreatedSpy.last().first().value<X11Window *>();
    QVERIFY(window2);
    QCOMPARE(window2->window(), windowId2);
    QCOMPARE(window2->isActive(), true);

    // When the second test window is destroyed, the window manager will attempt to activate the
    // next window in the focus chain, which is the first window.
    xcb_set_input_focus(connection.get(), XCB_INPUT_FOCUS_POINTER_ROOT, windowId1, XCB_CURRENT_TIME);
    xcb_destroy_window(connection.get(), windowId2);
    xcb_flush(connection.get());
    QVERIFY(Test::waitForWindowClosed(window2));
    QVERIFY(window1->isActive());

    // Destroy the first test window.
    xcb_destroy_window(connection.get(), windowId1);
    xcb_flush(connection.get());
    QVERIFY(Test::waitForWindowClosed(window1));
}

void X11WindowTest::testReentrantMoveResize()
{
    // This test verifies that calling moveResize() from a slot connected directly
    // to the frameGeometryChanged() signal won't cause an infinite recursion.

    QFETCH_GLOBAL(qreal, scale);
    kwinApp()->setXwaylandScale(scale);

    // Create a test window.
    Test::XcbConnectionPtr c = Test::createX11Connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t windowId = xcb_generate_id(c.get());
    xcb_create_window(c.get(), XCB_COPY_FROM_PARENT, windowId, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), windowId, &hints);
    xcb_change_property(c.get(), XCB_PROP_MODE_REPLACE, windowId, atoms->wm_client_leader, XCB_ATOM_WINDOW, 32, 1, &windowId);
    xcb_map_window(c.get(), windowId);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowCreatedSpy.wait());
    X11Window *window = windowCreatedSpy.first().first().value<X11Window *>();
    QVERIFY(window);
    QCOMPARE(window->pos(), QPoint(0, 0));

    // Let's pretend that there is a script that really wants the window to be at (100, 100).
    connect(window, &Window::frameGeometryChanged, this, [window]() {
        window->moveResize(QRectF(QPointF(100, 100), window->size()));
    });

    // Trigger the lambda above.
    window->move(QPoint(40, 50));

    // Eventually, the window will end up at (100, 100).
    QCOMPARE(window->pos(), QPoint(100, 100));

    // Destroy the test window.
    xcb_destroy_window(c.get(), windowId);
    xcb_flush(c.get());
    QVERIFY(Test::waitForWindowClosed(window));
}

WAYLANDTEST_MAIN(X11WindowTest)
#include "x11_window_test.moc"
