#include "imageview.h"
#include "logger.h"

#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    m_item = new QGraphicsPixmapItem();
    m_scene->addItem(m_item);
    setScene(m_scene);

    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(5, 10, 22)));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCursor(Qt::ArrowCursor);
}

void ImageView::setImage(const QImage& image)
{
    if (image.isNull()) {
        LOG_WARN("[ImageView] setImage received null image");
        return;
    }

    // ⭐ 优化：使用 QPixmap 缓存，避免频繁 fromImage 转换
    if (m_cachedPixmap.isNull() || m_cachedPixmap.size() != image.size()) {
        m_cachedPixmap = QPixmap::fromImage(image);
    } else {
        m_cachedPixmap.convertFromImage(image);
    }

    m_item->setPixmap(m_cachedPixmap);
    m_scene->setSceneRect(m_item->boundingRect());
    
    if (!m_hasImage) {
        m_hasImage = true;
        LOG_INFO(QString("[ImageView] First image set: %1x%2").arg(image.width()).arg(image.height()));
        fitImageInView();
    }
}

void ImageView::resetView()
{
    fitImageInView();
}

void ImageView::fitImageInView()
{
    if (!m_hasImage || m_item->pixmap().isNull()) return;
    resetTransform();
    fitInView(m_item, Qt::KeepAspectRatio);
    m_fitZoomFactor = transform().m11();
    m_zoomFactor = m_fitZoomFactor;
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    if (!m_hasImage) return;
    if (event->angleDelta().y() == 0) return;

    const bool zoomIn = event->angleDelta().y() > 0;
    const double step = 1.15;
    const double nextZoom = zoomIn ? (m_zoomFactor * step) : (m_zoomFactor / step);
    const double minZoom = m_fitZoomFactor * 0.5;
    const double maxZoom = m_fitZoomFactor * 30.0;
    if (nextZoom < minZoom || nextZoom > maxZoom) return;

    const double ratio = nextZoom / m_zoomFactor;
    scale(ratio, ratio);
    m_zoomFactor = nextZoom;
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    Q_UNUSED(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_hasImage) {
        fitImageInView();
        event->accept();
    } else {
        QGraphicsView::mouseDoubleClickEvent(event);
    }
}
