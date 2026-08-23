#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QMainWindow>
#include "../stack.h"

class CardView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadStack(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void goPrev();
    void goNext();
    void goHome();
    void goEnd();
    void zoomTo(int level);
    void openStack();
    void showAbout();

private:
    void buildMenus();
    void updateTitle();
    void applyZoom();

    Stack   *stack_ = nullptr;
    QString  filename_;
    uint32_t cur_ = 0;
    CardView *view_ = nullptr;
    int       zoom_ = 2;
};

#endif /* UI_MAINWINDOW_H */
