// input: Native Windows acrylic or optional Wayland background-effect protocol.
// output: Native blur with rounded popup regions, shaped backdrops for self-painted
//         windows (no acrylic on Windows), and surface lifecycle cleanup.
// pos: Shared window/menu blur backend; update widget/README.md with behavior changes.
#include "WindowBlurHelper.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>

namespace {

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

DWORD accentColorFromArgb(const QColor &color) {
    return (static_cast<DWORD>(color.alpha()) << 24)
        | (static_cast<DWORD>(color.blue()) << 16)
        | (static_cast<DWORD>(color.green()) << 8)
        | static_cast<DWORD>(color.red());
}

} // anonymous namespace
#endif

void WindowBlurHelper::enableBlurBehind(QWidget *widget, bool dark, bool extendFrame) {
#ifdef Q_OS_WIN
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());

    if (extendFrame) {
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    DWORD preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

    const QColor tint = dark ? QColor(30, 40, 55, 18) : QColor(235, 235, 245, 1);
    DWORD tintVal = accentColorFromArgb(tint);

    ACCENT_POLICY accent{};
    accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    accent.AccentFlags = 2;
    accent.GradientColor = tintVal;

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
#else
    enableNativeBlur(widget);
    Q_UNUSED(dark);
    Q_UNUSED(extendFrame);
#endif
}

void WindowBlurHelper::enableShapedBackdrop(QWidget *widget, bool dark, qreal radius, qreal inset) {
    if (!widget) return;
    widget->setProperty("mpasteBlurRadius", radius);
    widget->setProperty("mpasteBlurInset", inset);
#ifdef Q_OS_WIN
    Q_UNUSED(dark);
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    DWORD preference = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
    COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
#else
    Q_UNUSED(dark);
    enableNativeBlur(widget);
#endif
}

#ifdef MPASTE_WAYLAND_BLUR
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <QWindow>
#include <QPlatformSurfaceEvent>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include "background-effect-client.h"

namespace {
class BlurBackend : public QObject {
public:
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_registry *registry = nullptr;
    ext_background_effect_manager_v1 *manager = nullptr;
    uint32_t capabilities = 0;
    BlurBackend() : QObject(qApp) {
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) return;
        auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!native) return;
        display = native->display();
        compositor = native->compositor();
        if (!display || !compositor) return;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
                auto *self = static_cast<BlurBackend *>(data);
                if (qstrcmp(interface, "ext_background_effect_manager_v1") != 0) return;
                self->manager = static_cast<ext_background_effect_manager_v1 *>(
                    wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
                static const ext_background_effect_manager_v1_listener effects = {
                    [](void *data, ext_background_effect_manager_v1 *, uint32_t flags) {
                        static_cast<BlurBackend *>(data)->capabilities = flags;
                    }
                };
                ext_background_effect_manager_v1_add_listener(self->manager, &effects, self);
            },
            [](void *, wl_registry *, uint32_t) {}
        };
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        if (manager) wl_display_roundtrip(display);
        qInfo() << "[wayland-blur] compositor blur supported:" << supported();
    }
    bool supported() const {
        return manager && (capabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
    }
    ~BlurBackend() override {
        if (manager) ext_background_effect_manager_v1_destroy(manager);
        if (registry) wl_registry_destroy(registry);
    }
};
BlurBackend *blurBackend() {
    static auto *backend = new BlurBackend;
    return backend;
}

class SurfaceBlur : public QObject {
    QWidget *widget_;
    QWindow *window_ = nullptr;
    ext_background_effect_surface_v1 *effect_ = nullptr;
    wl_surface *surface_ = nullptr;
    void clear() {
        if (effect_) ext_background_effect_surface_v1_destroy(effect_);
        effect_ = nullptr;
        surface_ = nullptr;
    }
    void watchWindow() {
        auto *window = widget_->windowHandle();
        if (window == window_) return;
        clear();
        if (window_) {
            window_->removeEventFilter(this);
            disconnect(window_, nullptr, this, nullptr);
        }
        window_ = window;
        if (window_) {
            window_->installEventFilter(this);
            connect(window_, &QObject::destroyed, this, [this] { clear(); window_ = nullptr; });
        }
    }
    void updateRegion() {
        watchWindow();
        if (!window_ || !blurBackend()->supported()) return;
        auto *surface = static_cast<wl_surface *>(QGuiApplication::platformNativeInterface()
            ->nativeResourceForWindow("surface", window_));
        if (!surface) return;
        if (surface != surface_) {
            clear();
            surface_ = surface;
            effect_ = ext_background_effect_manager_v1_get_background_effect(blurBackend()->manager, surface);
        }
        // Protocol coordinates are logical surface coordinates, not backing pixels.
        QPainterPath outline;
        const qreal radius = widget_->property("mpasteBlurRadius").isValid()
            ? widget_->property("mpasteBlurRadius").toReal() : 8.0;
        const qreal inset = widget_->property("mpasteBlurInset").toReal();
        outline.addRoundedRect(QRectF(QPointF(), QSizeF(widget_->size()))
            .adjusted(inset, inset, -inset, -inset), radius, radius);
        const QRegion rounded(outline.toFillPolygon().toPolygon());
        auto *region = wl_compositor_create_region(blurBackend()->compositor);
        for (const QRect &rect : rounded)
            wl_region_add(region, rect.x(), rect.y(), rect.width(), rect.height());
        ext_background_effect_surface_v1_set_blur_region(effect_, region);
        wl_region_destroy(region);
        widget_->update(); // Qt owns wl_surface.commit and buffer lifetimes.
        wl_display_flush(blurBackend()->display);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == window_ && event->type() == QEvent::PlatformSurface) {
            if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) clear();
            else QTimer::singleShot(0, this, [this] { updateRegion(); });
        } else if (object == widget_ && (event->type() == QEvent::Show || event->type() == QEvent::Resize)) {
            updateRegion();
            QTimer::singleShot(0, this, [this] { updateRegion(); });
        }
        return false;
    }
public:
    explicit SurfaceBlur(QWidget *widget) : QObject(widget), widget_(widget) {
        setObjectName(QStringLiteral("mpasteNativeBlur"));
        widget_->installEventFilter(this);
        watchWindow();
        if (widget_->isVisible()) updateRegion();
    }
    ~SurfaceBlur() override { clear(); }
};
} // namespace
#endif

bool WindowBlurHelper::enableNativeBlur(QWidget *widget) {
    if (!widget) return false;
#ifdef MPASTE_WAYLAND_BLUR
    if (!blurBackend()->supported()) return false;
    if (!widget->findChild<QObject *>(QStringLiteral("mpasteNativeBlur"), Qt::FindDirectChildrenOnly))
        new SurfaceBlur(widget);
    return true;
#else
    return false;
#endif
}

bool WindowBlurHelper::hasNativeBlur() {
#ifdef MPASTE_WAYLAND_BLUR
    return blurBackend()->supported();
#else
    return false;
#endif
}

bool WindowBlurHelper::useOpaqueFallback() {
#ifdef Q_OS_WIN
    return false;
#else
    return !hasNativeBlur();
#endif
}
