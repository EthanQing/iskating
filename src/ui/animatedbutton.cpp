#include "animatedbutton.h"

#include "iconutils.h"

#include <QColor>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QStyleOption>
#include <QVariantAnimation>

#include <algorithm>

namespace {

QColor blend(const QColor &from, const QColor &to, qreal amount)
{
    const qreal t = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QColor colorFromProperty(const QObject *object, const char *name, const QColor &fallback)
{
    const QVariant value = object->property(name);
    if (!value.isValid()) {
        return fallback;
    }
    const QColor color(value.toString());
    return color.isValid() ? color : fallback;
}

} // namespace

AnimatedButton::AnimatedButton(QWidget *parent)
    : QPushButton(parent)
    , m_hoverAnimation(new QVariantAnimation(this))
    , m_pressAnimation(new QVariantAnimation(this))
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFlat(true);
    setAutoDefault(false);
    setTextOpacity(1.0);

    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_pressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_hoverProgress = value.toReal();
        update();
    });
    connect(m_pressAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_pressProgress = value.toReal();
        update();
    });
    connect(this, &QAbstractButton::pressed, this, [this]() { animatePressed(1.0, 80); });
    connect(this, &QAbstractButton::released, this, [this]() { animatePressed(0.0, 110); });
}

void AnimatedButton::setIconSource(const QString &source)
{
    const QString normalized = source.trimmed();
    if (m_iconSource == normalized) {
        return;
    }
    m_iconSource = normalized;
    m_generatedIcon = QIcon();
    m_generatedIconColor = QColor();
    update();
}

qreal AnimatedButton::textOpacity() const
{
    return m_textOpacity;
}

void AnimatedButton::setTextOpacity(qreal opacity)
{
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (qFuzzyCompare(m_textOpacity, normalized)) {
        return;
    }
    m_textOpacity = normalized;
    update();
}

void AnimatedButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const bool enabled = isEnabled();
    const QString variant = property("variant").toString().trimmed().toLower();
    const QString role = property("role").toString().trimmed().toLower();
    const bool navigation = role == QStringLiteral("nav");
    const bool iconOnly = role == QStringLiteral("icon");
    const bool ghostRole = role == QStringLiteral("ghost");
    const bool active = property("active").toBool();

    QColor background;
    QColor hoverBackground;
    QColor pressedBackground;
    QColor border;
    QColor hoverBorder;
    QColor textColor;
    QColor hoverText;
    QColor iconColor;
    QColor hoverIcon;

    if (variant == QStringLiteral("primary")) {
        background = QColor(QStringLiteral("#0EA5E9"));
        hoverBackground = QColor(QStringLiteral("#38BDF8"));
        pressedBackground = QColor(QStringLiteral("#0284C7"));
        border = QColor(QStringLiteral("#0EA5E9"));
        hoverBorder = QColor(QStringLiteral("#38BDF8"));
        textColor = QColor(QStringLiteral("#081018"));
        hoverText = textColor;
        iconColor = textColor;
        hoverIcon = iconColor;
    } else if (variant == QStringLiteral("danger")) {
        background = QColor(QStringLiteral("#542925"));
        hoverBackground = QColor(QStringLiteral("#6E302C"));
        pressedBackground = QColor(QStringLiteral("#45201E"));
        border = QColor(QStringLiteral("#7E3934"));
        hoverBorder = QColor(QStringLiteral("#F97066"));
        textColor = QColor(QStringLiteral("#FFE4E1"));
        hoverText = textColor;
        iconColor = textColor;
        hoverIcon = iconColor;
    } else if (variant == QStringLiteral("ghost") || ghostRole || navigation || iconOnly) {
        background = QColor(QStringLiteral("#00000000"));
        hoverBackground = QColor(QStringLiteral("#192533"));
        pressedBackground = QColor(QStringLiteral("#1D3042"));
        border = QColor(QStringLiteral("#00000000"));
        hoverBorder = QColor(QStringLiteral("#2D4054"));
        textColor = QColor(QStringLiteral("#A2AFBF"));
        hoverText = QColor(QStringLiteral("#F4F7FA"));
        iconColor = textColor;
        hoverIcon = hoverText;
    } else {
        background = QColor(QStringLiteral("#151E29"));
        hoverBackground = QColor(QStringLiteral("#192533"));
        pressedBackground = QColor(QStringLiteral("#1D3042"));
        border = QColor(QStringLiteral("#2D4054"));
        hoverBorder = QColor(QStringLiteral("#38BDF8"));
        textColor = QColor(QStringLiteral("#F4F7FA"));
        hoverText = textColor;
        iconColor = textColor;
        hoverIcon = iconColor;
    }

    if (navigation && active) {
        background = QColor(QStringLiteral("#151E29"));
        hoverBackground = QColor(QStringLiteral("#192533"));
        border = QColor(QStringLiteral("#00000000"));
        hoverBorder = border;
        textColor = QColor(QStringLiteral("#F4F7FA"));
        hoverText = textColor;
        iconColor = QColor(QStringLiteral("#38BDF8"));
        hoverIcon = iconColor;
    }

    if (!enabled) {
        background = QColor(QStringLiteral("#0F141B"));
        hoverBackground = background;
        pressedBackground = background;
        border = QColor(QStringLiteral("#1A222D"));
        hoverBorder = border;
        textColor = QColor(QStringLiteral("#667586"));
        hoverText = textColor;
        iconColor = textColor;
        hoverIcon = iconColor;
    }

    const QColor currentBackground = blend(blend(background, hoverBackground, m_hoverProgress),
                                           pressedBackground,
                                           m_pressProgress);
    const QColor currentBorder = blend(border, hoverBorder, m_hoverProgress);
    const QColor currentText = colorFromProperty(this,
                                                 "textColor",
                                                 blend(textColor, hoverText, m_hoverProgress));
    const QColor currentIcon = colorFromProperty(this,
                                                 "iconColor",
                                                 blend(iconColor, hoverIcon, m_hoverProgress));

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QRectF surfaceRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(currentBorder, 1.0));
    painter.setBrush(currentBackground);
    painter.drawRoundedRect(surfaceRect, navigation ? 7.0 : 6.0, navigation ? 7.0 : 6.0);

    if (navigation && active) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#38BDF8")));
        painter.drawRoundedRect(QRectF(0.0, 6.0, 3.0, std::max(0, height() - 12)), 1.5, 1.5);
    }

    if (hasFocus()) {
        QColor focusColor(QStringLiteral("#38BDF8"));
        focusColor.setAlpha(150);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(focusColor, 1.0));
        painter.drawRoundedRect(surfaceRect.adjusted(1.5, 1.5, -1.5, -1.5), 5.0, 5.0);
    }

    // The collapsed sidebar leaves a 36 px button; keep its icon centered at x = 18
    // throughout the width animation instead of switching alignment when text fades out.
    const int horizontalPadding = navigation ? 8 : 10;
    const int iconSlot = navigation ? 36 : (m_iconSource.isEmpty() && icon().isNull() ? 0 : 24);
    const int contentY = qRound(m_pressProgress);
    const QRect contentRect = rect().adjusted(horizontalPadding,
                                              0,
                                              -horizontalPadding,
                                              0)
                                  .translated(0, contentY);
    int textLeft = contentRect.left();
    if (iconSlot > 0) {
        const QRect iconRect = iconOnly
                                   ? QRect(contentRect.center().x() - 10,
                                           contentRect.top() + (contentRect.height() - 20) / 2,
                                           20,
                                           20)
                                   : QRect(textLeft,
                                           contentRect.top() + (contentRect.height() - 20) / 2,
                                           20,
                                           20);
        QIcon iconToDraw = icon();
        if (!m_iconSource.isEmpty()) {
            if (m_generatedIcon.isNull() || m_generatedIconColor != currentIcon) {
                m_generatedIcon = makeNormalizedTintedSvgIcon(m_iconSource, currentIcon, 20, 18);
                m_generatedIconColor = currentIcon;
            }
            iconToDraw = m_generatedIcon;
        }
        if (!iconToDraw.isNull()) {
            painter.drawPixmap(iconRect, iconToDraw.pixmap(iconRect.size(), QIcon::Normal, QIcon::Off));
        }
        textLeft += iconSlot;
    }

    if (!iconOnly && !text().isEmpty() && m_textOpacity > 0.001) {
        const QRect textRect(textLeft,
                             contentRect.top(),
                             std::max(0, contentRect.right() - textLeft + 1),
                             contentRect.height());
        QFont textFont = font();
        if (navigation) {
            textFont.setWeight(active ? QFont::DemiBold : QFont::Normal);
        }
        painter.setFont(textFont);
        painter.setPen(currentText);
        painter.setOpacity(m_textOpacity);
        const QFontMetrics metrics(textFont);
        const QString label = metrics.elidedText(text(), Qt::ElideRight, textRect.width());
        const Qt::Alignment alignment = navigation || iconSlot > 0
                                            ? Qt::AlignVCenter | Qt::AlignLeft
                                            : Qt::AlignVCenter | Qt::AlignHCenter;
        painter.drawText(textRect, alignment, label);
        painter.setOpacity(1.0);
    }
}

void AnimatedButton::enterEvent(QEnterEvent *event)
{
    QPushButton::enterEvent(event);
    animateHover(1.0, 140);
}

void AnimatedButton::leaveEvent(QEvent *event)
{
    QPushButton::leaveEvent(event);
    animateHover(0.0, 140);
    animatePressed(0.0, 110);
}

void AnimatedButton::changeEvent(QEvent *event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::StyleChange) {
        update();
    }
}

void AnimatedButton::animateHover(qreal target, int duration)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setDuration(duration);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}

void AnimatedButton::animatePressed(qreal target, int duration)
{
    m_pressAnimation->stop();
    m_pressAnimation->setDuration(duration);
    m_pressAnimation->setStartValue(m_pressProgress);
    m_pressAnimation->setEndValue(target);
    m_pressAnimation->start();
}
