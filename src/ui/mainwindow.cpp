#include "mainwindow.h"
#include "cardview.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QKeyEvent>
#include <QByteArray>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    view_ = new CardView(this);
    setCentralWidget(view_);
    buildMenus();
    setWindowTitle("StackWorks II Pro");
}

MainWindow::~MainWindow() {
    if (stack_) stack_free(stack_);
}

void MainWindow::buildMenus() {
    QMenu *bananaMenu = menuBar()->addMenu(QString::fromUtf8("\xF0\x9F\x8D\x8C BaNaNa"));
    bananaMenu->addAction("About StackWorks II Pro", this, &MainWindow::showAbout);

    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open Stack...", QKeySequence::Open, this, &MainWindow::openStack);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit StackWorks", QKeySequence::Quit, this, &QWidget::close);

    QMenu *goMenu = menuBar()->addMenu("&Go");
    goMenu->addAction("Previous", this, &MainWindow::goPrev);
    goMenu->addAction("Next", this, &MainWindow::goNext);
    goMenu->addSeparator();
    goMenu->addAction("Home", this, &MainWindow::goHome);

    QMenu *viewMenu = menuBar()->addMenu("&View");
    for (int z = 1; z <= 5; z++) {
        QAction *act = viewMenu->addAction(QString::fromUtf8("Zoom %1\xC3\x97").arg(z));
        connect(act, &QAction::triggered, this, [this, z] { zoomTo(z); });
    }
}

bool MainWindow::loadStack(const QString &path) {
    QByteArray pathBytes = path.toLocal8Bit();
    Stack *newStack = stack_load(pathBytes.constData());
    if (!newStack) {
        QMessageBox::warning(this, "StackWorks II Pro", "Failed to load stack:\n" + path);
        return false;
    }
    if (newStack->card_count == 0) {
        stack_free(newStack);
        QMessageBox::warning(this, "StackWorks II Pro", "No cards found in stack:\n" + path);
        return false;
    }

    if (stack_) stack_free(stack_);
    stack_ = newStack;
    cur_ = 0;
    filename_ = QFileInfo(path).fileName();

    view_->setStack(stack_);
    view_->setCardIndex(cur_);
    applyZoom();
    updateTitle();
    return true;
}

void MainWindow::applyZoom() {
    view_->setZoom(zoom_);
    view_->setFixedSize(view_->sizeHint());
    adjustSize();
}

void MainWindow::updateTitle() {
    if (!stack_) { setWindowTitle("StackWorks II Pro"); return; }
    setWindowTitle(QString("StackWorks II Pro - %1, card %2 of %3 (%4)")
                       .arg(stack_->name)
                       .arg(cur_ + 1)
                       .arg(stack_->card_count)
                       .arg(filename_));
}

void MainWindow::goPrev() {
    if (!stack_ || cur_ == 0) return;
    cur_--;
    view_->setCardIndex(cur_);
    updateTitle();
}

void MainWindow::goNext() {
    if (!stack_ || cur_ + 1 >= stack_->card_count) return;
    cur_++;
    view_->setCardIndex(cur_);
    updateTitle();
}

void MainWindow::goHome() {
    if (!stack_) return;
    cur_ = 0;
    view_->setCardIndex(cur_);
    updateTitle();
}

void MainWindow::goEnd() {
    if (!stack_) return;
    cur_ = stack_->card_count - 1;
    view_->setCardIndex(cur_);
    updateTitle();
}

void MainWindow::zoomTo(int level) {
    zoom_ = level;
    applyZoom();
}

void MainWindow::openStack() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Stack", QString(), "HyperCard Stacks (*.hc);;All Files (*)");
    if (!path.isEmpty()) loadStack(path);
}

void MainWindow::showAbout() {
    QMessageBox::information(this, "StackWorks II Pro",
        QString::fromUtf8("StackWorks II Pro\nHyperCard stack viewer\n"
                           "A work in progress.\n\n\xF0\x9F\x8D\x8C"));
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    /* HyperCard's Command-Option "show all buttons" reveal (Ctrl+Alt here,
     * since Linux has no Command key). */
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt) {
        view_->setShowAllButtons(e->modifiers().testFlag(Qt::ControlModifier) &&
                                  e->modifiers().testFlag(Qt::AltModifier));
    }

    switch (e->key()) {
    case Qt::Key_Right: case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter:
        goNext(); break;
    case Qt::Key_Left: case Qt::Key_Backspace:
        goPrev(); break;
    case Qt::Key_Home:
        goHome(); break;
    case Qt::Key_End:
        goEnd(); break;
    case Qt::Key_Escape: case Qt::Key_Q:
        close(); break;
    default:
        QMainWindow::keyPressEvent(e);
        return;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt)
        view_->setShowAllButtons(false);
    QMainWindow::keyReleaseEvent(e);
}
