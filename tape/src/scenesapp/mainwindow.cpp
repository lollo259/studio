/*
 * Scenes (Laurence Who Studio)
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "mainwindow.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "midiin.h"

using namespace scenes;

namespace {
const QColor ALU("#CDCED1");
const QColor ALU_LIGHT("#D9DADD");
const QColor KEY("#E3E4E6");
const QColor KEY_DOWN("#D6D7DA");
const QColor KEY_EDGE("#A6A7AA");
const QColor ORANGE("#FF5A1F");
const QColor OLED("#121214");
const QColor INK("#26262A");
const QColor MUTED("#6C6C70");
const QColor OLED_TEXT("#EDEDE8");
const QColor OLED_DIM("#8E8E8A");

const QColor LAYER_COLORS[] = { QColor("#FF5A1F"), QColor("#2A2A2E"), QColor("#8A8B8F"), QColor("#B8400F"), QColor("#55565B"), QColor("#FF9A6E") };

QSettings settings()
{
    return QSettings("LaurenceWho", "Scenes");
}

QString two(int n)
{
    return QString::number(n).rightJustified(2, '0');
}

QString rangeText(const Layer& l)
{
    return l.lo <= 0 && l.hi >= 127 ? QString("all keys") : noteName(l.lo) + "–" + noteName(l.hi);
}

QString layerText(const Layer& l)
{
    QString t = (l.sound.name.isEmpty() ? QString("no sound") : l.sound.name.toLower()) + " · " + rangeText(l);
    if (l.tr) {
        t += QString(" · %1%2").arg(l.tr > 0 ? "+" : "").arg(l.tr);
    }
    return t;
}

QFont monoFont(int px, bool medium = false)
{
    QFont f(Theme::mono());
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(px);
    if (medium) {
        f.setWeight(QFont::Medium);
    }
    return f;
}

QFont titleFont(int px, bool light = true)
{
    QFont f(light ? Theme::titleLight() : Theme::title());
    f.setPixelSize(px);
    if (light) {
        f.setWeight(QFont::Light);
    }
    return f;
}

//! a font size that fits the text in the width (perform mode, display)
QFont fitted(QFont f, const QString& text, int width, int maxPx, int minPx)
{
    for (int px = maxPx; px > minPx; px -= std::max(1, px / 20)) {
        f.setPixelSize(px);
        if (QFontMetrics(f).horizontalAdvance(text) <= width) {
            return f;
        }
    }
    f.setPixelSize(minPx);
    return f;
}

void flatKey(QPainter& p, const QRectF& r, qreal radius, bool down, const QColor& face = KEY)
{
    p.setPen(Qt::NoPen);
    if (!down) {
        p.setBrush(QColor(0, 0, 0, 28));
        p.drawRoundedRect(r.translated(0, 4), radius, radius);
        p.setBrush(KEY_EDGE);
        p.drawRoundedRect(r.translated(0, 2.5), radius, radius);
    }
    p.setBrush(down ? KEY_DOWN : face);
    p.drawRoundedRect(down ? r.translated(0, 1.5) : r, radius, radius);
}

QPushButton* keyButton(const QString& text, QWidget* parent, bool accent = false)
{
    auto* b = new QPushButton(text, parent);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::PointingHandCursor);
    if (accent) {
        b->setProperty("accent", true);
    }
    return b;
}

const char* STYLE = R"(
QWidget#body { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #D9DADD, stop:1 #CDCED1); }
QDialog { background: #CDCED1; }
QLabel { color: #26262A; }
QLabel[muted="true"] { color: #6C6C70; }
QPushButton { background: #E3E4E6; color: #26262A; border: none; border-bottom: 3px solid #A6A7AA; border-radius: 8px; padding: 7px 14px; font-size: 12px; }
QPushButton:hover { background: #EBEBED; }
QPushButton:pressed, QPushButton:checked { background: #D6D7DA; border-bottom: 1px solid #A6A7AA; margin-top: 2px; }
QPushButton[accent="true"] { background: #FF5A1F; color: #FFFFFF; border-bottom-color: #B8400F; }
QPushButton[accent="true"]:pressed { background: #E84F17; }
QPushButton[round="true"] { font-size: 20px; padding: 0; }
QLineEdit, QComboBox, QSpinBox { background: #F1F1EF; color: #26262A; border: none; border-radius: 6px; padding: 6px 8px; min-height: 20px; selection-background-color: #FF5A1F; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #F1F1EF; color: #26262A; selection-background-color: #FF5A1F; selection-color: #FFFFFF; outline: none; }
QSlider::groove:horizontal { height: 4px; background: #A6A7AA; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #FF5A1F; border-radius: 2px; }
QSlider::handle:horizontal { background: #E3E4E6; border: 1px solid #A6A7AA; width: 18px; height: 18px; margin: -8px 0; border-radius: 9px; }
QCheckBox { color: #26262A; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; background: #F1F1EF; border: 1px solid #A6A7AA; }
QCheckBox::indicator:checked { background: #FF5A1F; border-color: #B8400F; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: #A6A7AA; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QMenu { background: #E3E4E6; color: #26262A; border: 1px solid #A6A7AA; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 4px; }
QMenu::item:selected { background: #FF5A1F; color: #FFFFFF; }
QToolTip { background: #121214; color: #EDEDE8; border: none; padding: 4px 6px; }
)";
}

// ======================================================================== theme

QString Theme::mono()
{
    static QString family;
    if (family.isEmpty()) {
        const QStringList families = QFontDatabase::families();
        for (const char* f : { "Consolas", "IBM Plex Mono", "Cascadia Mono", "DejaVu Sans Mono" }) {
            if (families.contains(f)) {
                family = f;
                break;
            }
        }
        if (family.isEmpty()) {
            family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        }
    }
    return family;
}

QString Theme::title()
{
    return QFontDatabase::families().contains("Jost") ? QString("Jost") : QApplication::font().family();
}

QString Theme::titleLight()
{
    return QFontDatabase::families().contains("Jost*") ? QString("Jost*") : title();
}

// ======================================================================== display

OledDisplay::OledDisplay(MainWindow* w)
    : QWidget(w), m_w(w)
{
    setMinimumHeight(210);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void OledDisplay::flash()
{
    m_flashAt = QDateTime::currentMSecsSinceEpoch();
    update();
}

void OledDisplay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    // bezel, then the black glass
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 140));
    p.drawRoundedRect(r.translated(0, 1), 16, 16);
    p.setBrush(OLED);
    p.drawRoundedRect(r, 16, 16);
    QLinearGradient shade(r.topLeft(), r.bottomLeft());
    shade.setColorAt(0, QColor(0, 0, 0, 120));
    shade.setColorAt(0.12, QColor(0, 0, 0, 0));
    p.setBrush(shade);
    p.drawRoundedRect(r, 16, 16);

    const SceneSet& set = m_w->set();
    const QRectF in = r.adjusted(24, 18, -24, -16);
    p.setFont(monoFont(12));
    p.setPen(OLED_DIM);
    if (set.scenes.empty()) {
        p.drawText(in, Qt::AlignLeft | Qt::AlignTop, "no scenes · press + scene");
        return;
    }
    const int cur = m_w->current();
    const Scene& sc = set.scenes[size_t(cur)];

    // top line: position, latency, level
    p.drawText(in, Qt::AlignLeft | Qt::AlignTop, two(cur + 1) + " / " + two(int(set.scenes.size())));
    AudioEngine& a = m_w->audio();
    const QString lat = a.isRunning() ? QString("%1 ms").arg(a.outputLatencyMs(), 0, 'f', 1) : QString("no sound");
    const int meterW = 60;
    p.drawText(in.adjusted(0, 0, -meterW - 12, 0), Qt::AlignRight | Qt::AlignTop, lat);
    const QRectF meter(in.right() - meterW, in.top() + 5, meterW, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawRoundedRect(meter, 2, 2);
    static float shown = 0.0f;
    const float peak = a.takePeak();
    shown = std::max(peak, shown * 0.85f);
    if (shown > 0.001f) {
        const float db = 20.0f * std::log10(shown);
        const qreal f = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        p.setBrush(shown > 0.95f ? ORANGE : OLED_TEXT);
        p.drawRoundedRect(QRectF(meter.left(), meter.top(), meter.width() * f, meter.height()), 2, 2);
    }

    // the scene, big; fades in after a change
    const qint64 since = QDateTime::currentMSecsSinceEpoch() - m_flashAt;
    const qreal fade = since < 250 ? 0.25 + 0.75 * qreal(since) / 250.0 : 1.0;
    p.setOpacity(fade);
    const QFont big = fitted(titleFont(int(std::min(64.0, in.height() * 0.3))), sc.name, int(in.width()), int(std::min(64.0, in.height() * 0.3)), 22);
    p.setFont(big);
    p.setPen(OLED_TEXT);
    const qreal nameTop = in.top() + 26;
    const QFontMetricsF bm(big);
    p.drawText(QRectF(in.left(), nameTop, in.width(), bm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(big).elidedText(sc.name, Qt::ElideRight, int(in.width())));

    // its layers
    p.setFont(monoFont(12));
    qreal y = nameTop + bm.height() + 8;
    const qreal lineH = 18;
    const int maxLines = std::max(1, int((in.bottom() - 22 - y) / lineH));
    for (size_t i = 0; i < sc.layers.size() && int(i) < maxLines; ++i) {
        const Layer& l = sc.layers[i];
        p.setPen(LAYER_COLORS[i % 6] == QColor("#2A2A2E") ? QColor("#B9B9B5") : LAYER_COLORS[i % 6]);
        p.drawText(QPointF(in.left(), y + 12), "—");
        p.setPen(l.sound.path.isEmpty() && !l.sound.isBuiltin() ? ORANGE : QColor("#CFCFCA"));
        const QString t = layerText(l) + (l.sound.path.isEmpty() && !l.sound.isBuiltin() ? "  (missing)" : "");
        p.drawText(QPointF(in.left() + 22, y + 12), QFontMetrics(p.font()).elidedText(t, Qt::ElideRight, int(in.width() - 22)));
        y += lineH;
    }
    p.setOpacity(1.0);

    // bottom: next scene (or loading)
    p.setFont(monoFont(12));
    QString bottom;
    if (!m_w->isSceneReady(cur)) {
        p.setPen(ORANGE);
        bottom = "loading sounds…";
    } else {
        p.setPen(OLED_DIM);
        bottom = cur + 1 < int(set.scenes.size()) ? "next › " + set.scenes[size_t(cur + 1)].name.toLower() : QString("last scene");
        if (a.loadingCount() > 0) {
            bottom += QString("   · loading %1").arg(a.loadingCount());
        }
    }
    p.drawText(in, Qt::AlignLeft | Qt::AlignBottom, bottom);
}

// ======================================================================== keyboard strip

KeyStrip::KeyStrip(MainWindow* w)
    : QWidget(w), m_w(w)
{
    setFixedHeight(96);
}

void KeyStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int LO = 21, HI = 108; // a piano: A0..C8
    auto isBlack = [](int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; };
    int whites = 0;
    for (int n = LO; n <= HI; ++n) {
        whites += isBlack(n) ? 0 : 1;
    }
    const SceneSet& set = m_w->set();
    const Scene* sc = set.scenes.empty() ? nullptr : &set.scenes[size_t(m_w->current())];
    const int nLayers = sc ? int(sc->layers.size()) : 0;
    const qreal bandH = 7, bandGap = 3;
    const qreal bandsH = std::min(4, std::max(1, nLayers)) * (bandH + bandGap);
    const QRectF keys(0, bandsH + 2, width(), height() - bandsH - 2);
    const qreal ww = keys.width() / whites;

    std::vector<qreal> x(128, 0.0);
    int wi = 0;
    for (int n = LO; n <= HI; ++n) {
        if (isBlack(n)) {
            x[size_t(n)] = keys.left() + wi * ww - ww * 0.3;
        } else {
            x[size_t(n)] = keys.left() + wi * ww;
            ++wi;
        }
    }
    auto leftOf = [&](int n) { n = std::clamp(n, LO, HI); return x[size_t(n)]; };
    auto rightOf = [&](int n) { n = std::clamp(n, LO, HI); return x[size_t(n)] + (isBlack(n) ? ww * 0.6 : ww); };

    // layer bands
    for (int i = 0; i < nLayers; ++i) {
        const Layer& l = sc->layers[size_t(i)];
        const int row = i % 4;
        const QRectF band(leftOf(l.lo) + 1, row * (bandH + bandGap), rightOf(l.hi) - leftOf(l.lo) - 2, bandH);
        p.setPen(Qt::NoPen);
        p.setBrush(LAYER_COLORS[i % 6]);
        p.drawRoundedRect(band, 3, 3);
    }

    // keys: flat and light; held keys light up orange
    p.setPen(Qt::NoPen);
    for (int n = LO; n <= HI; ++n) {
        if (isBlack(n)) {
            continue;
        }
        const QRectF k(x[size_t(n)] + 0.5, keys.top(), ww - 1, keys.height());
        p.setBrush(m_w->audio().isKeyDown(n) ? ORANGE : KEY);
        p.drawRoundedRect(k, 2, 2);
        if (n % 12 == 0) {
            p.setPen(MUTED);
            p.setFont(monoFont(9));
            p.drawText(k.adjusted(0, 0, 0, -3), Qt::AlignHCenter | Qt::AlignBottom, noteName(n).toLower());
            p.setPen(Qt::NoPen);
        }
    }
    for (int n = LO; n <= HI; ++n) {
        if (!isBlack(n)) {
            continue;
        }
        const QRectF k(x[size_t(n)], keys.top(), ww * 0.6, keys.height() * 0.6);
        p.setBrush(m_w->audio().isKeyDown(n) ? ORANGE : QColor("#3A3A3E"));
        p.drawRoundedRect(k, 2, 2);
    }
}

// ======================================================================== scene key

SceneKey::SceneKey(MainWindow* w, int index)
    : QWidget(w), m_w(w), m_index(index)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(150, 84);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(92);
}

void SceneKey::paintEvent(QPaintEvent*)
{
    const SceneSet& set = m_w->set();
    if (m_index >= int(set.scenes.size())) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool cur = m_index == m_w->current();
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -6);
    flatKey(p, r, 10, m_down || cur, cur ? QColor("#F2F2F0") : KEY);
    const QRectF in = (m_down || cur ? r.translated(0, 1.5) : r).adjusted(12, 10, -12, -10);

    // LED: orange on the scene you are on, an outline on the next one
    const QPointF led(in.right() - 4, in.top() + 5);
    if (cur) {
        QRadialGradient glow(led, 10);
        glow.setColorAt(0, QColor(255, 90, 31, 160));
        glow.setColorAt(1, QColor(255, 90, 31, 0));
        p.setBrush(glow);
        p.setPen(Qt::NoPen);
        p.drawEllipse(led, 10, 10);
        p.setBrush(ORANGE);
        p.drawEllipse(led, 4, 4);
    } else {
        const bool isNext = m_index == m_w->current() + 1;
        p.setBrush(isNext ? QBrush(Qt::NoBrush) : QBrush(QColor("#BDBEC1")));
        p.setPen(isNext ? QPen(ORANGE, 1.4) : QPen(Qt::NoPen));
        p.drawEllipse(led, 3.5, 3.5);
    }

    const Scene& sc = set.scenes[size_t(m_index)];
    p.setPen(cur ? ORANGE : MUTED);
    p.setFont(monoFont(11));
    QString top = two(m_index + 1);
    if (!m_w->isSceneReady(m_index)) {
        top += "  ·  loading";
    }
    p.drawText(in, Qt::AlignLeft | Qt::AlignTop, top);
    p.setPen(cur ? ORANGE : INK);
    QFont f = titleFont(19, false);
    p.setFont(f);
    p.drawText(in, Qt::AlignLeft | Qt::AlignBottom, QFontMetrics(f).elidedText(sc.name, Qt::ElideRight, int(in.width())));
    p.setPen(MUTED);
    p.setFont(monoFont(10));
    QString sounds;
    for (const Layer& l : sc.layers) {
        sounds += (sounds.isEmpty() ? "" : " + ") + l.sound.name.toLower();
    }
    p.drawText(in.adjusted(0, 16, 0, -26), Qt::AlignLeft | Qt::AlignTop, QFontMetrics(p.font()).elidedText(sounds, Qt::ElideRight, int(in.width())));
}

void SceneKey::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_down = true;
        m_w->go(m_index); // on press, not release: the fastest switch
        update();
    }
}

void SceneKey::mouseReleaseEvent(QMouseEvent*)
{
    m_down = false;
    update();
}

void SceneKey::mouseDoubleClickEvent(QMouseEvent*)
{
    m_w->editScene(m_index);
}

void SceneKey::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu m(this);
    m.addAction("edit", [this] { m_w->editScene(m_index); });
    m.addAction("duplicate", [this] { m_w->duplicateScene(m_index); });
    m.addAction("move earlier", [this] { m_w->moveScene(m_index, -1); });
    m.addAction("move later", [this] { m_w->moveScene(m_index, +1); });
    m.addSeparator();
    m.addAction("delete", [this] { m_w->deleteScene(m_index); });
    m.exec(e->globalPos());
}

// ======================================================================== perform

PerformView::PerformView(MainWindow* w)
    : QWidget(nullptr), m_w(w)
{
    setWindowTitle("Scenes");
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::BlankCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void PerformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), OLED);
    const SceneSet& set = m_w->set();
    const QRectF in = QRectF(rect()).adjusted(width() * 0.06, height() * 0.07, -width() * 0.06, -height() * 0.07);
    p.setFont(monoFont(std::max(14, height() / 40)));
    p.setPen(OLED_DIM);
    if (set.scenes.empty()) {
        p.drawText(in, Qt::AlignCenter, "no scenes");
        return;
    }
    const int cur = m_w->current();
    p.drawText(in, Qt::AlignLeft | Qt::AlignTop, two(cur + 1) + " / " + two(int(set.scenes.size())));
    AudioEngine& a = m_w->audio();
    p.drawText(in, Qt::AlignRight | Qt::AlignTop, a.isRunning() ? QString("%1 ms").arg(a.outputLatencyMs(), 0, 'f', 1) : QString("no sound"));

    const QString name = set.scenes[size_t(cur)].name;
    const QFont big = fitted(titleFont(int(height() * 0.26)), name, int(in.width()), int(height() * 0.26), 40);
    p.setFont(big);
    p.setPen(OLED_TEXT);
    const QRectF nameBox(in.left(), in.top() + height() * 0.12, in.width(), height() * 0.42);
    p.drawText(nameBox, Qt::AlignLeft | Qt::AlignVCenter, name);
    if (!m_w->isSceneReady(cur)) {
        p.setFont(monoFont(std::max(14, height() / 36)));
        p.setPen(ORANGE);
        p.drawText(QRectF(in.left(), nameBox.bottom(), in.width(), height() * 0.06), Qt::AlignLeft | Qt::AlignTop, "loading sounds…");
    }

    // next, smaller, with the orange mark
    const QString next = cur + 1 < int(set.scenes.size()) ? set.scenes[size_t(cur + 1)].name : QString();
    const qreal ny = in.bottom() - height() * 0.16;
    p.setPen(Qt::NoPen);
    p.setBrush(ORANGE);
    p.drawEllipse(QPointF(in.left() + 8, ny + height() * 0.095), 7, 7);
    p.setPen(OLED_DIM);
    p.setFont(monoFont(std::max(14, height() / 40)));
    p.drawText(QRectF(in.left() + 28, ny, in.width(), height() * 0.04), Qt::AlignLeft | Qt::AlignTop, next.isEmpty() ? "last scene" : "next");
    if (!next.isEmpty()) {
        p.setPen(QColor("#B9B9B5"));
        p.setFont(fitted(titleFont(int(height() * 0.08)), next, int(in.width() - 28), int(height() * 0.08), 18));
        p.drawText(QRectF(in.left() + 28, ny + height() * 0.035, in.width() - 28, height() * 0.12), Qt::AlignLeft | Qt::AlignVCenter, next);
    }
}

void PerformView::mousePressEvent(QMouseEvent* e)
{
    // left third: back, the rest: forward
    if (e->position().x() < width() / 3.0) {
        m_w->prev();
    } else {
        m_w->next();
    }
}

void PerformView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_F11) {
        m_w->togglePerform();
        return;
    }
    QApplication::sendEvent(m_w, e);
}

// ======================================================================== editor

EditDialog::EditDialog(MainWindow* w, int index)
    : QDialog(w), m_w(w), m_index(index), m_scene(w->set().scenes[size_t(index)])
{
    setWindowTitle("Scene");
    setMinimumSize(640, 520);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(22, 18, 22, 18);
    v->setSpacing(10);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel("scene " + two(index + 1));
    title->setFont(titleFont(22));
    head->addWidget(title);
    head->addStretch();
    auto* del = keyButton("delete", this);
    auto* done = keyButton("done", this, true);
    head->addWidget(del);
    head->addWidget(done);
    v->addLayout(head);

    auto* nameLabel = new QLabel("name");
    nameLabel->setProperty("muted", true);
    nameLabel->setFont(monoFont(11));
    v->addWidget(nameLabel);
    m_name = new QLineEdit(m_scene.name);
    m_name->setFont(titleFont(18, false));
    v->addWidget(m_name);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& t) {
        m_scene.name = t.isEmpty() ? QString("Scene") : t;
        apply();
    });

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_rowsHost = new QWidget();
    m_rows = new QVBoxLayout(m_rowsHost);
    m_rows->setContentsMargins(0, 0, 6, 0);
    m_rows->setSpacing(10);
    scroll->setWidget(m_rowsHost);
    v->addWidget(scroll, 1);

    auto* bottom = new QHBoxLayout();
    auto* add = keyButton("+ layer", this);
    bottom->addWidget(add);
    bottom->addStretch();
    auto* hint = new QLabel("a layer is one sound on a range of keys · same keys = stacked · different ranges = split");
    hint->setProperty("muted", true);
    hint->setFont(monoFont(10));
    bottom->addWidget(hint);
    v->addLayout(bottom);

    connect(add, &QPushButton::clicked, this, [this]() {
        Layer l;
        l.sound = builtinRef("b:pad");
        l.vol = 0.7;
        m_scene.layers.push_back(l);
        apply();
        rebuild();
    });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(del, &QPushButton::clicked, this, [this]() {
        m_w->deleteScene(m_index);
        reject();
    });
    rebuild();
}

void EditDialog::apply()
{
    m_w->setScene(m_index, m_scene);
}

void EditDialog::rebuild()
{
    while (QLayoutItem* it = m_rows->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    const std::vector<SoundRef>& lib = m_w->library();
    for (size_t i = 0; i < m_scene.layers.size(); ++i) {
        auto* box = new QWidget();
        box->setObjectName("layer");
        box->setStyleSheet("QWidget#layer { background: #D9DADD; border-radius: 12px; }");
        auto* g = new QGridLayout(box);
        g->setContentsMargins(14, 10, 14, 12);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(6);
        auto label = [](const QString& t) {
            auto* l = new QLabel(t);
            l->setProperty("muted", true);
            l->setFont(monoFont(10));
            return l;
        };
        auto* head = new QHBoxLayout();
        auto* swatch = new QLabel();
        swatch->setFixedSize(14, 6);
        swatch->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(LAYER_COLORS[i % 6].name()));
        head->addWidget(swatch);
        head->addWidget(label(QString("layer %1").arg(i + 1)));
        head->addStretch();
        auto* remove = keyButton("remove", box);
        head->addWidget(remove);
        g->addLayout(head, 0, 0, 1, 4);

        // sound: type to search
        g->addWidget(label("sound"), 1, 0, 1, 4);
        auto* sound = new QComboBox();
        sound->setEditable(true);
        sound->setInsertPolicy(QComboBox::NoInsert);
        sound->setMaxVisibleItems(24);
        int sel = -1;
        for (size_t k = 0; k < lib.size(); ++k) {
            sound->addItem(lib[k].name + "   ·   " + lib[k].library.toLower() + " · " + lib[k].family.toLower(), int(k));
            if (lib[k].id == m_scene.layers[i].sound.id) {
                sel = int(k);
            }
        }
        if (sel < 0) {
            sound->addItem(m_scene.layers[i].sound.name + "   ·   missing", -1);
            sel = sound->count() - 1;
        }
        sound->setCurrentIndex(sel);
        sound->completer()->setFilterMode(Qt::MatchContains);
        sound->completer()->setCompletionMode(QCompleter::PopupCompletion);
        g->addWidget(sound, 2, 0, 1, 4);

        g->addWidget(label("lowest key"), 3, 0);
        g->addWidget(label("highest key"), 3, 1);
        g->addWidget(label("transpose"), 3, 2);
        g->addWidget(label("volume"), 3, 3);
        auto* lo = new QLineEdit(noteName(m_scene.layers[i].lo));
        auto* hi = new QLineEdit(noteName(m_scene.layers[i].hi));
        auto* tr = new QSpinBox();
        tr->setRange(-48, 48);
        tr->setValue(m_scene.layers[i].tr);
        auto* vol = new QSlider(Qt::Horizontal);
        vol->setRange(0, 100);
        vol->setValue(int(std::lround(m_scene.layers[i].vol * 100)));
        g->addWidget(lo, 4, 0);
        g->addWidget(hi, 4, 1);
        g->addWidget(tr, 4, 2);
        g->addWidget(vol, 4, 3);
        auto* fromKeys = keyButton("set range from the keyboard", box);
        auto* all = keyButton("all keys", box);
        auto* row = new QHBoxLayout();
        row->addWidget(fromKeys);
        row->addWidget(all);
        row->addStretch();
        g->addLayout(row, 5, 0, 1, 4);

        connect(sound, &QComboBox::activated, this, [this, i, sound](int idx) {
            const int k = sound->itemData(idx).toInt();
            if (k >= 0 && k < int(m_w->library().size())) {
                m_scene.layers[i].sound = m_w->library()[size_t(k)];
                apply();
            }
        });
        auto setNote = [this, i](QLineEdit* e, bool isLo) {
            const int n = parseNote(e->text());
            if (n < 0) {
                e->setText(noteName(isLo ? m_scene.layers[i].lo : m_scene.layers[i].hi));
                return;
            }
            (isLo ? m_scene.layers[i].lo : m_scene.layers[i].hi) = n;
            if (m_scene.layers[i].lo > m_scene.layers[i].hi) {
                std::swap(m_scene.layers[i].lo, m_scene.layers[i].hi);
            }
            apply();
        };
        connect(lo, &QLineEdit::editingFinished, this, [setNote, lo]() { setNote(lo, true); });
        connect(hi, &QLineEdit::editingFinished, this, [setNote, hi]() { setNote(hi, false); });
        connect(tr, &QSpinBox::valueChanged, this, [this, i](int v) {
            m_scene.layers[i].tr = v;
            apply();
        });
        connect(vol, &QSlider::valueChanged, this, [this, i](int v) {
            m_scene.layers[i].vol = v / 100.0;
            apply();
        });
        connect(all, &QPushButton::clicked, this, [this, i, lo, hi]() {
            m_scene.layers[i].lo = 0;
            m_scene.layers[i].hi = 127;
            lo->setText(noteName(0));
            hi->setText(noteName(127));
            apply();
        });
        connect(fromKeys, &QPushButton::clicked, this, [this, i, lo, hi, fromKeys]() {
            fromKeys->setText("press the lowest key, then the highest…");
            QPointer<EditDialog> self(this);
            m_w->learnRange([self, i, lo, hi, fromKeys](int a, int b) {
                if (!self) {
                    return;
                }
                self->m_scene.layers[i].lo = a;
                self->m_scene.layers[i].hi = b;
                lo->setText(noteName(a));
                hi->setText(noteName(b));
                fromKeys->setText("set range from the keyboard");
                self->apply();
            });
        });
        connect(remove, &QPushButton::clicked, this, [this, i]() {
            m_scene.layers.erase(m_scene.layers.begin() + long(i));
            apply();
            QMetaObject::invokeMethod(this, [this]() { rebuild(); }, Qt::QueuedConnection);
        });
        m_rows->addWidget(box);
    }
    m_rows->addStretch();
}

// ======================================================================== setup

SetupDialog::SetupDialog(MainWindow* w)
    : QDialog(w), m_w(w)
{
    setWindowTitle("Setup");
    setMinimumWidth(560);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(22, 18, 22, 18);
    v->setSpacing(8);
    auto label = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setProperty("muted", true);
        l->setFont(monoFont(11));
        return l;
    };
    auto* head = new QHBoxLayout();
    auto* title = new QLabel("setup");
    title->setFont(titleFont(22));
    head->addWidget(title);
    head->addStretch();
    auto* done = keyButton("done", this, true);
    head->addWidget(done);
    v->addLayout(head);

    v->addWidget(label("sound card"));
    m_device = new QComboBox();
    v->addWidget(m_device);
    auto* row = new QHBoxLayout();
    auto* col1 = new QVBoxLayout();
    col1->addWidget(label("buffer (smaller = sooner, until it crackles)"));
    m_buffer = new QComboBox();
    for (int f : { 32, 48, 64, 96, 128, 192, 256, 512, 1024 }) {
        m_buffer->addItem(QString("%1 frames").arg(f), f);
    }
    col1->addWidget(m_buffer);
    auto* col2 = new QVBoxLayout();
    col2->addWidget(label("sample rate"));
    m_rate = new QComboBox();
    m_rate->addItem("the card's own", 0.0);
    for (double r : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
        m_rate->addItem(QString("%1 Hz").arg(r, 0, 'f', 0), r);
    }
    col2->addWidget(m_rate);
    row->addLayout(col1);
    row->addLayout(col2);
    v->addLayout(row);
    m_exclusive = new QCheckBox("exclusive (WASAPI): Scenes takes the card for itself — lowest latency, other apps go quiet");
    m_exclusive->setFont(monoFont(11));
    v->addWidget(m_exclusive);
    auto* applyRow = new QHBoxLayout();
    auto* apply = keyButton("apply", this);
    auto* rescan = keyButton("look for cards again", this);
    applyRow->addWidget(apply);
    applyRow->addWidget(rescan);
    applyRow->addStretch();
    v->addLayout(applyRow);
    m_status = new QLabel();
    m_status->setFont(monoFont(11));
    m_status->setWordWrap(true);
    m_status->setStyleSheet("background: #121214; color: #EDEDE8; border-radius: 10px; padding: 12px;");
    v->addWidget(m_status);

    v->addSpacing(8);
    v->addWidget(label("MIDI keyboards (all of them play; plugged in later = found within 2 s)"));
    m_midi = new QLabel();
    m_midi->setFont(monoFont(12));
    m_midi->setWordWrap(true);
    v->addWidget(m_midi);

    v->addSpacing(8);
    v->addWidget(label("switch scenes with: a click · arrows / space / enter · page up-down (pedal) · MIDI program change · a learned key or pedal"));
    m_learn = new QLabel();
    m_learn->setFont(monoFont(12));
    v->addWidget(m_learn);
    auto* learnRow = new QHBoxLayout();
    auto* ln = keyButton("learn \"next\" key / pedal", this);
    auto* lp = keyButton("learn \"previous\" key / pedal", this);
    auto* clear = keyButton("forget", this);
    learnRow->addWidget(ln);
    learnRow->addWidget(lp);
    learnRow->addWidget(clear);
    learnRow->addStretch();
    v->addLayout(learnRow);

    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(apply, &QPushButton::clicked, this, [this]() {
        AudioSettings s;
        const QStringList d = m_device->currentData().toStringList();
        s.deviceName = d.value(0);
        s.hostApi = d.value(1);
        s.bufferFrames = m_buffer->currentData().toInt();
        s.sampleRate = m_rate->currentData().toDouble();
        s.exclusive = m_exclusive->isChecked();
        m_w->restartAudio(s);
        refresh();
    });
    connect(rescan, &QPushButton::clicked, this, [this]() {
        AudioSettings s = m_w->audio().settings();
        m_w->audio().stop();
        m_w->restartAudio(s); // PortAudio lists the cards again on restart
        fillDevices();
        refresh();
    });
    connect(ln, &QPushButton::clicked, this, [this]() { m_w->learnSwitch(true); m_learn->setText("press the key or pedal for \"next\"…"); });
    connect(lp, &QPushButton::clicked, this, [this]() { m_w->learnSwitch(false); m_learn->setText("press the key or pedal for \"previous\"…"); });
    connect(clear, &QPushButton::clicked, this, [this]() {
        m_w->m_set.learnNext = Learned();
        m_w->m_set.learnPrev = Learned();
        m_w->storeLearned();
        m_w->changed();
        refresh();
    });
    connect(&m_timer, &QTimer::timeout, this, &SetupDialog::refresh);
    m_timer.start(500);
    fillDevices();
    refresh();
}

void SetupDialog::fillDevices()
{
    m_device->clear();
    const AudioSettings& s = m_w->audio().settings();
    m_device->addItem("default (WASAPI)", QStringList { QString(), QString() });
    for (const AudioDevice& d : m_w->audio().devices()) {
        QString api = d.hostApi;
        api.remove("Windows ");
        m_device->addItem(d.name + "   ·   " + api.toLower(), QStringList { d.name, d.hostApi });
        if (d.name == s.deviceName && d.hostApi == s.hostApi) {
            m_device->setCurrentIndex(m_device->count() - 1);
        }
    }
    m_buffer->setCurrentIndex(std::max(0, m_buffer->findData(s.bufferFrames)));
    m_rate->setCurrentIndex(std::max(0, m_rate->findData(s.sampleRate)));
    m_exclusive->setChecked(s.exclusive);
}

void SetupDialog::refresh()
{
    AudioEngine& a = m_w->audio();
    if (a.isRunning()) {
        m_status->setText(QString("%1\n\noutput latency %2 ms  (buffer %3 ms + the card's own)\nCPU %4 %")
                          .arg(a.status())
                          .arg(a.outputLatencyMs(), 0, 'f', 1)
                          .arg(a.bufferMs(), 0, 'f', 1)
                          .arg(a.cpuLoad() * 100.0, 0, 'f', 0));
    } else {
        m_status->setText(a.status());
    }
    const QStringList midi = m_w->midiDevices();
    m_midi->setText(midi.isEmpty() ? QString("no keyboard") : midi.join("\n"));
    if (!m_w->m_learnMode.load()) {
        m_learn->setText(m_w->learnedText());
    }
}

// ======================================================================== main window

MainWindow::MainWindow()
{
    setWindowTitle("Scenes");
    setObjectName("body");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QString(STYLE) + QString("QPushButton, QComboBox, QLineEdit, QSpinBox, QMenu { font-family: \"%1\"; }").arg(Theme::mono()));
    setFont(monoFont(12));
    setFocusPolicy(Qt::StrongFocus);
    resize(1180, 760);
    setMinimumSize(760, 560);

    m_audio = new AudioEngine(this);
    connect(m_audio, &AudioEngine::stateChanged, this, [this]() { refresh(); });
    connect(m_audio, &AudioEngine::soundLoaded, this, [this](const QString&, bool) { refresh(); });

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &MainWindow::save);

    buildUi();

    // audio as it was last time; first time: WASAPI default, exclusive, 128 frames
    QSettings st = settings();
    AudioSettings as;
    as.deviceName = st.value("audio/device").toString();
    as.hostApi = st.value("audio/hostApi").toString();
    as.bufferFrames = st.value("audio/buffer", 128).toInt();
    as.exclusive = st.value("audio/exclusive", true).toBool();
    as.sampleRate = st.value("audio/rate", 0.0).toDouble();
    m_audio->start(as);
    m_volume->setValue(st.value("volume", 90).toInt());

    m_midi = new MidiIn([this](unsigned char s, unsigned char a, unsigned char b) { onMidi(s, a, b); }, this);
    connect(m_midi, &MidiIn::devicesChanged, this, [this]() { refresh(); });

    m_frame.setInterval(33);
    connect(&m_frame, &QTimer::timeout, this, [this]() {
        m_strip->update();
        m_display->update();
        const bool hit = QDateTime::currentMSecsSinceEpoch() - m_lastMidi.load() < 90;
        m_midiDot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(hit ? ORANGE.name() : QString("#9A9B9E")));
        if (m_perform) {
            m_perform->update();
        }
    });
    m_frame.start();

    scanLibrary();
}

MainWindow::~MainWindow()
{
    if (m_saveTimer.isActive()) {
        save();
    }
    if (m_scan) {
        m_scan->wait();
    }
    delete m_midi; // no more MIDI callbacks before the audio goes
    m_midi = nullptr;
    if (m_perform) {
        m_perform->close();
    }
}

void MainWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 14, 22, 16);
    root->setSpacing(14);

    // header: name, set, MIDI light, sound status, buttons
    auto* head = new QHBoxLayout();
    head->setSpacing(10);
    auto* logo = new QLabel("scenes");
    logo->setFont(titleFont(28));
    head->addWidget(logo);
    m_setButton = keyButton("set", this);
    m_setButton->setToolTip("sets: new, open, rename, import");
    connect(m_setButton, &QPushButton::clicked, this, &MainWindow::setMenu);
    head->addSpacing(6);
    head->addWidget(m_setButton);
    head->addSpacing(10);
    m_midiDot = new QWidget(this);
    m_midiDot->setFixedSize(10, 10);
    head->addWidget(m_midiDot);
    m_midiLabel = new QLabel("no keyboard");
    m_midiLabel->setProperty("muted", true);
    m_midiLabel->setFont(monoFont(11));
    head->addWidget(m_midiLabel);
    head->addStretch();
    m_audioLabel = new QLabel();
    m_audioLabel->setProperty("muted", true);
    m_audioLabel->setFont(monoFont(11));
    head->addWidget(m_audioLabel);
    auto* setupBtn = keyButton("setup", this);
    auto* performBtn = keyButton("perform", this);
    performBtn->setToolTip("full screen: huge current scene, next one under it (F11)");
    connect(setupBtn, &QPushButton::clicked, this, &MainWindow::openSetup);
    connect(performBtn, &QPushButton::clicked, this, &MainWindow::togglePerform);
    head->addWidget(setupBtn);
    head->addWidget(performBtn);
    root->addLayout(head);

    // body: display + keys on the left, the big round keys on the right
    auto* body = new QHBoxLayout();
    body->setSpacing(22);
    auto* left = new QVBoxLayout();
    left->setSpacing(12);
    m_display = new OledDisplay(this);
    m_strip = new KeyStrip(this);
    left->addWidget(m_display, 1);
    left->addWidget(m_strip);
    body->addLayout(left, 1);

    auto* right = new QVBoxLayout();
    right->setSpacing(14);
    auto* rounds = new QHBoxLayout();
    rounds->setSpacing(14);
    auto round = [this](const QString& t, bool accent) {
        auto* b = keyButton(t, this, accent);
        b->setProperty("round", true);
        b->setFixedSize(76, 76);
        b->setStyleSheet("border-radius: 38px;");
        return b;
    };
    auto* prevB = round("‹", false);
    auto* offB = round("off", true);
    offB->setFont(monoFont(12));
    offB->setToolTip("all notes off (Esc)");
    auto* nextB = round("›", false);
    connect(prevB, &QPushButton::clicked, this, &MainWindow::prev);
    connect(nextB, &QPushButton::clicked, this, &MainWindow::next);
    connect(offB, &QPushButton::clicked, this, &MainWindow::panic);
    rounds->addWidget(prevB);
    rounds->addWidget(offB);
    rounds->addWidget(nextB);
    right->addLayout(rounds);
    auto* row = new QHBoxLayout();
    auto* edit = keyButton("edit", this);
    auto* add = keyButton("+ scene", this);
    connect(edit, &QPushButton::clicked, this, [this]() { editScene(m_set.cur); });
    connect(add, &QPushButton::clicked, this, &MainWindow::addScene);
    row->addWidget(edit);
    row->addWidget(add);
    right->addLayout(row);
    auto* volLabel = new QLabel("volume");
    volLabel->setProperty("muted", true);
    volLabel->setFont(monoFont(11));
    right->addWidget(volLabel);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 120);
    m_volume->setFocusPolicy(Qt::NoFocus);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_audio->setMasterVolume(float(v) / 100.0f);
        settings().setValue("volume", v);
    });
    right->addWidget(m_volume);
    right->addStretch();
    auto* rightHost = new QWidget(this);
    rightHost->setLayout(right);
    rightHost->setFixedWidth(270);
    body->addWidget(rightHost);
    root->addLayout(body, 3);

    // the scenes, as big keys
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_keysHost = new QWidget();
    m_grid = new QGridLayout(m_keysHost);
    m_grid->setContentsMargins(0, 2, 6, 6);
    m_grid->setHorizontalSpacing(12);
    m_grid->setVerticalSpacing(10);
    m_scroll->setWidget(m_keysHost);
    root->addWidget(m_scroll, 2);

    auto* hint = new QLabel("click a scene · ← → space enter · page up/down (pedal) · 1–9 · MIDI program change · esc = all off · F11 perform · double-click to edit");
    hint->setProperty("muted", true);
    hint->setFont(monoFont(10));
    root->addWidget(hint);
}

void MainWindow::rebuildKeys()
{
    for (SceneKey* k : m_keys) {
        k->deleteLater();
    }
    m_keys.clear();
    for (int i = 0; i < int(m_set.scenes.size()); ++i) {
        m_keys.push_back(new SceneKey(this, i));
        m_keys.back()->setParent(m_keysHost);
    }
    layoutKeys();
}

void MainWindow::layoutKeys()
{
    while (m_grid->count()) {
        delete m_grid->takeAt(0);
    }
    const int w = m_scroll->viewport()->width() - 8;
    const int cols = std::max(1, (w + 12) / (170 + 12));
    for (int i = 0; i < int(m_keys.size()); ++i) {
        m_grid->addWidget(m_keys[size_t(i)], i / cols, i % cols);
        m_keys[size_t(i)]->show();
    }
    for (int c = 0; c < cols; ++c) {
        m_grid->setColumnStretch(c, 1);
    }
    m_grid->setRowStretch(int(m_keys.size()) / cols + 1, 1);
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutKeys();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    save();
    if (m_perform) {
        m_perform->close();
    }
    e->accept();
}

void MainWindow::refresh(bool flash)
{
    if (flash) {
        m_display->flash();
    }
    m_setButton->setText((m_set.name.isEmpty() ? QString("set") : m_set.name.toLower()) + "  ▾");
    const QStringList midi = midiDevices();
    m_midiLabel->setText(midi.isEmpty() ? QString("no keyboard") : midi.join(" · ").toLower());
    m_audioLabel->setText(m_audio->isRunning() ? QString("%1 ms").arg(m_audio->outputLatencyMs(), 0, 'f', 1) : m_audio->status());
    m_audioLabel->setToolTip(m_audio->status());
    m_display->update();
    m_strip->update();
    for (SceneKey* k : m_keys) {
        k->update();
    }
    if (m_perform) {
        m_perform->update();
    }
    // keep the current scene's key in view
    if (m_set.cur < int(m_keys.size())) {
        m_scroll->ensureWidgetVisible(m_keys[size_t(m_set.cur)], 0, 10);
    }
}

QStringList MainWindow::midiDevices() const
{
    return m_midi ? m_midi->openDevices() : QStringList();
}

bool MainWindow::midiActive() const
{
    return QDateTime::currentMSecsSinceEpoch() - m_lastMidi.load() < 90;
}

bool MainWindow::isSceneReady(int i) const
{
    return i >= 0 && i < int(m_set.scenes.size()) && m_audio->isReady(m_set.scenes[size_t(i)]);
}

QString MainWindow::learnedText() const
{
    auto f = [](const Learned& l) {
        return l.isSet() ? (l.type == 0x90 ? "key " + noteName(l.d1).toLower() : QString("control %1").arg(l.d1)) : QString("—");
    };
    return "MIDI next: " + f(m_set.learnNext) + "  ·  previous: " + f(m_set.learnPrev);
}

// ---------------------------------------------------------------- MIDI (driver thread)

void MainWindow::onMidi(unsigned char status, unsigned char d1, unsigned char d2)
{
    m_lastMidi = QDateTime::currentMSecsSinceEpoch();
    const int type = status & 0xF0;
    const bool press = (type == 0x90 && d2 > 0) || (type == 0xB0 && d2 >= 64);
    const int learn = m_learnMode.load();
    if ((learn == 1 || learn == 2) && press) {
        QMetaObject::invokeMethod(this, [this, type, d1]() { onLearned(type, d1); }, Qt::QueuedConnection);
        return;
    }
    if (learn == 3) {
        if (type == 0x90 && d2 > 0) {
            QMetaObject::invokeMethod(this, [this, d1]() { onRangeKey(d1); }, Qt::QueuedConnection);
        }
        if (type == 0x90 || type == 0x80) {
            return;
        }
    }
    auto matches = [&](int learned) {
        if (!learned || (learned & 0xFF) != d1) {
            return false;
        }
        const int lt = learned >> 8;
        return lt == 0x90 ? (type == 0x90 || type == 0x80) : (type == lt);
    };
    if (matches(m_learnNext.load())) {
        if (press) {
            QMetaObject::invokeMethod(this, [this]() { next(); }, Qt::QueuedConnection);
        }
        return;
    }
    if (matches(m_learnPrev.load())) {
        if (press) {
            QMetaObject::invokeMethod(this, [this]() { prev(); }, Qt::QueuedConnection);
        }
        return;
    }
    switch (type) {
    case 0x90:
        if (d2 > 0) {
            m_audio->noteOn(d1, d2);
        } else {
            m_audio->noteOff(d1);
        }
        break;
    case 0x80:
        m_audio->noteOff(d1);
        break;
    case 0xB0:
        m_audio->controlChange(d1, d2);
        break;
    case 0xE0:
        m_audio->pitchBend(((d2 << 7) | d1) - 8192);
        break;
    case 0xC0:
        QMetaObject::invokeMethod(this, [this, d1]() { go(d1); }, Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

void MainWindow::learnSwitch(bool nextKey)
{
    m_learnMode = nextKey ? 1 : 2;
}

void MainWindow::onLearned(int type, int d1)
{
    const int mode = m_learnMode.exchange(0);
    if (mode != 1 && mode != 2) {
        return;
    }
    Learned l;
    l.type = type;
    l.d1 = d1;
    (mode == 1 ? m_set.learnNext : m_set.learnPrev) = l;
    storeLearned();
    changed();
}

void MainWindow::storeLearned()
{
    auto pack = [](const Learned& l) { return l.isSet() ? (l.type << 8) | l.d1 : 0; };
    m_learnNext = pack(m_set.learnNext);
    m_learnPrev = pack(m_set.learnPrev);
}

void MainWindow::learnRange(std::function<void(int, int)> done)
{
    m_rangeDone = std::move(done);
    m_rangeFirst = -1;
    m_learnMode = 3;
}

void MainWindow::onRangeKey(int note)
{
    if (m_learnMode.load() != 3) {
        return;
    }
    if (m_rangeFirst < 0) {
        m_rangeFirst = note;
        return;
    }
    m_learnMode = 0;
    const int a = std::min(m_rangeFirst, note), b = std::max(m_rangeFirst, note);
    m_rangeFirst = -1;
    if (auto f = std::move(m_rangeDone)) {
        m_rangeDone = nullptr;
        f(a, b);
    }
}

// ---------------------------------------------------------------- switching

bool MainWindow::handleSwitchKey(int key)
{
    switch (key) {
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_PageDown: case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter:
        next();
        return true;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Backspace:
        prev();
        return true;
    case Qt::Key_Escape:
        panic();
        return true;
    case Qt::Key_F11:
        togglePerform();
        return true;
    case Qt::Key_Home:
        go(0);
        return true;
    case Qt::Key_End:
        go(int(m_set.scenes.size()) - 1);
        return true;
    default:
        if (key >= Qt::Key_1 && key <= Qt::Key_9) {
            go(key - Qt::Key_1);
            return true;
        }
    }
    return false;
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (!e->isAutoRepeat() && handleSwitchKey(e->key())) {
        return;
    }
    if (e->key() == Qt::Key_E && !e->isAutoRepeat()) {
        editScene(m_set.cur);
        return;
    }
    QWidget::keyPressEvent(e);
}

void MainWindow::go(int index)
{
    if (m_set.scenes.empty()) {
        return;
    }
    index = std::clamp(index, 0, int(m_set.scenes.size()) - 1);
    if (index == m_set.cur) {
        refresh(true);
        return;
    }
    m_set.cur = index;
    m_audio->setScene(m_set.scenes[size_t(index)]); // held notes ring on; new notes play the new scene
    refresh(true);
    m_saveTimer.start();
}

void MainWindow::panic()
{
    m_audio->allNotesOff();
}

void MainWindow::togglePerform()
{
    if (m_perform) {
        m_perform->close();
        activateWindow();
        return;
    }
    m_perform = new PerformView(this);
    if (QScreen* s = screen()) {
        m_perform->setGeometry(s->geometry());
    }
    m_perform->showFullScreen();
    m_perform->activateWindow();
    m_perform->setFocus();
}

void MainWindow::openSetup()
{
    SetupDialog d(this);
    d.exec();
    m_learnMode = 0;
}

void MainWindow::restartAudio(const AudioSettings& s)
{
    m_audio->start(s);
    QSettings st = settings();
    st.setValue("audio/device", s.deviceName);
    st.setValue("audio/hostApi", s.hostApi);
    st.setValue("audio/buffer", s.bufferFrames);
    st.setValue("audio/exclusive", s.exclusive);
    st.setValue("audio/rate", s.sampleRate);
    refresh();
}

// ---------------------------------------------------------------- editing

void MainWindow::editScene(int index)
{
    if (index < 0 || index >= int(m_set.scenes.size())) {
        return;
    }
    if (m_perform) {
        m_perform->close();
    }
    EditDialog d(this, index);
    d.exec();
    m_learnMode = 0;
    rebuildKeys();
    refresh();
}

void MainWindow::setScene(int index, const Scene& scene)
{
    if (index < 0 || index >= int(m_set.scenes.size())) {
        return;
    }
    m_set.scenes[size_t(index)] = scene;
    changed();
}

void MainWindow::addScene()
{
    Scene sc;
    sc.name = "New scene";
    Layer l;
    l.sound = builtinRef("b:epiano");
    sc.layers.push_back(l);
    const int at = m_set.scenes.empty() ? 0 : m_set.cur + 1;
    m_set.scenes.insert(m_set.scenes.begin() + at, sc);
    m_set.cur = at;
    rebuildKeys();
    changed();
    editScene(at);
}

void MainWindow::duplicateScene(int index)
{
    if (index < 0 || index >= int(m_set.scenes.size())) {
        return;
    }
    Scene sc = m_set.scenes[size_t(index)];
    sc.name += " 2";
    m_set.scenes.insert(m_set.scenes.begin() + index + 1, sc);
    rebuildKeys();
    changed();
}

void MainWindow::moveScene(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= int(m_set.scenes.size()) || to >= int(m_set.scenes.size())) {
        return;
    }
    std::swap(m_set.scenes[size_t(index)], m_set.scenes[size_t(to)]);
    if (m_set.cur == index) {
        m_set.cur = to;
    } else if (m_set.cur == to) {
        m_set.cur = index;
    }
    changed();
}

void MainWindow::deleteScene(int index)
{
    if (index < 0 || index >= int(m_set.scenes.size())) {
        return;
    }
    m_set.scenes.erase(m_set.scenes.begin() + index);
    m_set.cur = std::clamp(m_set.cur > index ? m_set.cur - 1 : m_set.cur, 0, std::max(0, int(m_set.scenes.size()) - 1));
    rebuildKeys();
    changed();
}

void MainWindow::changed()
{
    if (!m_set.scenes.empty()) {
        m_audio->preload(m_set, m_set.cur);
        m_audio->setScene(m_set.scenes[size_t(m_set.cur)]);
    } else {
        m_audio->setScene(Scene());
    }
    if (m_keys.size() != m_set.scenes.size()) {
        rebuildKeys();
    }
    refresh();
    m_saveTimer.start();
}

void MainWindow::save()
{
    m_saveTimer.stop();
    if (m_path.isEmpty()) {
        m_path = setPath(m_set.name);
    }
    QString error;
    if (!saveSet(m_set, m_path, &error)) {
        m_audioLabel->setText(error);
    }
}

// ---------------------------------------------------------------- sets

void MainWindow::scanLibrary()
{
    // the library takes a moment (VSCO, Salamander, GeneralUser's programs, Sampler kits): in the background
    QThread* t = QThread::create([this]() {
        std::vector<SoundRef> lib = allSounds();
        QMetaObject::invokeMethod(this, [this, lib]() {
            m_library = lib;
            m_libraryReady = true;
            applyResolved();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    m_scan = t;
    t->start();
}

void MainWindow::applyResolved()
{
    if (m_libraryReady) {
        for (Scene& sc : m_set.scenes) {
            for (Layer& l : sc.layers) {
                l.sound = resolve(l.sound, m_library);
            }
        }
    }
    storeLearned();
    rebuildKeys();
    if (!m_set.scenes.empty()) {
        m_audio->preload(m_set, m_set.cur);
        m_audio->setScene(m_set.scenes[size_t(m_set.cur)]);
    }
    refresh(true);
}

void MainWindow::openSet(const QString& path)
{
    SceneSet s;
    QString error;
    if (!loadSet(path, s, &error)) {
        QMessageBox::warning(this, "Scenes", error);
        return;
    }
    if (!m_path.isEmpty() && m_saveTimer.isActive()) {
        save();
    }
    m_set = s;
    m_path = path;
    settings().setValue("lastSet", path);
    applyResolved();
}

void MainWindow::setMenu()
{
    QMenu m(this);
    m.addAction("new set…", [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Scenes", "name of the new set", QLineEdit::Normal, "New set", &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        save();
        SceneSet s = defaultSet();
        s.name = name.trimmed();
        s.scenes.resize(1);
        m_set = s;
        m_path = setPath(s.name);
        settings().setValue("lastSet", m_path);
        applyResolved();
        save();
    });
    m.addAction("open…", [this]() {
        QDir().mkpath(scenesFolder());
        const QString f = QFileDialog::getOpenFileName(this, "Open a set", scenesFolder(), "Scenes sets (*.scenes.json *.json)");
        if (!f.isEmpty()) {
            openSet(f);
        }
    });
    QMenu* recent = m.addMenu("sets");
    for (const QFileInfo& fi : QDir(scenesFolder()).entryInfoList({ "*.scenes.json" }, QDir::Files, QDir::Time)) {
        recent->addAction(fi.fileName().section(".scenes", 0, 0), [this, p = fi.absoluteFilePath()]() { openSet(p); });
    }
    recent->setEnabled(!recent->isEmpty());
    m.addAction("rename…", [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Scenes", "name of this set", QLineEdit::Normal, m_set.name, &ok);
        if (!ok || name.trimmed().isEmpty() || name.trimmed() == m_set.name) {
            return;
        }
        const QString old = m_path;
        m_set.name = name.trimmed();
        m_path = setPath(m_set.name);
        save();
        if (!old.isEmpty() && old != m_path && QFileInfo(old).absolutePath() == QFileInfo(m_path).absolutePath()) {
            QFile::remove(old);
        }
        settings().setValue("lastSet", m_path);
        refresh();
    });
    m.addAction("save a copy as…", [this]() {
        const QString f = QFileDialog::getSaveFileName(this, "Save a copy", setPath(m_set.name + " copy"), "Scenes sets (*.scenes.json)");
        if (!f.isEmpty()) {
            saveSet(m_set, f);
        }
    });
    m.addSeparator();
    m.addAction("import from the browser Scenes (paste)", [this]() {
        // the browser app's "copy setlist" puts {"scenes":[...]} on the clipboard
        SceneSet s;
        QString error;
        if (!setFromJson(QApplication::clipboard()->text().toUtf8(), s, &error)) {
            QMessageBox::information(this, "Scenes", "In the browser Scenes press setup > copy setlist, then come back here.\n\n" + error);
            return;
        }
        save();
        s.name = "Imported " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm");
        m_set = s;
        m_path = setPath(s.name);
        settings().setValue("lastSet", m_path);
        applyResolved();
        save();
    });
    m.addAction("show the Scenes folder", []() {
        QDir().mkpath(scenesFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(scenesFolder()));
    });
    m.exec(m_setButton->mapToGlobal(QPoint(0, m_setButton->height() + 4)));
}
