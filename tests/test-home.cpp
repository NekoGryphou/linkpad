#include "home.hpp"
#include <QApplication>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QFrame root; QVBoxLayout layout(&root);
    QLabel title("Steam Link"), description("Stream games from your computer with Steam.");
    QToolButton button; button.setText("Start Playing");
    layout.addWidget(&title); layout.addWidget(&description); layout.addWidget(&button);
    root.show(); root.activateWindow(); app.processEvents();
    CHECK(nativeHomeButton() == &button);
    QDialog modal(&root); modal.setModal(true); modal.show(); app.processEvents();
    CHECK(!nativeHomeButton());
    modal.hide(); root.activateWindow(); app.processEvents();
    CHECK(nativeHomeButton() == &button);
    button.hide(); app.processEvents(); CHECK(!nativeHomeButton());
    button.show(); app.processEvents(); CHECK(nativeHomeButton() == &button);
    description.setText("Settings"); CHECK(!nativeHomeButton());
    description.setText("Stream games from your computer with Steam.");
    QFrame unknown; unknown.show(); unknown.activateWindow(); app.processEvents();
    CHECK(!nativeHomeButton());
    unknown.hide(); root.activateWindow(); app.processEvents();
    CHECK(nativeHomeButton() == &button);
    QToolButton duplicate(&root); duplicate.setText("Start Playing"); duplicate.show();
    app.processEvents(); CHECK(!nativeHomeButton());
    std::puts("PASS native home, Settings, modal, unknown and duplicate UI guard");
}
