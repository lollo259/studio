/*
 * Studio (Laurence Who Studio): one window that holds Score and Tape.
 *
 * Starts both apps and takes their windows inside its own: Score above, Tape below,
 * or one of them full size. The top of the window is the Studio machine: a display
 * (time, link), the transport (back, play/stop), and keys to choose the page.
 * The transport talks over the Studio live link, so both apps play together.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFileInfo>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "studiolinkpeer.h"

namespace {
const QColor ALU_TOP("#DADBDE");
const QColor ALU_BOTTOM("#C9CACD");
const QColor INK("#1E1E21");
const QColor ORANGE("#FF5A1F");
const QColor BLUE("#3C7BFF");
const QColor GREEN("#2CD47C");
const QColor OLED("#0B0B0C");

QString fmtTime(double s)
{
    if (s < 0) {
        s = 0;
    }
    const int total = int(s);
    const int h = total / 3600;
    const int m = (total / 60) % 60;
    const int sec = total % 60;
    const int tenth = int((s - total) * 10);
    if (h > 0) {
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    }
    return QString("%1:%2.%3").arg(m).arg(sec, 2, 10, QChar('0')).arg(tenth);
}
}

// ------------------------------------------------------------------ the machine top
class TopBar : public QWidget
{
    Q_OBJECT
public:
    enum Key { None, Back, Play, Link, PageScore, PageSplit, PageTape, Min, Max, Close };

    explicit TopBar(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(104);
        setMouseTracking(true);
    }

    double time = 0;
    bool playing = false;
    bool linked = false;
    bool scoreUp = false;
    bool tapeUp = false;
    QString page = "split";
    QString note;

signals:
    void pressed(TopBar::Key key);

protected:
    QRectF keyRect(Key k) const
    {
        const double y = 30;
        const double s = 56;
        switch (k) {
        case Back: return QRectF(560, y, s, s);
        case Play: return QRectF(626, y, s, s);
        case Link: return QRectF(692, y, s, s);
        case PageScore: return QRectF(width() - 3 * 66 - 18, y, s, s);
        case PageSplit: return QRectF(width() - 2 * 66 - 18, y, s, s);
        case PageTape: return QRectF(width() - 66 - 18, y, s, s);
        case Min: return QRectF(width() - 3 * 34 - 10, 2, 30, 22);
        case Max: return QRectF(width() - 2 * 34 - 10, 2, 30, 22);
        case Close: return QRectF(width() - 34 - 10, 2, 30, 22);
        default: return {};
        }
    }

    Key keyAt(const QPointF& p) const
    {
        for (Key k : { Back, Play, Link, PageScore, PageSplit, PageTape, Min, Max, Close }) {
            if (keyRect(k).contains(p)) {
                return k;
            }
        }
        return None;
    }

    void drawKey(QPainter& p, Key k, const QString& label, const QString& glyph, bool dark, bool led, const QColor& ledColor,
                 const QColor& glyphColor = INK)
    {
        const QRectF r = keyRect(k);
        const bool down = m_down == k;
        const QRectF body = r.translated(0, down ? 1 : 0);
        // shadow + body
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, down ? 40 : 70));
        p.drawRoundedRect(body.translated(0, down ? 1 : 2), 9, 9);
        QLinearGradient g(body.topLeft(), body.bottomLeft());
        if (dark) {
            g.setColorAt(0, QColor("#3A3A3E"));
            g.setColorAt(1, QColor("#252528"));
        } else {
            g.setColorAt(0, QColor(m_hover == k ? "#FAFAFB" : "#F4F4F5"));
            g.setColorAt(1, QColor("#E3E3E5"));
        }
        p.setBrush(g);
        p.drawRoundedRect(body, 9, 9);
        // led
        p.setBrush(led ? ledColor : QColor("#C6C7CA"));
        p.drawEllipse(QPointF(body.right() - 10, body.top() + 10), 3, 3);
        // glyph + label
        QFont f("Consolas");
        f.setStyleHint(QFont::Monospace);
        f.setPixelSize(18);
        p.setFont(f);
        p.setPen(dark ? QColor("#F2F2F0") : glyphColor);
        p.drawText(body.adjusted(0, 6, 0, -20), Qt::AlignCenter, glyph);
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(dark ? QColor("#9A9A9E") : QColor("#4A4A4F"));
        p.drawText(body.adjusted(0, 0, 0, -6), Qt::AlignHCenter | Qt::AlignBottom, label);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient bg(0, 0, 0, height());
        bg.setColorAt(0, ALU_TOP);
        bg.setColorAt(1, ALU_BOTTOM);
        p.fillRect(rect(), bg);
        p.setPen(QColor("#F2F2F3"));
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor("#A9AAAE"));
        p.drawLine(0, height() - 1, width(), height() - 1);

        // brand
        QFont jost("Jost");
        jost.setPixelSize(19);
        p.setFont(jost);
        p.setPen(INK);
        p.drawText(QPointF(20, 21), "studio");
        p.setPen(ORANGE);
        p.drawText(QPointF(20 + QFontMetrics(jost).horizontalAdvance("studio"), 21), ".");

        // display
        const QRectF d(20, 30, 520, 62);
        p.setPen(Qt::NoPen);
        p.setBrush(OLED);
        p.drawRoundedRect(d, 10, 10);
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        mono.setPixelSize(9);
        p.setFont(mono);
        p.setPen(QColor("#77777A"));
        p.drawText(QPointF(d.left() + 16, d.top() + 16), "TIME");
        p.drawText(QPointF(d.left() + 210, d.top() + 16), "LINK");
        p.drawText(QPointF(d.left() + 330, d.top() + 16), "APPS");
        mono.setPixelSize(30);
        p.setFont(mono);
        p.setPen(playing ? ORANGE : QColor("#E8E8E4"));
        p.drawText(QPointF(d.left() + 14, d.top() + 50), fmtTime(time));
        mono.setPixelSize(13);
        p.setFont(mono);
        p.setPen(linked ? BLUE : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 210, d.top() + 44), linked ? "linked" : "waiting");
        p.setPen(scoreUp ? GREEN : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 330, d.top() + 44), "score");
        p.setPen(tapeUp ? GREEN : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 400, d.top() + 44), "tape");
        if (!note.isEmpty()) {
            mono.setPixelSize(9);
            p.setFont(mono);
            p.setPen(QColor("#77777A"));
            p.drawText(QPointF(d.left() + 210, d.top() + 56), note);
        }

        // transport
        drawKey(p, Back, "back", "‹‹", false, false, ORANGE);
        drawKey(p, Play, playing ? "stop" : "play", playing ? "■" : "▶", true, playing, ORANGE, ORANGE);
        drawKey(p, Link, "link", "", false, linked, BLUE);

        // pages
        drawKey(p, PageScore, "score", "", false, page == "score", ORANGE);
        drawKey(p, PageSplit, "both", "", false, page == "split", ORANGE);
        drawKey(p, PageTape, "tape", "", false, page == "tape", ORANGE);

        // window keys
        QFont wf("Jost");
        wf.setPixelSize(13);
        p.setFont(wf);
        p.setPen(QColor("#3A3A3E"));
        for (auto [k, g] : { std::pair { Min, QString("–") }, std::pair { Max, QString("□") }, std::pair { Close, QString("×") } }) {
            if (m_hover == k) {
                p.setPen(Qt::NoPen);
                p.setBrush(k == Close ? ORANGE : QColor("#E3E3E5"));
                p.drawRoundedRect(keyRect(k), 4, 4);
                p.setPen(k == Close ? QColor("#FFFFFF") : QColor("#3A3A3E"));
            } else {
                p.setPen(QColor("#3A3A3E"));
            }
            p.drawText(keyRect(k), Qt::AlignCenter, g);
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        m_down = keyAt(e->position());
        if (m_down == None && e->button() == Qt::LeftButton && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove(); // drag the window by its top
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        const Key k = keyAt(e->position());
        if (k != None && k == m_down) {
            emit pressed(k);
        }
        m_down = None;
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (keyAt(e->position()) == None) {
            emit pressed(Max);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const Key k = keyAt(e->position());
        if (k != m_hover) {
            m_hover = k;
            update();
        }
    }

    void leaveEvent(QEvent*) override
    {
        m_hover = None;
        update();
    }

private:
    Key m_down = None;
    Key m_hover = None;
};

// ------------------------------------------------------------------ one app inside
class AppSlot : public QWidget
{
    Q_OBJECT
public:
    AppSlot(const QString& name, const QString& exe, QWidget* parent)
        : QWidget(parent), m_name(name), m_exe(exe)
    {
        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        setMinimumHeight(120);
        m_find.setInterval(250);
        connect(&m_find, &QTimer::timeout, this, &AppSlot::lookForWindow);
    }

    bool isUp() const { return m_container != nullptr; }
    QString status() const { return m_status; }

    void launch()
    {
        if (!QFileInfo::exists(m_exe)) {
            m_status = m_name + " not found";
            update();
            return;
        }
        m_proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("STUDIO_EMBEDDED", "1");
        m_proc->setProcessEnvironment(env);
        m_proc->setWorkingDirectory(QFileInfo(m_exe).absolutePath());
        connect(m_proc, &QProcess::finished, this, [this]() {
            delete m_container;
            m_container = nullptr;
            m_hwnd = 0;
            m_status = m_name + " closed";
            emit changed();
            update();
        });
        m_proc->start(m_exe, QStringList());
        m_started.start();
        m_status = "starting " + m_name + "…";
        m_find.start();
        update();
    }

    void closeApp()
    {
#ifdef Q_OS_WIN
        if (m_hwnd) {
            PostMessageW(reinterpret_cast<HWND>(m_hwnd), WM_CLOSE, 0, 0);
        }
#endif
    }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_container) {
            return;
        }
        QPainter p(this);
        p.fillRect(rect(), QColor("#BFC0C4"));
        QFont f("Consolas");
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor("#4A4A4F"));
        p.drawText(rect(), Qt::AlignCenter, m_status);
    }

private:
    void lookForWindow()
    {
#ifdef Q_OS_WIN
        if (!m_proc || m_proc->state() != QProcess::Running) {
            return;
        }
        struct Find {
            DWORD pid;
            HWND best;
            long area;
        } f { DWORD(m_proc->processId()), nullptr, 0 };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<Find*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) {
                return TRUE;
            }
            if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
                return TRUE;
            }
            RECT r;
            GetWindowRect(h, &r);
            const long area = long(r.right - r.left) * long(r.bottom - r.top);
            if (area > f->area && (r.right - r.left) >= 800) {
                f->area = area;
                f->best = h;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&f));

        // the main window is the big one that stays (splash screens are smaller and go away)
        if (!f.best) {
            m_candidate = 0;
            return;
        }
        if (WId(f.best) != m_candidate) {
            m_candidate = WId(f.best);
            m_candidateSince.start();
            return;
        }
        if (m_candidateSince.elapsed() < 1200) {
            return;
        }
        m_find.stop();
        m_hwnd = m_candidate;
        QWindow* w = QWindow::fromWinId(m_hwnd);
        w->setFlags(Qt::FramelessWindowHint);
        m_container = QWidget::createWindowContainer(w, this);
        m_container->setFocusPolicy(Qt::StrongFocus);
        layout()->addWidget(m_container);
        m_status.clear();
        emit changed();
#endif
    }

    QString m_name;
    QString m_exe;
    QString m_status;
    QProcess* m_proc = nullptr;
    QWidget* m_container = nullptr;
    WId m_hwnd = 0;
    WId m_candidate = 0;
    QElapsedTimer m_started;
    QElapsedTimer m_candidateSince;
    QTimer m_find;
};

// ------------------------------------------------------------------ the window
class StudioWindow : public QWidget
{
    Q_OBJECT
public:
    StudioWindow()
        : m_link("studio")
    {
        setWindowTitle("Studio");
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        setMinimumSize(900, 600);
        setMouseTracking(true);

        // Studio/Score/bin/StudioHost.exe -> Studio/Score/bin/<score>.exe and Studio/Tape/bin/Audacity4.exe
        const QDir bin(QCoreApplication::applicationDirPath());
        QString scoreExe;
        for (const QString& n : bin.entryList({ "LaurenceWhoScore*.exe", "MuseScore*.exe" }, QDir::Files)) {
            scoreExe = bin.filePath(n);
        }
        const QString tapeExe = QDir::cleanPath(bin.filePath("../../Tape/bin/Audacity4.exe"));

        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(6, 0, 6, 6);
        l->setSpacing(6);
        m_top = new TopBar(this);
        l->addWidget(m_top);
        m_split = new QSplitter(Qt::Vertical, this);
        m_split->setHandleWidth(8);
        m_split->setStyleSheet("QSplitter::handle { background: #C9CACD; } QSplitter::handle:hover { background: #FF5A1F; }");
        m_score = new AppSlot("Score", scoreExe, m_split);
        m_tape = new AppSlot("Tape", tapeExe, m_split);
        m_split->addWidget(m_score);
        m_split->addWidget(m_tape);
        m_split->setStretchFactor(0, 3);
        m_split->setStretchFactor(1, 2);
        l->addWidget(m_split, 1);

        connect(m_top, &TopBar::pressed, this, &StudioWindow::onKey);
        connect(m_score, &AppSlot::changed, this, &StudioWindow::refresh);
        connect(m_tape, &AppSlot::changed, this, &StudioWindow::refresh);

        connect(&m_link, &studiolink::Peer::received, this, [this](const QJsonObject& m) {
            const QString cmd = m.value("cmd").toString();
            if (m.contains("t")) {
                m_top->time = m.value("t").toDouble();
            }
            if (cmd == "play") {
                m_top->playing = true;
            } else if (cmd == "stop") {
                m_top->playing = false;
            } else if (cmd == "show") {
                setPage(m.value("page").toString());
            }
            m_top->update();
        });
        connect(&m_link, &studiolink::Peer::linkedChanged, this, [this](bool on) {
            m_top->linked = on;
            m_top->update();
        });
        m_link.start();

        resize(1440, 900);
        QTimer::singleShot(100, m_score, &AppSlot::launch);
        QTimer::singleShot(400, m_tape, &AppSlot::launch);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), ALU_BOTTOM);
    }

    // resize from the edges (the window has no system frame)
    void mousePressEvent(QMouseEvent* e) override
    {
        const Qt::Edges edges = edgesAt(e->position().toPoint());
        if (edges && windowHandle()) {
            windowHandle()->startSystemResize(edges);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const Qt::Edges ed = edgesAt(e->position().toPoint());
        if ((ed & Qt::LeftEdge && ed & Qt::TopEdge) || (ed & Qt::RightEdge && ed & Qt::BottomEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if ((ed & Qt::RightEdge && ed & Qt::TopEdge) || (ed & Qt::LeftEdge && ed & Qt::BottomEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (ed & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (ed & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }

    void closeEvent(QCloseEvent* e) override
    {
        // let each app ask about unsaved work, then go
        m_score->closeApp();
        m_tape->closeApp();
        e->accept();
    }

private:
    Qt::Edges edgesAt(const QPoint& p) const
    {
        const int m = 6;
        Qt::Edges e;
        if (p.x() < m) e |= Qt::LeftEdge;
        if (p.x() > width() - m) e |= Qt::RightEdge;
        if (p.y() < m) e |= Qt::TopEdge;
        if (p.y() > height() - m) e |= Qt::BottomEdge;
        return e;
    }

    void refresh()
    {
        m_top->scoreUp = m_score->isUp();
        m_top->tapeUp = m_tape->isUp();
        m_top->note = !m_score->status().isEmpty() ? m_score->status() : m_tape->status();
        m_top->update();
    }

    void setPage(const QString& page)
    {
        m_top->page = page;
        m_score->setVisible(page != "tape");
        m_tape->setVisible(page != "score");
        m_top->update();
    }

    void onKey(TopBar::Key k)
    {
        switch (k) {
        case TopBar::Back:
            m_link.send(QJsonObject { { "cmd", m_top->playing ? "play" : "stop" }, { "t", 0.0 } });
            m_top->time = 0;
            break;
        case TopBar::Play:
            m_top->playing = !m_top->playing;
            m_link.send(QJsonObject { { "cmd", m_top->playing ? "play" : "stop" }, { "t", m_top->time } });
            break;
        case TopBar::Link:
            m_link.send(QJsonObject { { "cmd", "hello" } }); // ask both apps where they are
            break;
        case TopBar::PageScore: setPage("score");
            break;
        case TopBar::PageSplit: setPage("split");
            break;
        case TopBar::PageTape: setPage("tape");
            break;
        case TopBar::Min: showMinimized();
            break;
        case TopBar::Max: isMaximized() ? showNormal() : showMaximized();
            break;
        case TopBar::Close: close();
            break;
        default: break;
        }
        m_top->update();
    }

    studiolink::Peer m_link;
    TopBar* m_top = nullptr;
    QSplitter* m_split = nullptr;
    AppSlot* m_score = nullptr;
    AppSlot* m_tape = nullptr;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    for (const char* f : { ":/fonts/Jost-Regular.ttf", ":/fonts/JostStar-Light.ttf" }) {
        QFontDatabase::addApplicationFont(f);
    }
    app.setApplicationName("Studio");
    app.setOrganizationName("LaurenceWho");
    StudioWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"
