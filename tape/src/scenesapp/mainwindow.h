/*
 * Scenes (Laurence Who Studio)
 * The window: TP-7 look — aluminium body, flat light keys, one orange accent,
 * a black display with the scene you are on (big) and the next one (small).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "audioengine.h"
#include "scenesformat.h"

class QComboBox;
class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QThread;
class QVBoxLayout;

namespace scenes {
class MidiIn;
class MainWindow;

struct Theme {
    static QString mono();   // Consolas / IBM Plex Mono / any monospace
    static QString title();  // Jost
    static QString titleLight();
};

//! the black OLED display
class OledDisplay : public QWidget
{
public:
    explicit OledDisplay(MainWindow* w);
    void flash();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    MainWindow* m_w;
    qint64 m_flashAt = 0;
};

//! piano keys with the current scene's layers drawn over them
class KeyStrip : public QWidget
{
public:
    explicit KeyStrip(MainWindow* w);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    MainWindow* m_w;
};

//! one big scene key
class SceneKey : public QWidget
{
public:
    SceneKey(MainWindow* w, int index);
    QSize sizeHint() const override { return QSize(170, 92); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    MainWindow* m_w;
    int m_index;
    bool m_down = false;
};

//! full screen: huge current scene, next one under it
class PerformView : public QWidget
{
public:
    explicit PerformView(MainWindow* w);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    MainWindow* m_w;
};

class EditDialog : public QDialog
{
public:
    EditDialog(MainWindow* w, int index);
private:
    void rebuild();
    void apply();
    MainWindow* m_w;
    int m_index;
    Scene m_scene;
    QLineEdit* m_name = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QWidget* m_rowsHost = nullptr;
};

class SetupDialog : public QDialog
{
public:
    explicit SetupDialog(MainWindow* w);
private:
    void fillDevices();
    void refresh();
    MainWindow* m_w;
    QComboBox* m_device = nullptr;
    QComboBox* m_buffer = nullptr;
    QComboBox* m_rate = nullptr;
    QCheckBox* m_exclusive = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_midi = nullptr;
    QLabel* m_learn = nullptr;
    QTimer m_timer;
};

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    void openSet(const QString& path);

    // used by the parts of the window
    const SceneSet& set() const { return m_set; }
    int current() const { return m_set.cur; }
    AudioEngine& audio() { return *m_audio; }
    const std::vector<SoundRef>& library() const { return m_library; }
    bool libraryReady() const { return m_libraryReady; }
    QStringList midiDevices() const;
    bool midiActive() const;
    QString learnedText() const;
    bool isSceneReady(int i) const;

    void go(int index);
    void next() { go(m_set.cur + 1); }
    void prev() { go(m_set.cur - 1); }
    void panic();
    void editScene(int index);
    void setScene(int index, const Scene& scene); // from the editor
    void addScene();
    void duplicateScene(int index);
    void moveScene(int index, int delta);
    void deleteScene(int index);
    void togglePerform();
    QWidget* performView() const { return m_perform; }
    void openSetup();
    void learnSwitch(bool nextKey);
    //! next two keys pressed on the MIDI keyboard set a range (editor)
    void learnRange(std::function<void (int lo, int hi)> done);
    void restartAudio(const AudioSettings& s);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    friend class SetupDialog;
    void onMidi(unsigned char status, unsigned char d1, unsigned char d2); // MIDI thread
    void onLearned(int type, int d1);
    void onRangeKey(int note);
    void buildUi();
    void rebuildKeys();
    void layoutKeys();
    void refresh(bool flash = false);
    void changed();      // the set was edited: save soon, reload sounds
    void save();
    void scanLibrary();
    void applyResolved();
    void setMenu();
    void storeLearned();
    bool handleSwitchKey(int key);

    SceneSet m_set;
    QString m_path;
    std::vector<SoundRef> m_library;
    bool m_libraryReady = false;

    AudioEngine* m_audio = nullptr;
    MidiIn* m_midi = nullptr;
    std::atomic<int> m_learnMode { 0 };   // 0 none, 1 next, 2 previous, 3 range
    std::atomic<int> m_learnNext { 0 };   // type << 8 | d1
    std::atomic<int> m_learnPrev { 0 };
    std::atomic<qint64> m_lastMidi { 0 };
    std::function<void (int, int)> m_rangeDone;
    int m_rangeFirst = -1;

    OledDisplay* m_display = nullptr;
    KeyStrip* m_strip = nullptr;
    QLabel* m_midiLabel = nullptr;
    QLabel* m_audioLabel = nullptr;
    QWidget* m_midiDot = nullptr;
    QPushButton* m_setButton = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_keysHost = nullptr;
    QGridLayout* m_grid = nullptr;
    std::vector<SceneKey*> m_keys;
    QSlider* m_volume = nullptr;
    QPointer<PerformView> m_perform;
    QPointer<QThread> m_scan;
    QTimer m_saveTimer;
    QTimer m_frame;   // 30 fps: keys held, level, MIDI light
};
}
