/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2014 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <xcb/xcb.h>

#include <QList>

#include <kwin_export.h>

namespace KWin
{

class KWIN_EXPORT X11EventFilter
{
public:
    /**
     * Creates an event filter for the given event type.
     */
    X11EventFilter(int eventType, int opcode = 0, int genericEventType = 0);
    X11EventFilter(int eventType, int opcode, const QList<int> &genericEventTypes);
    X11EventFilter(const QList<int> &eventTypes);

    /**
     * Destroys the event filter.
     */
    virtual ~X11EventFilter();

    /**
     * Returns the type of events to filter.
     */
    QList<int> eventTypes() const
    {
        return m_eventTypes;
    }

    /**
     * Returns the major opcode of the extension.
     *
     * Only used when the event type is XCB_GE_GENERIC.
     */
    int extension() const
    {
        return m_extension;
    }

    /**
     * Returns the types of generic events to filter.
     *
     * Only used when the event type is XCB_GE_GENERIC.
     */
    QList<int> genericEventTypes() const
    {
        return m_genericEventTypes;
    }

    /**
     * This method is called for every event of the filtered type.
     *
     * Return true to accept the event and stop further processing, and false otherwise.
     */
    virtual bool event(xcb_generic_event_t *event) = 0;

    /**
     * Whether the event filter is for XCB_GE_GENERIC events.
     */
    bool isGenericEvent() const;

private:
    QList<int> m_eventTypes;
    int m_extension;
    QList<int> m_genericEventTypes;
};

} // namespace KWin
