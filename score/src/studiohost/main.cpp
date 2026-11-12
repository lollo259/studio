/*
 * Studio (Laurence Who Studio): one window that holds Score, Tape and Mail.
 *
 * Starts the apps and takes their windows inside its own: Score above, Tape below,
 * or one of them full size. Mail is Thunderbird, with its own profile under
 * %APPDATA%/LaurenceWho/Studio/MailProfile and Studio's look (user.js + userChrome.css).
 * The top of the window is the Studio machine: a display
 * (time, link), the transport (back, play/stop), and keys to choose the page.
 * The transport talks over the Studio live link, so both apps play together.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <algorithm>
#include <string>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QSet>
#include <QShortcut>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wchar.h>
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

// the pages, in key order (Ctrl+Tab goes round them)
const QStringList PAGES { "score", "tape", "mail" };

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
    enum Key { None, Back, Play, Link, PageScore, PageSplit, PageTape, PageMail, Min, Max, Close };

    explicit TopBar(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(104);
        setMouseTracking(true);
    }

    double time = 0;
    bool playing = false;
    bool linked = false;
    QSet<QString> up;  // pages whose app window is inside
    QSet<QString> out; // pages popped out into their own window
    QString page = "score";
    QString note;

    static QString pageOf(Key k)
    {
        switch (k) {
        case PageScore: return "score";
        case PageTape: return "tape";
        case PageMail: return "mail";
        default: return {};
        }
    }

signals:
    void pressed(TopBar::Key key);
    void detach(TopBar::Key key, QPoint globalPos);

protected:
    QRectF keyRect(Key k) const
    {
        const double y = 30;
        const double s = 56;
        switch (k) {
        case Back: return QRectF(560, y, s, s);
        case Play: return QRectF(626, y, s, s);
        case Link: return QRectF(692, y, s, s);
        case PageScore: return QRectF(width() - 4 * 66 - 18, y, s, s);
        case PageSplit: return QRectF(width() - 3 * 66 - 18, y, s, s);
        case PageTape: return QRectF(width() - 2 * 66 - 18, y, s, s);
        case PageMail: return QRectF(width() - 66 - 18, y, s, s);
        case Min: return QRectF(width() - 3 * 34 - 10, 2, 30, 22);
        case Max: return QRectF(width() - 2 * 34 - 10, 2, 30, 22);
        case Close: return QRectF(width() - 34 - 10, 2, 30, 22);
        default: return {};
        }
    }

    Key keyAt(const QPointF& p) const
    {
        for (Key k : { Back, Play, Link, PageScore, PageSplit, PageTape, PageMail, Min, Max, Close }) {
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
        p.setPen(up.contains("score") ? GREEN : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 330, d.top() + 44), "score");
        p.setPen(up.contains("tape") ? GREEN : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 400, d.top() + 44), "tape");
        p.setPen(up.contains("mail") ? GREEN : QColor("#55555A"));
        p.drawText(QPointF(d.left() + 456, d.top() + 44), "mail");
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
        for (Key k : { PageScore, PageTape, PageMail }) {
            const QString pg = pageOf(k);
            const bool o = out.contains(pg);
            drawKey(p, k, o ? pg + " ↗" : pg, "", false, o || page == pg, o ? BLUE : ORANGE);
        }
        drawKey(p, PageSplit, "both", "", false, page == "split", ORANGE);

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
        const Key k = keyAt(e->position());
        if (k == None) {
            emit pressed(Max);
        } else if (!pageOf(k).isEmpty()) {
            m_down = None;
            emit detach(k, e->globalPosition().toPoint()); // double-click a page key: it pops out
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        // drag a page key out of the bar: it pops out where you drop it
        if (!pageOf(m_down).isEmpty() && (e->buttons() & Qt::LeftButton)
            && !rect().adjusted(0, 0, 0, 30).contains(e->position().toPoint())) {
            const Key d = m_down;
            m_down = None;
            update();
            emit detach(d, e->globalPosition().toPoint());
            return;
        }
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
        m_alive.setInterval(1000);
        connect(&m_alive, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WIN
            if (m_hwnd && !IsWindow(reinterpret_cast<HWND>(m_hwnd))) {
                dropWindow(); // its window went away (e.g. closed from inside, or owned by another process)
            }
#endif
        });
    }

    ~AppSlot() override
    {
        // never kill a running app on the way out: let it finish closing by itself
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            m_proc->disconnect(this);
            m_proc->setParent(nullptr);
        }
    }

    bool isUp() const { return m_container != nullptr; }
    QString status() const { return m_status; }

    // still running (its process, or the window it handed over to)
    bool isRunning() const
    {
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            return true;
        }
#ifdef Q_OS_WIN
        if (m_hwnd && IsWindow(reinterpret_cast<HWND>(m_hwnd))) {
            return true;
        }
#endif
        return m_find.isActive();
    }

    QStringList args; // files to open (double-clicked in Explorer), or command-line options

    // Apps that may open their window from another process (Thunderbird): after a few
    // seconds without a window of our own process, take a top-level window of this class
    // whose process runs our exe.
    QString windowClass;

    void launch()
    {
        if (!QFileInfo::exists(m_exe)) {
            m_status = m_name + " not found";
            emit changed();
            update();
            return;
        }
        m_proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("STUDIO_EMBEDDED", "1");
        m_proc->setProcessEnvironment(env);
        m_proc->setWorkingDirectory(QFileInfo(m_exe).absolutePath());
        connect(m_proc, &QProcess::finished, this, [this]() {
#ifdef Q_OS_WIN
            if (m_hwnd && IsWindow(reinterpret_cast<HWND>(m_hwnd))) {
                return; // the window belongs to another process of the app: it lives on
            }
#endif
            if (!m_hwnd && !windowClass.isEmpty() && m_started.elapsed() < 30000) {
                return; // the app handed over to another process: keep looking for its window
            }
            m_find.stop();
            dropWindow();
        });
        m_proc->start(m_exe, args);
        m_started.start();
        m_status = "starting " + m_name + "…";
        m_find.start();
        update();
    }

    // ask the app to close (it may ask about unsaved work); once per window unless asked again
    void closeApp(bool again = false)
    {
#ifdef Q_OS_WIN
        if (m_hwnd && (again || m_closeSent != m_hwnd)) {
            m_closeSent = m_hwnd;
            PostMessageW(reinterpret_cast<HWND>(m_hwnd), WM_CLOSE, 0, 0);
        }
#else
        Q_UNUSED(again);
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
    void dropWindow()
    {
        m_alive.stop();
        delete m_container;
        m_container = nullptr;
        m_hwnd = 0;
        m_candidate = 0;
        m_status = m_name + " closed";
        emit changed();
        update();
    }

    void lookForWindow()
    {
#ifdef Q_OS_WIN
        const bool running = m_proc && m_proc->state() == QProcess::Running;
        if (!running && windowClass.isEmpty()) {
            return;
        }
        if (!running && m_started.elapsed() > 30000) {
            m_find.stop();
            dropWindow();
            return;
        }
        struct Find {
            DWORD pid;
            const wchar_t* cls;  // non-null: match by window class + process image instead of pid
            const wchar_t* exe;
            HWND best;
            long area;
        };
        auto search = [](Find& f) {
            EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                auto* f = reinterpret_cast<Find*>(lp);
                if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) {
                    return TRUE;
                }
                if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
                    return TRUE;
                }
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (f->cls) {
                    wchar_t cls[256] = {};
                    GetClassNameW(h, cls, 256);
                    if (_wcsicmp(cls, f->cls) != 0) {
                        return TRUE;
                    }
                    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (!ph) {
                        return TRUE;
                    }
                    wchar_t img[MAX_PATH * 2] = {};
                    DWORD n = MAX_PATH * 2;
                    const BOOL ok = QueryFullProcessImageNameW(ph, 0, img, &n);
                    CloseHandle(ph);
                    if (!ok || _wcsicmp(img, f->exe) != 0) {
                        return TRUE; // another copy of the app (not the one in Studio)
                    }
                } else if (pid != f->pid) {
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
        };

        HWND best = nullptr;
        if (running) {
            Find f { DWORD(m_proc->processId()), nullptr, nullptr, nullptr, 0 };
            search(f);
            best = f.best;
        }
        if (!best && !windowClass.isEmpty() && m_started.elapsed() > 4000) {
            const std::wstring cls = windowClass.toStdWString();
            const std::wstring exe = QDir::toNativeSeparators(QFileInfo(m_exe).absoluteFilePath()).toStdWString();
            Find f { 0, cls.c_str(), exe.c_str(), nullptr, 0 };
            search(f);
            best = f.best;
        }

        // the main window is the big one that stays (splash screens are smaller and go away)
        if (!best) {
            m_candidate = 0;
            return;
        }
        if (WId(best) != m_candidate) {
            m_candidate = WId(best);
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
        m_alive.start();
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
    WId m_closeSent = 0;
    QElapsedTimer m_started;
    QElapsedTimer m_candidateSince;
    QTimer m_find;
    QTimer m_alive;
};

// ------------------------------------------------------------------ frameless window, resizable from its edges
class Frameless : public QWidget
{
public:
    Frameless()
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), ALU_BOTTOM);
    }

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
};

// ------------------------------------------------------------------ a page popped out into its own window
class FloatStrip : public QWidget
{
    Q_OBJECT
public:
    FloatStrip(const QString& name, QWidget* parent)
        : QWidget(parent), m_name(name)
    {
        setFixedHeight(30);
        setMouseTracking(true);
    }

signals:
    void dock();
    void maximize();

protected:
    QRectF dockRect() const { return QRectF(width() - 92, 4, 84, 22); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient bg(0, 0, 0, height());
        bg.setColorAt(0, ALU_TOP);
        bg.setColorAt(1, ALU_BOTTOM);
        p.fillRect(rect(), bg);
        p.setPen(QColor("#A9AAAE"));
        p.drawLine(0, height() - 1, width(), height() - 1);
        QFont jost("Jost");
        jost.setPixelSize(15);
        p.setFont(jost);
        p.setPen(INK);
        p.drawText(QPointF(14, 20), "studio");
        p.setPen(ORANGE);
        const double x = 14 + QFontMetrics(jost).horizontalAdvance("studio");
        p.drawText(QPointF(x, 20), ".");
        p.setPen(QColor("#4A4A4F"));
        p.drawText(QPointF(x + 14, 20), m_name);
        p.setPen(Qt::NoPen);
        p.setBrush(m_hover ? ORANGE : QColor("#E3E3E5"));
        p.drawRoundedRect(dockRect(), 5, 5);
        QFont f("Consolas");
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(m_hover ? QColor("#FFFFFF") : INK);
        p.drawText(dockRect(), Qt::AlignCenter, "↙ dock");
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (dockRect().contains(e->position())) {
            emit dock();
        } else if (e->button() == Qt::LeftButton && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (!dockRect().contains(e->position())) {
            emit maximize();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const bool h = dockRect().contains(e->position());
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }

    void leaveEvent(QEvent*) override
    {
        m_hover = false;
        update();
    }

private:
    QString m_name;
    bool m_hover = false;
};

class FloatWindow : public Frameless
{
    Q_OBJECT
public:
    FloatWindow(const QString& name, QWidget* slot)
    {
        setWindowTitle("Studio · " + name);
        setMinimumSize(700, 400);
        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(4, 0, 4, 4);
        l->setSpacing(4);
        auto* strip = new FloatStrip(name, this);
        l->addWidget(strip);
        slot->setParent(this);
        l->addWidget(slot, 1);
        slot->show();
        connect(strip, &FloatStrip::dock, this, &FloatWindow::dock);
        connect(strip, &FloatStrip::maximize, this, [this]() { isMaximized() ? showNormal() : showMaximized(); });
    }

signals:
    void dock();

protected:
    void closeEvent(QCloseEvent* e) override
    {
        // closing the popped-out window puts the page back in Studio (the app keeps running)
        e->ignore();
        emit dock();
    }
};

// ------------------------------------------------------------------ the window
class StudioWindow : public Frameless
{
    Q_OBJECT
public:
    StudioWindow()
        : m_link("studio")
    {
        setWindowTitle("Studio");
        setMinimumSize(1040, 600);

        // Studio/Score/bin/StudioHost.exe -> Studio/Score/bin/<score>.exe, Studio/Tape/bin/Audacity4.exe
        // and Studio/Mail/thunderbird.exe
        const QDir bin(QCoreApplication::applicationDirPath());
        QString scoreExe;
        for (const QString& n : bin.entryList({ "LaurenceWhoScore*.exe", "MuseScore*.exe" }, QDir::Files)) {
            scoreExe = bin.filePath(n);
        }
        const QString tapeExe = QDir::cleanPath(bin.filePath("../../Tape/bin/Audacity4.exe"));
        const QString mailExe = QDir::cleanPath(bin.filePath("../../Mail/thunderbird.exe"));

        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(6, 0, 6, 6);
        l->setSpacing(6);
        m_top = new TopBar(this);
        l->addWidget(m_top);
        m_split = new QSplitter(Qt::Vertical, this);
        m_split->setHandleWidth(8);
        m_split->setStyleSheet("QSplitter::handle { background: #C9CACD; } QSplitter::handle:hover { background: #FF5A1F; }");
        m_slots["score"] = new AppSlot("Score", scoreExe, m_split);
        m_slots["tape"] = new AppSlot("Tape", tapeExe, m_split);
        m_slots["mail"] = new AppSlot("Mail", mailExe, m_split);
        for (const QString& pg : PAGES) {
            m_split->addWidget(m_slots[pg]);
            connect(m_slots[pg], &AppSlot::changed, this, &StudioWindow::refresh);
        }
        m_split->setStretchFactor(0, 3);
        m_split->setStretchFactor(1, 2);
        m_split->setStretchFactor(2, 3);
        l->addWidget(m_split, 1);

        // Thunderbird: its own profile, Studio's look, and it may open its window from another process
        m_slots["mail"]->windowClass = "MozillaWindowClass";
        m_slots["mail"]->args = { "-profile", QDir::toNativeSeparators(prepareMailProfile(QFileInfo(mailExe).absolutePath())), "-no-remote" };

        connect(m_top, &TopBar::pressed, this, &StudioWindow::onKey);
        connect(m_top, &TopBar::detach, this, [this](TopBar::Key k, QPoint at) {
            popOut(TopBar::pageOf(k), at);
        });

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
            } else if (cmd == "take") {
                setPage("score"); // the take's notes open in Score
            }
            m_top->update();
        });
        connect(&m_link, &studiolink::Peer::linkedChanged, this, [this](bool on) {
            m_top->linked = on;
            m_top->update();
        });
        m_link.start();

        auto* sw = new QShortcut(QKeySequence("Ctrl+Tab"), this);
        sw->setContext(Qt::ApplicationShortcut);
        connect(sw, &QShortcut::activated, this, [this]() {
            // score -> tape -> mail -> score (from "both", on to mail)
            const QString cur = m_top->page == "split" ? "tape" : m_top->page;
            setPage(PAGES.at((PAGES.indexOf(cur) + 1) % PAGES.size()));
        });

        // files passed to Studio (e.g. a double-clicked score): scores to Score, Tape projects to Tape
        bool tapeFile = false;
        for (const QString& a : QCoreApplication::arguments().mid(1)) {
            if (!QFileInfo::exists(a)) {
                continue;
            }
            if (a.endsWith(".aup4", Qt::CaseInsensitive) || a.endsWith(".aup3", Qt::CaseInsensitive)) {
                m_slots["tape"]->args << a;
                tapeFile = true;
            } else {
                m_slots["score"]->args << a;
            }
        }

        // closing: wait for the apps to finish closing (they may ask about unsaved work)
        m_closeWait.setInterval(250);
        connect(&m_closeWait, &QTimer::timeout, this, [this]() {
            bool running = false;
            for (AppSlot* s : std::as_const(m_slots)) {
                if (s->isRunning()) {
                    s->closeApp(); // also the ones whose window showed up meanwhile
                    running = true;
                }
            }
            if (!running) {
                m_closeWait.stop();
                m_quit = true;
                close();
            } else if (m_closingSince.elapsed() > 60000) {
                // an app said no (e.g. "cancel" on save): Studio stays
                m_closeWait.stop();
                m_closing = false;
                refresh();
            }
        });

        setPage(tapeFile ? "tape" : "score");
        resize(1440, 900);
        QTimer::singleShot(100, m_slots["score"], &AppSlot::launch);
        QTimer::singleShot(400, m_slots["tape"], &AppSlot::launch);
        QTimer::singleShot(700, m_slots["mail"], &AppSlot::launch);
    }

protected:
    void closeEvent(QCloseEvent* e) override
    {
        if (!m_quit) {
            bool running = false;
            for (AppSlot* s : std::as_const(m_slots)) {
                if (s->isRunning()) {
                    s->closeApp(m_closing); // pressing close again asks again
                    running = true;
                }
            }
            if (running) {
                // let each app ask about unsaved work and close gracefully, then go
                m_closing = true;
                m_closingSince.start();
                m_closeWait.start();
                m_top->note = "closing…";
                m_top->update();
                e->ignore();
                return;
            }
        }
        for (FloatWindow* f : std::as_const(m_floats)) {
            if (f) {
                f->hide();
            }
        }
        e->accept();
    }

private:
    // Mail's profile: %APPDATA%/LaurenceWho/Studio/MailProfile. Studio's own files (user.js,
    // chrome/userChrome.css, chrome/fonts) are copied over it on every start; Thunderbird's
    // own settings (prefs.js, accounts, mail) are never touched.
    static QString prepareMailProfile(const QString& mailDir)
    {
        const QString profile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/MailProfile";
        QDir().mkpath(profile);
        const QString tpl = mailDir + "/studio-profile";
        QDirIterator it(tpl, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString src = it.next();
            const QString rel = QDir(tpl).relativeFilePath(src);
            if (rel.compare("prefs.js", Qt::CaseInsensitive) == 0) {
                continue;
            }
            const QString dst = profile + "/" + rel;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (QFile::exists(dst)) {
                QFile::setPermissions(dst, QFile::permissions(dst) | QFileDevice::WriteOwner);
                QFile::remove(dst);
            }
            QFile::copy(src, dst);
            QFile::setPermissions(dst, QFile::permissions(dst) | QFileDevice::WriteOwner); // copies keep read-only flags
        }
        return profile;
    }

    void refresh()
    {
        m_top->up.clear();
        m_top->note.clear();
        for (const QString& pg : PAGES) {
            if (m_slots[pg]->isUp()) {
                m_top->up.insert(pg);
            }
            if (m_top->note.isEmpty()) {
                m_top->note = m_slots[pg]->status();
            }
        }
        if (m_closing) {
            m_top->note = "closing…";
        }
        m_top->update();
    }

    FloatWindow*& floatOf(const QString& page) { return m_floats[page]; }
    AppSlot* slotOf(const QString& page) const { return m_slots.value(page); }

    bool docked(const QString& page) const { return !m_floats.value(page); }

    void setPage(QString page)
    {
        if (page != "split" && !PAGES.contains(page)) {
            return;
        }
        // a popped-out page lives in its own window: its key brings that window to the front
        if (page != "split" && floatOf(page)) {
            FloatWindow* f = floatOf(page);
            f->showNormal();
            f->raise();
            f->activateWindow();
            m_link.send(QJsonObject { { "cmd", "page" }, { "page", page } });
            return;
        }
        if (page == "split" && (!docked("score") || !docked("tape"))) {
            // one of the two is in its own window: show the other one here, bring that window up
            for (const QString& pg : { QString("score"), QString("tape") }) {
                if (!docked(pg)) {
                    floatOf(pg)->raise();
                }
            }
            if (docked("score")) {
                setPage("score");
            } else if (docked("tape")) {
                setPage("tape");
            }
            return;
        }
        if (m_top->page != page) {
            m_link.send(QJsonObject { { "cmd", "page" }, { "page", page } }); // Score refreshes Tape's tracks on the way in
        }
        m_top->page = page;
        for (const QString& pg : PAGES) {
            if (docked(pg)) {
                m_slots[pg]->setVisible(page == pg || (page == "split" && pg != "mail"));
            }
        }
        m_top->update();
    }

    void syncOut()
    {
        m_top->out.clear();
        for (const QString& pg : PAGES) {
            if (!docked(pg)) {
                m_top->out.insert(pg);
            }
        }
    }

    // take a page out of Studio into its own window (drag it to another screen)
    void popOut(const QString& page, QPoint at)
    {
        if (!PAGES.contains(page)) {
            return;
        }
        if (floatOf(page)) {
            setPage(page);
            return;
        }
        QStringList stay;
        for (const QString& pg : PAGES) {
            if (pg != page && docked(pg)) {
                stay << pg;
            }
        }
        if (stay.isEmpty()) {
            return; // one page always stays in Studio
        }
        AppSlot* slot = slotOf(page);
        auto* f = new FloatWindow(page, slot);
        floatOf(page) = f;
        connect(f, &FloatWindow::dock, this, [this, page]() { dockBack(page); });
        const QSize sz(std::max(900, width() * 3 / 4), std::max(600, height() * 3 / 4));
        f->resize(sz);
        f->move(at - QPoint(sz.width() / 2, 15));
        f->show();
        syncOut();
        // stay on the page that was showing if it's still here, otherwise the first one left
        const QString was = m_top->page;
        m_top->page = "";
        if (was == "split" && docked("score") && docked("tape")) {
            setPage("split");
        } else {
            setPage(stay.contains(was) ? was : stay.first());
        }
    }

    void dockBack(const QString& page)
    {
        FloatWindow*& f = floatOf(page);
        if (!f) {
            return;
        }
        AppSlot* slot = slotOf(page);
        f->deleteLater();
        f = nullptr;
        // back into its place among the pages that are in Studio
        int index = 0;
        for (const QString& pg : PAGES) {
            if (pg == page) {
                break;
            }
            if (docked(pg)) {
                ++index;
            }
        }
        m_split->insertWidget(index, slot);
        syncOut();
        m_top->page = "";
        setPage(page);
        raise();
        activateWindow();
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
        case TopBar::PageScore:
        case TopBar::PageTape:
        case TopBar::PageMail: setPage(TopBar::pageOf(k));
            break;
        case TopBar::PageSplit: setPage("split");
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
    QMap<QString, AppSlot*> m_slots;
    QMap<QString, FloatWindow*> m_floats;
    QTimer m_closeWait;
    QElapsedTimer m_closingSince;
    bool m_closing = false;
    bool m_quit = false;
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
