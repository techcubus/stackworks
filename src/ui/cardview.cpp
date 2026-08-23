#include "cardview.h"

#include <QPainter>
#include <cstring>

CardView::CardView(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

void CardView::setStack(const Stack *stack) {
    stack_ = stack;
    updateGeometry();
    update();
}

void CardView::setCardIndex(uint32_t idx) {
    cardIdx_ = idx;
    update();
}

void CardView::setZoom(int zoom) {
    if (zoom < 1) zoom = 1;
    if (zoom > 5) zoom = 5;
    zoom_ = zoom;
    updateGeometry();
    update();
}

QSize CardView::sizeHint() const {
    if (!stack_) return QSize(512, 342);
    return QSize(stack_->card_width * zoom_, stack_->card_height * zoom_);
}

/* Blit a 1-bit bitmap into an ARGB32 image.
 * xor=false: black pixels write opaque black; white pixels are untouched.
 * xor=true: black pixels invert the destination (card layer over background). */
static void blit1Bit(QImage &img, const uint8_t *bitmap, bool doXor) {
    if (!bitmap) return;
    uint16_t w = (uint16_t)img.width();
    uint16_t h = (uint16_t)img.height();
    uint16_t row_bytes = (w + 7) / 8;
    for (uint16_t y = 0; y < h; y++) {
        auto *px = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (uint16_t x = 0; x < w; x++) {
            int bit = (bitmap[y * row_bytes + x / 8] >> (7 - (x & 7))) & 1;
            if (!bit) continue;
            px[x] = doXor ? (px[x] ^ 0x00FFFFFFu) : 0xFF000000u;
        }
    }
}

void CardView::drawParts(QImage &img, const Part *parts, uint16_t count) const {
    int W = img.width(), H = img.height();
    for (uint16_t i = 0; i < count; i++) {
        const Part &p = parts[i];
        if (!p.visible) continue;
        /* Rectangle/shadow buttons get real chrome drawn in drawButtons();
         * skip the debug outline for those so it doesn't double up. Other
         * (unconfirmed) button styles keep the blue debug box for now. */
        if (p.type == PART_BUTTON &&
            (p.style == BTN_STYLE_RECTANGLE || p.style == BTN_STYLE_SHADOW))
            continue;
        /* blue for buttons, green for fields */
        QRgb color = (p.type == PART_BUTTON) ? 0xFF0000CCu : 0xFF006600u;
        int t = p.rect.top, l = p.rect.left, b = p.rect.bottom, r = p.rect.right;
        for (int x = l; x < r; x++) {
            if (x < 0 || x >= W) continue;
            if (t >= 0 && t < H) reinterpret_cast<QRgb *>(img.scanLine(t))[x] = color;
            if (b - 1 >= 0 && b - 1 < H) reinterpret_cast<QRgb *>(img.scanLine(b - 1))[x] = color;
        }
        for (int y = t; y < b; y++) {
            if (y < 0 || y >= H) continue;
            auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            if (l >= 0 && l < W) row[l] = color;
            if (r - 1 >= 0 && r - 1 < W) row[r - 1] = color;
        }
    }
}

QImage CardView::buildCardImage() const {
    QImage img(stack_->card_width, stack_->card_height, QImage::Format_ARGB32);
    img.fill(0xFFFFFFFFu);

    const Card *card = &stack_->cards[cardIdx_];
    Background *bg = stack_find_bkgd(stack_, card->bkgd_id);
    if (bg) {
        blit1Bit(img, bg->bitmap, false);
        drawParts(img, bg->parts, bg->part_count);
    }
    blit1Bit(img, card->bitmap, true);
    drawParts(img, card->parts, card->part_count);
    return img;
}

void CardView::drawOneField(QPainter &painter, const Part *p, const char *text) const {
    int pt_size = (p->text_size > 0 ? (int)p->text_size : 12) * zoom_;

    QFont font = painter.font();
    font.setPointSize(pt_size);
    font.setBold(p->text_style & 0x01);
    font.setItalic(p->text_style & 0x02);
    font.setUnderline(p->text_style & 0x04);
    painter.setFont(font);

    int fx = p->rect.left * zoom_;
    int fy = p->rect.top * zoom_;
    int fw = (p->rect.right - p->rect.left) * zoom_;
    int fh = (p->rect.bottom - p->rect.top) * zoom_;
    if (fw <= 0 || fh <= 0) return;

    painter.setClipping(false);
    QRect fieldRect(fx, fy, fw, fh);

    /* shadow: filled black rect offset 2 card-pixels down-right */
    if (p->style == FLD_STYLE_SHADOW) {
        int sh = 2 * zoom_;
        painter.fillRect(QRect(fx + sh, fy + sh, fw, fh), Qt::black);
    }

    /* white fill for all non-transparent styles */
    if (p->style != FLD_STYLE_TRANSPARENT)
        painter.fillRect(fieldRect, Qt::white);

    /* border for rectangle, shadow, and scrolling; opaque has no border */
    if (p->style == FLD_STYLE_RECTANGLE ||
        p->style == FLD_STYLE_SHADOW    ||
        p->style == FLD_STYLE_SCROLLING) {
        painter.setPen(Qt::black);
        painter.drawRect(QRect(fieldRect.topLeft(), QSize(fw - 1, fh - 1)));
    }

    /* scroll bar: ~15 card-pixel wide column on the right */
    int scroll_w = 0;
    if (p->style == FLD_STYLE_SCROLLING) {
        scroll_w = 15 * zoom_;
        int sbx = fx + fw - scroll_w;
        painter.fillRect(QRect(sbx, fy, scroll_w, fh), QColor(192, 192, 192));
        painter.setPen(Qt::black);
        painter.drawLine(sbx, fy, sbx, fy + fh - 1);
    }

    int text_w = fw - scroll_w - 4;
    if (text_w < 1) text_w = 1;
    painter.setClipRect(QRect(fx + 2, fy + 2, text_w, fh - 4));

    QString qtext = QString::fromLatin1(text);
    qtext.replace(QChar(0x0D), QChar('\n'));
    if (!qtext.isEmpty()) {
        /* Outline style over a transparent field renders on the black bitmap;
         * Mac shows outline text as white-on-black, so use white here. */
        QColor textColor = Qt::black;
        if ((p->text_style & 0x08) && p->style == FLD_STYLE_TRANSPARENT)
            textColor = Qt::white;
        painter.setPen(textColor);
        painter.drawText(QRect(fx + 2, fy + 2, text_w, fh - 4),
                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, qtext);
    }
    painter.setClipping(false);
}

/* Only the rectangle and shadow button styles are confirmed (see BTN_STYLE_*
 * in stack.h); other/unrecognised styles fall back to the debug outline
 * drawn by drawParts() rather than guessing at their appearance. */
void CardView::drawOneButton(QPainter &painter, const Part *p) const {
    if (p->style != BTN_STYLE_RECTANGLE && p->style != BTN_STYLE_SHADOW) return;

    int fx = p->rect.left * zoom_;
    int fy = p->rect.top * zoom_;
    int fw = (p->rect.right - p->rect.left) * zoom_;
    int fh = (p->rect.bottom - p->rect.top) * zoom_;
    if (fw <= 0 || fh <= 0) return;

    painter.setClipping(false);
    QRect btnRect(fx, fy, fw, fh);

    /* No fill: button rects often sit over hand-drawn art in the card
     * bitmap (an invisible click zone laid over painted button art), so
     * painting an opaque background would erase it. Just draw the chrome. */
    painter.setPen(Qt::black);
    painter.drawRect(QRect(btnRect.topLeft(), QSize(fw - 1, fh - 1)));

    /* shadow: thin offset bars along the bottom and right edges, rather
     * than a filled rect, so the button's own interior is left untouched */
    if (p->style == BTN_STYLE_SHADOW) {
        int sh = 2 * zoom_;
        painter.fillRect(QRect(fx + sh, fy + fh, fw, sh), Qt::black);
        painter.fillRect(QRect(fx + fw, fy + sh, sh, fh), Qt::black);
    }

    if (p->name && *p->name) {
        int pt_size = (p->text_size > 0 ? (int)p->text_size : 12) * zoom_;
        QFont font = painter.font();
        font.setPointSize(pt_size);
        font.setBold(p->text_style & 0x01);
        font.setItalic(p->text_style & 0x02);
        font.setUnderline(p->text_style & 0x04);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(btnRect, Qt::AlignCenter, QString::fromLatin1(p->name));
    }
}

void CardView::drawButtons(QPainter &painter) const {
    const Card *card = &stack_->cards[cardIdx_];
    Background *bg = stack_find_bkgd(stack_, card->bkgd_id);

    if (bg) {
        for (uint16_t i = 0; i < bg->part_count; i++) {
            const Part *p = &bg->parts[i];
            if (p->type != PART_BUTTON || !p->visible) continue;
            drawOneButton(painter, p);
        }
    }
    for (uint16_t i = 0; i < card->part_count; i++) {
        const Part *p = &card->parts[i];
        if (p->type != PART_BUTTON || !p->visible) continue;
        drawOneButton(painter, p);
    }
}

void CardView::drawFields(QPainter &painter) const {
    const Card *card = &stack_->cards[cardIdx_];
    Background *bg = stack_find_bkgd(stack_, card->bkgd_id);

    /* background-layer fields: card content overrides bg content per part */
    if (bg) {
        for (uint16_t i = 0; i < bg->part_count; i++) {
            const Part *p = &bg->parts[i];
            if (p->type != PART_FIELD || !p->visible) continue;
            const char *text = card_field_text(card, p->id);
            if (!text || !*text) text = bkgd_field_text(bg, p->id);
            if (!text || !*text) continue;
            drawOneField(painter, p, text);
        }
    }

    /* card-layer fields */
    for (uint16_t i = 0; i < card->part_count; i++) {
        const Part *p = &card->parts[i];
        if (p->type != PART_FIELD || !p->visible) continue;
        const char *text = card_field_text(card, p->id);
        if (!text || !*text) continue;
        drawOneField(painter, p, text);
    }

    painter.setClipping(false);
    painter.setPen(Qt::black);
}

void CardView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    if (!stack_ || cardIdx_ >= stack_->card_count) {
        painter.fillRect(rect(), Qt::white);
        return;
    }

    QImage img = buildCardImage();
    QImage scaled = img.scaled(img.width() * zoom_, img.height() * zoom_,
                                Qt::IgnoreAspectRatio, Qt::FastTransformation);
    painter.drawImage(0, 0, scaled);

    painter.setRenderHint(QPainter::TextAntialiasing, true);
    drawFields(painter);
    drawButtons(painter);
}
