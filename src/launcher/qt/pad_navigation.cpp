// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QKeyEvent>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabBar>
#include <QWidget>

#include "launcher/pad_commands.h"
#include "launcher/qt/pad_navigation.h"

namespace Gr2Port::Launcher {

namespace {

using PadCommands::Command;

// Milliseconds between looks at the pads.
constexpr int PollMs = 16;
// The most widgets a search of a window's focus chain goes through.
constexpr int MaxChain = 10000;
// The frame around the focused widget: its width and corner radius, and its margin around the
// widget, in pixels.
constexpr qreal MarkWidth = 2.0;
constexpr qreal MarkRadius = 3.0;
constexpr int MarkMargin = 3;

// A frame in the highlight colour around the widget the pads have focused, as a game's menu marks
// its chosen row. The style's own focus mark is too faint to follow from a distance.
class FocusMark : public QWidget {
public:
    explicit FocusMark(QWidget* window) : QWidget{window} {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen{palette().color(QPalette::Highlight), MarkWidth});
        const qreal inset = MarkWidth / 2.0;
        painter.drawRoundedRect(QRectF{rect()}.adjusted(inset, inset, -inset, -inset), MarkRadius,
                                MarkRadius);
    }
};

// The dialogs the pads have moved in. A dialog's first direction goes to its first widget; one the
// pads come back to, as a dialog over it closes, keeps its focus.
QList<QPointer<QWidget>> g_touched;
// The mark of the focused widget, which follows the focus once the pads have moved in a dialog.
QPointer<QWidget> g_mark;
bool g_marking = false;

// Marks the focused widget of a dialog; other windows have no mark.
void Mark(QWidget* focus) {
    QWidget* const window = focus->window();
    if (qobject_cast<QDialog*>(window) == nullptr) {
        if (g_mark != nullptr) {
            g_mark->hide();
        }
        return;
    }
    if (g_mark == nullptr || g_mark->parentWidget() != window) {
        delete g_mark.data();
        g_mark = new FocusMark{window};
    }
    const QRect area{focus->mapTo(window, QPoint{0, 0}), focus->size()};
    g_mark->setGeometry(area.adjusted(-MarkMargin, -MarkMargin, MarkMargin, MarkMargin));
    g_mark->raise();
    g_mark->show();
    g_mark->update();
}

// Up and down stop at the widgets that Tab focuses, but neither at a tab bar, whose tabs the
// shoulder buttons change, nor at a scroll area that holds a page.
bool Stops(QWidget* widget, QWidget* window) {
    return widget->window() == window && widget->isVisible() && widget->isEnabled() &&
           (widget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus &&
           widget->focusProxy() == nullptr && qobject_cast<QTabBar*>(widget) == nullptr &&
           qobject_cast<QScrollArea*>(widget) == nullptr;
}

// A key press for a widget. Qt gives a timer no event while its own handler runs, so a dialog
// that a command opens runs from the event loop instead, where the pads still move it.
void Press(QWidget* target, int key) {
    QApplication::postEvent(target, new QKeyEvent{QEvent::KeyPress, key, Qt::NoModifier});
    QApplication::postEvent(target, new QKeyEvent{QEvent::KeyRelease, key, Qt::NoModifier});
}

// Scrolls the widget into sight, then focuses the next or previous widget that up and down stop
// at in the focus chain after from.
void MoveFocus(QWidget* from, bool next) {
    QWidget* const window = from->window();
    QWidget* candidate = from;
    for (int i = 0; i < MaxChain; ++i) {
        candidate = next ? candidate->nextInFocusChain() : candidate->previousInFocusChain();
        if (candidate == nullptr || candidate == from) {
            return;
        }
        if (Stops(candidate, window)) {
            for (QWidget* parent = candidate->parentWidget(); parent != nullptr;
                 parent = parent->parentWidget()) {
                if (auto* const area = qobject_cast<QScrollArea*>(parent)) {
                    area->ensureWidgetVisible(candidate);
                }
            }
            candidate->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
            return;
        }
    }
}

// The window's tab bar shows its previous or next tab, around the ends, and the tab's first
// widget takes the focus.
void ChangeTab(QWidget* window, bool back) {
    for (QTabBar* const tab : window->findChildren<QTabBar*>()) {
        if (!tab->isVisible() || tab->count() <= 0) {
            continue;
        }
        const int count = tab->count();
        tab->setCurrentIndex((tab->currentIndex() + (back ? count - 1 : 1)) % count);
        MoveFocus(tab, true);
        return;
    }
}

// The launcher's menu takes the arrows and Enter.
void InMenu(QWidget* focus, Command command) {
    switch (command) {
    case Command::Up:
        Press(focus, Qt::Key_Up);
        break;
    case Command::Down:
        Press(focus, Qt::Key_Down);
        break;
    case Command::Accept:
        Press(focus, Qt::Key_Return);
        break;
    default:
        break;
    }
}

// An open combo box list moves its row, takes it with accept and closes without it with back.
void InPopup(QWidget* focus, Command command) {
    switch (command) {
    case Command::Up:
        Press(focus, Qt::Key_Up);
        break;
    case Command::Down:
        Press(focus, Qt::Key_Down);
        break;
    case Command::Accept:
        Press(focus, Qt::Key_Return);
        break;
    case Command::Back:
        Press(focus, Qt::Key_Escape);
        break;
    default:
        break;
    }
}

// A dialog takes a command by the widget that has the focus. Up and down move between the
// widgets, and left and right too, but change a combo box's choice, a slider, a tab bar's tab and
// a group's radio button. A list moves its row until its first or last. The first direction in a
// dialog goes to its first widget, whichever has the focus as it opens.
void InDialog(QWidget* window, QWidget* focus, Command command) {
    const bool horizontal = command == Command::Left || command == Command::Right;
    if (!g_marking) {
        g_marking = true;
        Mark(focus);
    }
    g_touched.removeIf([](const QPointer<QWidget>& touched) { return touched.isNull(); });
    if (!g_touched.contains(window)) {
        g_touched.append(window);
        if (horizontal || command == Command::Up || command == Command::Down) {
            MoveFocus(window, true);
            return;
        }
    }
    if (auto* const combo = qobject_cast<QComboBox*>(focus)) {
        if (horizontal) {
            Press(combo, command == Command::Left ? Qt::Key_Up : Qt::Key_Down);
            return;
        }
        if (command == Command::Accept) {
            QTimer::singleShot(0, combo, [combo] { combo->showPopup(); });
            return;
        }
    } else if (qobject_cast<QAbstractSlider*>(focus) != nullptr ||
               qobject_cast<QTabBar*>(focus) != nullptr ||
               qobject_cast<QRadioButton*>(focus) != nullptr) {
        if (horizontal) {
            Press(focus, command == Command::Left ? Qt::Key_Left : Qt::Key_Right);
            return;
        }
    } else if (auto* const view = qobject_cast<QAbstractItemView*>(focus)) {
        const QModelIndex current = view->currentIndex();
        const int row = current.isValid() ? current.row() : -1;
        const int rows = view->model() != nullptr ? view->model()->rowCount(view->rootIndex()) : 0;
        if (command == Command::Up && row > 0) {
            Press(view, Qt::Key_Up);
            return;
        }
        if (command == Command::Down && row + 1 < rows) {
            Press(view, Qt::Key_Down);
            return;
        }
        if (command == Command::Accept) {
            // The current row takes a double click, as the mouse gives it.
            if (current.isValid()) {
                QTimer::singleShot(0, view, [view, clicked = QPersistentModelIndex{current}] {
                    if (clicked.isValid()) {
                        Q_EMIT view->doubleClicked(clicked);
                    }
                });
            }
            return;
        }
    } else if (auto* const button = qobject_cast<QAbstractButton*>(focus)) {
        if (command == Command::Accept) {
            QTimer::singleShot(0, button, [button] { button->click(); });
            return;
        }
    }
    switch (command) {
    case Command::Up:
    case Command::Left:
        MoveFocus(focus, false);
        break;
    case Command::Down:
    case Command::Right:
        MoveFocus(focus, true);
        break;
    case Command::Back:
        Press(focus, Qt::Key_Escape);
        break;
    case Command::PreviousTab:
    case Command::NextTab:
        ChangeTab(window, command == Command::PreviousTab);
        break;
    case Command::Accept:
        break;
    }
}

// A command goes to the application's active window; with another program's window active, the
// pads move nothing.
void Apply(Command command) {
    QWidget* const window = QApplication::activeWindow();
    if (window == nullptr) {
        return;
    }
    if (QWidget* const popup = QApplication::activePopupWidget()) {
        InPopup(popup->focusWidget() != nullptr ? popup->focusWidget() : popup, command);
        return;
    }
    QWidget* const focus =
        QApplication::focusWidget() != nullptr ? QApplication::focusWidget() : window;
    if (qobject_cast<QDialog*>(window) == nullptr) {
        InMenu(focus, command);
        return;
    }
    InDialog(window, focus, command);
}

} // namespace

PadNavigation::PadNavigation() {
    PadCommands::Start();
    QObject::connect(qApp, &QApplication::focusChanged, &timer, [](QWidget*, QWidget* now) {
        if (g_marking && now != nullptr) {
            Mark(now);
        }
    });
    QObject::connect(&timer, &QTimer::timeout, [] {
        if (const std::optional<Command> command = PadCommands::Next()) {
            Apply(*command);
        }
    });
    timer.start(PollMs);
}

PadNavigation::~PadNavigation() {
    timer.stop();
    PadCommands::Stop();
    g_marking = false;
    g_touched.clear();
}

} // namespace Gr2Port::Launcher
