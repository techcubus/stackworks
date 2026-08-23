#include <cstdio>
#include <QApplication>
#include "stack.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[]) {
    int dump = 0;
    const char *path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'd') dump = 1;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-d] <stack.hc>\n", argv[0]);
        return 1;
    }

    if (dump) {
        Stack *s = stack_load(path);
        if (!s) return 1;
        printf("loaded: %u cards, %u backgrounds, %ux%u\n",
               s->card_count, s->bkgd_count, s->card_width, s->card_height);
        stack_dump(s, stdout);
        stack_free(s);
        return 0;
    }

    QApplication app(argc, argv);

    MainWindow window;
    if (!window.loadStack(QString::fromLocal8Bit(path)))
        return 1;
    window.show();

    return app.exec();
}
