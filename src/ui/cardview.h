#ifndef UI_CARDVIEW_H
#define UI_CARDVIEW_H

#include <QWidget>
#include <QImage>
#include "../stack.h"

/* Renders one card of a loaded Stack: composited background+card bitmap
 * (nearest-neighbour scaled to the current zoom) plus field text drawn on
 * top at true scaled resolution. */
class CardView : public QWidget {
    Q_OBJECT
public:
    explicit CardView(QWidget *parent = nullptr);

    void setStack(const Stack *stack);
    void setCardIndex(uint32_t idx);
    void setZoom(int zoom);
    int  zoom() const { return zoom_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage buildCardImage() const;
    void   drawParts(QImage &img, const Part *parts, uint16_t count) const;
    void   drawFields(QPainter &painter) const;
    void   drawOneField(QPainter &painter, const Part *part, const char *text) const;

    const Stack *stack_ = nullptr;
    uint32_t     cardIdx_ = 0;
    int          zoom_ = 2;
};

#endif /* UI_CARDVIEW_H */
