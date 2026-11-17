#pragma once
#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <cstring>
inline QWidget *nativeHomeButton() {
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return nullptr;
    QWidget *root = QApplication::activeWindow();
    if (!root || std::strcmp(root->metaObject()->className(), "QFrame")) return nullptr;
    QWidget *button = nullptr;
    bool title = false, description = false;
    for (QWidget *w : QApplication::allWidgets()) {
        if (w->window() != root || !w->isVisible()) continue;
        if (auto *b = qobject_cast<QToolButton *>(w)) {
            if (b->text().simplified() == "Start Playing") {
                if (button) return nullptr;
                button = b;
            }
        }
        if (auto *label = qobject_cast<QLabel *>(w)) {
            title |= label->text().simplified() == "Steam Link";
            description |= label->text().simplified() == "Stream games from your computer with Steam.";
        }
    }
    return title && description ? button : nullptr;
}
