#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPixmap>

class ImageView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void resetView();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void fitImageInView();

private:
    QGraphicsScene* m_scene {nullptr};
    QGraphicsPixmapItem* m_item {nullptr};
    bool m_panning {false};
    QPoint m_lastMousePos;
    bool m_hasImage {false};
    double m_zoomFactor {1.0};
    double m_fitZoomFactor {1.0};
    QPixmap m_cachedPixmap;  // ⭐ 缓存 QPixmap，减少重复创建开销
};

#endif
