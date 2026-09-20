#include "button_styles.h"
#include "painting.h"

namespace Ui
{
namespace ButtonStyles
{
namespace
{
void feedback(QPainter &p, const QRectF &rect, const CanvasButton &b, qreal radius = 14)
{
    if (!b.isEnabled() || (!b.underMouse() && !b.isDown()))
        return;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(Theme::Control::Feedback, b.isDown() ? 45 : 18));
    p.drawRoundedRect(rect, radius, radius);
    p.restore();
}

void captureRing(QPainter &p, int backgroundAlpha)
{
    p.setPen(QPen(Theme::Overlay::CaptureOutline, 3));
    p.setBrush(withAlpha(Theme::Overlay::Background, backgroundAlpha));
    p.drawEllipse(QRectF(3, 3, 82, 82));
}
} // namespace

CanvasButton::Paint modeCard(Icon type, const QString &title, const QString &description)
{
    return [type, title, description](QPainter &p, const CanvasButton &b) {
        p.save();
        if (!b.isEnabled())
            p.setOpacity(.45);
        const QRectF card(5, 5, 470, 500);
        for (int i = 6; i > 0; --i) {
            p.setPen(QPen(withAlpha(Theme::Home::CardShadow, 5 + i), i));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(-i / 2., 2, i / 2., i / 2. + 2), 26, 26);
        }
        panel(p, card, Theme::Home::Card);
        if (b.isDown()) {
            p.setPen(QPen(Theme::Home::CardPressedOutline, 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card, 25, 25);
            p.setPen(QPen(Qt::white, 2));
            p.drawRoundedRect(card.adjusted(5, 5, -5, -5), 21, 21);
        }
        const auto &accent = type == Icon::Camera ? Theme::Home::CameraAccent
                             : type == Icon::Usb  ? Theme::Home::UvcAccent
                                                  : Theme::Home::OpenCvAccent;
        panel(p, QRectF(155, 80, 170, 156), accent);
        icon(p, type, QRectF(188, 109, 104, 101));
        text(p, QRectF(20, 271, 440, 66), title, 42, Theme::TextDark, true, Qt::AlignCenter);
        text(p, QRectF(20, 352, 440, 82), description, 28, Theme::Home::CardDescription, false,
             Qt::AlignCenter);
        p.restore();
    };
}

CanvasButton::Paint navigationTab(Icon type, const QString &title)
{
    return [type, title](QPainter &p, const CanvasButton &b) {
        if (b.isChecked()) {
            panel(p, QRectF(1, 1, 218, 65), Theme::Navigation::SelectedTab);
            p.setPen(QPen(Theme::Navigation::Underline, 4));
            p.drawLine(QPointF(12, 65), QPointF(208, 65));
        }
        icon(p, type, QRectF(28, 17, 35, 35),
             b.isChecked() ? Theme::Navigation::ActiveIcon : Theme::Navigation::InactiveIcon);
        text(p, QRectF(80, 3, 126, 60), title, 29, b.isChecked() ? Theme::TextPrimary : Theme::TextMuted,
             b.isChecked());
        feedback(p, QRectF(1, 1, 218, 65), b);
    };
}

CanvasButton::Paint back()
{
    return [](QPainter &p, const CanvasButton &b) {
        icon(p, Icon::Back, QRectF(5, 11, 30, 30), Theme::TextMuted);
        text(p, QRectF(43, 0, 160, 54), b.text(), 27, Theme::TextMuted);
        feedback(p, QRectF(0, 0, 206, 54), b);
    };
}

CanvasButton::Paint overlayToggle(Icon type, const QString &label)
{
    return [type, label](QPainter &p, const CanvasButton &b) {
        p.save();
        if (!b.isEnabled())
            p.setOpacity(.45);
        p.setPen(QPen(Theme::Overlay::Outline, 1.4));
        p.setBrush(withAlpha(Theme::Overlay::Background, b.isDown() ? 155 : b.underMouse() ? 130 : 105));
        p.drawRoundedRect(QRectF(1, 1, 262, 62), 30, 30);
        icon(p, type, QRectF(14, 18, 28, 28), Theme::Overlay::Icon);
        text(p, QRectF(53, 0, 128, 64), label, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(b.isChecked() ? Theme::Overlay::SwitchOn : Theme::Overlay::SwitchOff);
        p.drawRoundedRect(QRectF(190, 14, 60, 36), 18, 18);
        p.setBrush(Theme::Overlay::SwitchKnob);
        p.drawEllipse(QPointF(b.isChecked() ? 232 : 208, 32), 14, 14);
        p.restore();
    };
}

CanvasButton::Paint photoCapture()
{
    return [](QPainter &p, const CanvasButton &b) {
        captureRing(p, b.isDown() ? 165 : 105);
        p.setPen(QPen(Theme::Overlay::CaptureInnerRing, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(9, 9, 70, 70));
        icon(p, Icon::Camera, QRectF(23, 21, 42, 42));
        feedback(p, QRectF(3, 3, 82, 82), b, 41);
    };
}

CanvasButton::Paint recordCapture()
{
    return [](QPainter &p, const CanvasButton &b) {
        captureRing(p, 105);
        p.setPen(Qt::NoPen);
        p.setBrush(linearGradient({15, 10}, {75, 78}, Theme::Overlay::Record));
        if (b.isChecked())
            p.drawRoundedRect(QRectF(24, 24, 40, 40), 7, 7);
        else
            p.drawEllipse(QRectF(11, 11, 66, 66));
        feedback(p, QRectF(3, 3, 82, 82), b, 41);
    };
}

CanvasButton::Paint iosRecordingStop()
{
    return [](QPainter &p, const CanvasButton &b) {
        p.save();
        if (!b.isEnabled())
            p.setOpacity(.55);
        p.setPen(QPen(Theme::Overlay::CaptureOutline, 5));
        p.setBrush(withAlpha(Theme::Overlay::Background,
                             b.isDown() ? 175 : 120));
        p.drawEllipse(QRectF(5, 5, 102, 102));
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Overlay::RecordIndicator);
        const QRectF stopRect = b.isDown() ? QRectF(36, 36, 40, 40)
                                           : QRectF(33, 33, 46, 46);
        p.drawRoundedRect(stopRect, 9, 9);
        feedback(p, QRectF(5, 5, 102, 102), b, 51);
        p.restore();
    };
}

CanvasButton::Paint exitApplication()
{
    return [](QPainter &p, const CanvasButton &b) {
        const QRectF bounds(1, 1, 218, 66);
        p.save();
        p.setBrush(QColor(190, 45, 60, b.isDown() ? 220 : 155));
        p.setPen(QPen(QColor(255, 150, 160), 2));
        p.drawRoundedRect(bounds, 14, 14);
        p.setPen(QPen(Theme::TextPrimary, 3));
        p.drawArc(QRectF(17, 19, 29, 29), 45 * 16, 270 * 16);
        p.drawLine(QPointF(31.5, 13), QPointF(31.5, 31));
        text(p, QRectF(56, 0, 158, 68), QStringLiteral("退出程序"), 26,
             Theme::TextPrimary, true, Qt::AlignCenter);
        feedback(p, bounds, b, 14);
        p.restore();
    };
}

CanvasButton::Paint mediaFilter(Icon type, const QString &label)
{
    return [type, label](QPainter &p, const CanvasButton &b) {
        if (b.isChecked())
            panel(p, QRectF(2, 2, 108, 58), Theme::Album::SelectedFilter);
        const auto &foreground = b.isChecked() ? Theme::TextPrimary : Theme::TextMuted;
        icon(p, type, QRectF(12, 20, 22, 22), foreground);
        text(p, QRectF(43, 0, 65, 62), label, 23, foreground, b.isChecked());
        feedback(p, QRectF(2, 2, 108, 58), b, 20);
    };
}

CanvasButton::Paint playback(Icon type, qreal size, bool primary)
{
    return [type, size, primary](QPainter &p, const CanvasButton &b) {
        const QRectF circle(3, 3, size - 6, size - 6);
        p.setBrush(linearGradient(circle.topLeft(), circle.bottomRight(),
                                  primary ? Theme::Playback::Play : Theme::Playback::Step));
        p.setPen(QPen(primary ? Theme::Playback::PlayBorder : Theme::Playback::StepBorder, 2));
        p.drawEllipse(circle);
        icon(p, type, QRectF(size * .29, size * .27, size * .43, size * .46),
             b.isEnabled() ? Theme::TextPrimary : Theme::Playback::DisabledIcon);
        feedback(p, circle, b, size / 2.);
    };
}

CanvasButton::Paint uvcToggle()
{
    return [](QPainter &p, const CanvasButton &b) {
        const QRectF bounds(1, 1, 460, 86);
        panel(p, bounds, b.isChecked() ? Theme::Uvc::StopButton : Theme::Uvc::EnableButton);
        if (b.isChecked()) {
            p.setPen(QPen(Theme::Uvc::StopOutline, 3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(160, 44), 20, 20);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Uvc::StopFill);
            p.drawEllipse(QPointF(160, 44), 12, 12);
        } else {
            icon(p, Icon::Usb, QRectF(143, 25, 34, 38));
        }
        text(p, QRectF(193, 0, 221, 88), b.text(), 30);
        feedback(p, bounds, b);
    };
}
} // namespace ButtonStyles
} // namespace Ui
