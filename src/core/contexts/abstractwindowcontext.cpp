#include "abstractwindowcontext_p.h"

#include <QtGui/QPen>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

#include "qwkglobal_p.h"

namespace QWK {

    AbstractWindowContext::AbstractWindowContext() = default;

    AbstractWindowContext::~AbstractWindowContext() = default;

    void AbstractWindowContext::setup(QObject *host, WindowItemDelegate *delegate) {
        if (m_host || !host || !delegate) {
            return;
        }
        m_host = host;
        m_delegate.reset(delegate);
        setEnabled(true);
    }

    void AbstractWindowContext::setWindowAttribute(const QString &key, const QVariant &var) {
        auto it = m_windowAttributes.find(key);
        if (it.value() == var)
            return;

        auto newVar = var;
        auto oldVar = it.value();
        void *args[] = {
            &const_cast<QString &>(key),
            &newVar,
            &oldVar,
        };
        virtual_hook(WindowAttributeChangedHook, args);
    }

    bool AbstractWindowContext::setHitTestVisible(const QObject *obj, bool visible) {
        Q_ASSERT(obj);
        if (!obj) {
            return false;
        }

        if (visible) {
            m_hitTestVisibleItems.insert(obj);
        } else {
            m_hitTestVisibleItems.remove(obj);
        }
        return true;
    }

    bool AbstractWindowContext::setSystemButton(WindowAgentBase::SystemButton button,
                                                QObject *obj) {
        Q_ASSERT(button != WindowAgentBase::Unknown);
        if (button == WindowAgentBase::Unknown) {
            return false;
        }

        if (m_systemButtons[button] == obj) {
            return false;
        }
        m_systemButtons[button] = obj;
        return true;
    }

    bool AbstractWindowContext::setTitleBar(QObject *item) {
        Q_ASSERT(item);
        if (m_titleBar == item) {
            return false;
        }
        m_titleBar = item;
        return true;
    }

#ifdef Q_OS_MAC
    void AbstractWindowContext::setSystemButtonArea(const QRect &rect) {
        m_systemButtonArea = rect;
        virtual_hook(SystemButtonAreaChangedHook, nullptr);
    }
#endif

    bool AbstractWindowContext::isInSystemButtons(const QPoint &pos,
                                                  WindowAgentBase::SystemButton *button) const {
        *button = WindowAgentBase::Unknown;
        for (int i = WindowAgentBase::WindowIcon; i <= WindowAgentBase::Close; ++i) {
            auto currentButton = m_systemButtons[i];
            if (!currentButton || !m_delegate->isVisible(currentButton) ||
                !m_delegate->isEnabled(currentButton)) {
                continue;
            }
            if (m_delegate->mapGeometryToScene(currentButton).contains(pos)) {
                *button = static_cast<WindowAgentBase::SystemButton>(i);
                return true;
            }
        }
        return false;
    }

    bool AbstractWindowContext::isInTitleBarDraggableArea(const QPoint &pos) const {
        if (!m_titleBar) {
            // There's no title bar at all, the mouse will always be in the client area.
            return false;
        }
        if (!m_delegate->isVisible(m_titleBar) || !m_delegate->isEnabled(m_titleBar)) {
            // The title bar is hidden or disabled for some reason, treat it as there's
            // no title bar.
            return false;
        }
        QRect windowRect = {QPoint(0, 0), m_windowHandle->size()};
        QRect titleBarRect = m_delegate->mapGeometryToScene(m_titleBar);
        if (!titleBarRect.intersects(windowRect)) {
            // The title bar is totally outside the window for some reason,
            // also treat it as there's no title bar.
            return false;
        }

        if (!titleBarRect.contains(pos)) {
            return false;
        }

        for (int i = WindowAgentBase::WindowIcon; i <= WindowAgentBase::Close; ++i) {
            auto currentButton = m_systemButtons[i];
            if (currentButton && m_delegate->isVisible(currentButton) &&
                m_delegate->isEnabled(currentButton) &&
                m_delegate->mapGeometryToScene(currentButton).contains(pos)) {
                return false;
            }
        }

        for (auto widget : m_hitTestVisibleItems) {
            if (widget && m_delegate->isVisible(widget) && m_delegate->isEnabled(widget) &&
                m_delegate->mapGeometryToScene(widget).contains(pos)) {
                return false;
            }
        }

        return true;
    }

    QString AbstractWindowContext::key() const {
        return {};
    }

    QWK_USED static constexpr const struct {
        const quint32 activeLight = MAKE_RGBA_COLOR(210, 233, 189, 226);
        const quint32 activeDark = MAKE_RGBA_COLOR(177, 205, 190, 240);
        const quint32 inactiveLight = MAKE_RGBA_COLOR(193, 195, 211, 203);
        const quint32 inactiveDark = MAKE_RGBA_COLOR(240, 240, 250, 255);
    } kSampleColorSet;

    void AbstractWindowContext::virtual_hook(int id, void *data) {
        switch (id) {
            case CentralizeHook: {
                QRect screenGeometry = m_windowHandle->screen()->geometry();
                int x = (screenGeometry.width() - m_windowHandle->width()) / 2;
                int y = (screenGeometry.height() - m_windowHandle->height()) / 2;
                QPoint pos(x, y);
                pos += screenGeometry.topLeft();
                m_windowHandle->setPosition(pos);
                return;
            }

            case RaiseWindowHook: {
                Qt::WindowStates state = m_delegate->getWindowState(m_host);
                if (state & Qt::WindowMinimized) {
                    m_delegate->setWindowState(m_host, state & ~Qt::WindowMinimized);
                }
                m_delegate->bringWindowToTop(m_host);
                return;
            }

            case DefaultColorsHook: {
                auto &map = *static_cast<QMap<QString, QColor> *>(data);
                map.clear();
                map.insert(QStringLiteral("activeLight"), kSampleColorSet.activeLight);
                map.insert(QStringLiteral("activeDark"), kSampleColorSet.activeDark);
                map.insert(QStringLiteral("inactiveLight"), kSampleColorSet.inactiveLight);
                map.insert(QStringLiteral("inactiveDark"), kSampleColorSet.inactiveDark);
                return;
            }

            default:
                break;
        }
    }

    void AbstractWindowContext::showSystemMenu(const QPoint &pos) {
        virtual_hook(ShowSystemMenuHook, &const_cast<QPoint &>(pos));
    }

    void AbstractWindowContext::notifyWinIdChange() {
        if (!m_internalEnabled)
            return;

        auto oldWindow = m_windowHandle;
        if (oldWindow == m_windowHandle)
            return;
        auto isDestroyed = oldWindow && m_windowHandleCache.isNull();
        m_windowHandle = m_delegate->window(m_host);
        m_windowHandleCache = m_windowHandle;
        winIdChanged(oldWindow, isDestroyed);
    }

    void AbstractWindowContext::setEnabled(bool enabled) {
        if (enabled == m_internalEnabled)
            return;
        m_internalEnabled = enabled;

        if (enabled) {
            m_windowHandle = m_delegate->window(m_host);
            m_windowHandleCache = m_windowHandle;
            if (m_windowHandle) {
                winIdChanged(nullptr, false);
            }
            return;
        }

        if (!m_windowHandle)
            return;

        auto oldWindow = m_windowHandle;
        m_windowHandle = nullptr;
        m_windowHandleCache.clear();
        winIdChanged(oldWindow, false);
    }

}
