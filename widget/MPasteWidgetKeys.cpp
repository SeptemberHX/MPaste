// MPasteWidgetKeys.cpp — Keyboard handling, event filter, and focus management.
// Split from MPasteWidget.cpp; shares the MPasteWidget class.
#include <QButtonGroup>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QComboBox>
#include <QWindow>
#include <QTimer>

#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "utils/MPasteSettings.h"
#include "ScrollItemsWidget.h"
#include "ClipboardItemPreviewDialog.h"
#include "ClipboardAppController.h"
#include "WindowBlurHelper.h"
#include "BoardInternalHelpers.h"
#include "GlassPagePopup.h"

// ── anonymous-namespace helpers ────────────────────────────────────────

namespace {

static int shortcutIndexForKey(int key) {
    switch (key) {
        case Qt::Key_1:
        case Qt::Key_Exclam:
            return 0;
        case Qt::Key_2:
        case Qt::Key_At:
            return 1;
        case Qt::Key_3:
        case Qt::Key_NumberSign:
            return 2;
        case Qt::Key_4:
        case Qt::Key_Dollar:
            return 3;
        case Qt::Key_5:
        case Qt::Key_Percent:
            return 4;
        case Qt::Key_6:
        case Qt::Key_AsciiCircum:
            return 5;
        case Qt::Key_7:
        case Qt::Key_Ampersand:
            return 6;
        case Qt::Key_8:
        case Qt::Key_Asterisk:
            return 7;
        case Qt::Key_9:
        case Qt::Key_ParenLeft:
            return 8;
        case Qt::Key_0:
        case Qt::Key_ParenRight:
            return 9;
        default:
            return -1;
    }
}

} // anonymous namespace

// QEvent::KeyPress conflicts with the KeyPress in X.h
#undef KeyPress

// ── MPasteWidget keyboard/event methods ────────────────────────────────

void MPasteWidget::handleKeyboardEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            handleEscapeKey();
            break;
        case Qt::Key_Alt:
            currItemsWidget()->setShortcutInfo();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            handleEnterKey(event->modifiers().testFlag(Qt::ControlModifier));
            break;
        case Qt::Key_Space:
            handlePreviewKey();
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            handleNavigationKeys(event);
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            handleHomeEndKeys(event);
            break;
        case Qt::Key_Tab:
            handleTabKey();
            break;
        case Qt::Key_M:
            if (event->modifiers().testFlag(Qt::ControlModifier)) {
                dumpMemoryStats();
                return;
            }
            handleSearchInput(event);
            break;
        default:
            handleSearchInput(event);
            break;
    }
}

void MPasteWidget::handleTabKey() {
    QAbstractButton* currentButton = ui_.buttonGroup->checkedButton();
    QList<QAbstractButton*> buttons = ui_.buttonGroup->buttons();
    int currentIndex = buttons.indexOf(currentButton);
    int nextIndex = (currentIndex + 1) % buttons.size();
    buttons[nextIndex]->click();
}

void MPasteWidget::handleEscapeKey() {
    if (ui_.ui->searchEdit->hasFocus() || !ui_.ui->searchEdit->text().isEmpty()) {
        ui_.ui->searchEdit->clear();
        setFocusOnSearch(false);
    } else {
        hide();
    }
}

void MPasteWidget::handleEnterKey(bool plainText) {
    if (currItemsWidget()->hasMultipleSelectedItems()) {
        return;
    }

    auto *board = currItemsWidget();
    const ClipboardItem *selectedItem = board->selectedByEnter();
    if (selectedItem && controller_->setClipboard(*selectedItem, plainText)) {
        hideAndPaste();
        board->moveSelectedToFirst();
    }
}

void MPasteWidget::handlePreviewKey() {
    ClipboardItemPreviewDialog *previewDialog = ensurePreviewDialog();
    if (previewDialog->isVisible()) {
        previewDialog->reject();
        return;
    }

    if (currItemsWidget()->hasMultipleSelectedItems()) {
        return;
    }

    const ClipboardItem *selectedItem = currItemsWidget()->currentSelectedItem();
    if (!selectedItem || !ClipboardItemPreviewDialog::supportsPreview(*selectedItem)) {
        return;
    }

    previewDialog->showItem(*selectedItem);
}

bool MPasteWidget::triggerShortcutPaste(int shortcutIndex, bool plainText) {
    if (shortcutIndex < 0 || shortcutIndex > 9) {
        return false;
    }

    auto *board = currItemsWidget();
    const ClipboardItem *selectedItem = board->selectedByShortcut(shortcutIndex);
    if (!selectedItem || !controller_->setClipboard(*selectedItem, plainText)) {
        return false;
    }

    const QString itemName = selectedItem->getName();
    qInfo().noquote() << QStringLiteral("[shortcut-paste] index=%1 itemName=%2")
        .arg(shortcutIndex).arg(itemName);
    QTimer::singleShot(50, this, [this, board, itemName]() {
        hideAndPaste();
        board->moveItemByNameToFirst(itemName);
        currItemsWidget()->cleanShortCutInfo();
    });
    return true;
}

void MPasteWidget::handleNavigationKeys(QKeyEvent *event) {
    if (!ui_.ui->searchEdit->isVisible()) {
        if (event->key() == Qt::Key_Left) {
            currItemsWidget()->focusMoveLeft();
        } else {
            currItemsWidget()->focusMoveRight();
        }
    } else if (ui_.ui->searchEdit->isVisible()) {
        QGuiApplication::sendEvent(ui_.ui->searchEdit, event);
        setFocusOnSearch(true);
    }
}

void MPasteWidget::handleHomeEndKeys(QKeyEvent *event) {
    if (!ui_.ui->searchEdit->isVisible()) {
        if (event->key() == Qt::Key_Home) {
            currItemsWidget()->scrollToFirst();
        } else {
            currItemsWidget()->scrollToLast();
        }
    }
}

void MPasteWidget::handleSearchInput(QKeyEvent *event) {
    if (event->key() < Qt::Key_Space || event->key() > Qt::Key_AsciiTilde) {
        return;
    }

    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers & (Qt::AltModifier | Qt::ControlModifier)) {
        event->ignore();
        return;
    }

    if (!ui_.ui->searchEdit->hasFocus()) {
        ui_.ui->searchEdit->setFocus();
        setFocusOnSearch(true);
    }

    QString currentText = ui_.ui->searchEdit->text();
    currentText += event->text();
    ui_.ui->searchEdit->setText(currentText);
    event->accept();
}

bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Paint &&
        (watched == ui_.ui->clipboardBtnWidget || watched == ui_.ui->typeBtnWidget)) {
        QWidget *w = qobject_cast<QWidget*>(watched);
        QPainter p(w);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal radius = 13.0;
        QRectF r = QRectF(w->rect());

        if (darkTheme_) {
            // Glass pill container
            r = r.adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawRoundedRect(r, radius, radius);
        } else {
            // Light: glass pill container with dark tint
            r = r.adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(QColor(0, 0, 0, 25), 1.0));
            p.setBrush(QColor(0, 0, 0, 8));
            p.drawRoundedRect(r, radius, radius);
        }
    }

    if (event->type() == QEvent::Wheel) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
        return this->currItemsWidget()->handleWheelScroll(static_cast<QWheelEvent *>(event));
#endif
    } else if (event->type() == QEvent::KeyPress) {
        if (watched == ui_.ui->searchEdit) {
            auto keyEvent = dynamic_cast<QKeyEvent*>(event);
            if (keyEvent->modifiers() & Qt::AltModifier) {
                QGuiApplication::sendEvent(this, keyEvent);
                return true;
            }
        }
    }

    if (watched == ui_.pageNumberLabel && event->type() == QEvent::MouseButtonPress) {
        ScrollItemsWidget *board = currItemsWidget();
        const int totalPages = board ? board->totalPageCount() : 0;
        const int currentPage = board ? board->currentPageNumber() : 1;
        if (totalPages > 1) {
            auto *popup = new GlassPagePopup(this);
            popup->setAttribute(Qt::WA_DeleteOnClose);
            popup->setDark(darkTheme_);
            popup->setPages(totalPages, currentPage);
            connect(popup, &GlassPagePopup::pageSelected, this, [this](int page) {
                ui_.pageComboBox->setCurrentIndex(page - 1);
            });
            const QPoint pos = ui_.pageNumberLabel->mapToGlobal(
                QPoint(0, -popup->sizeHint().height() - 4));
            popup->popup(QPoint(pos.x() - popup->width() / 2 + ui_.pageNumberLabel->width() / 2,
                                ui_.pageNumberLabel->mapToGlobal(QPoint(0, 0)).y() - popup->height() - 4));
        }
        return true;
    }

    return QObject::eventFilter(watched, event);
}

void MPasteWidget::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & Qt::AltModifier) {
        const int shortcutIndex = shortcutIndexForKey(event->key());
        if (shortcutIndex >= 0) {
            if (event->isAutoRepeat()) {
                event->accept();
                return;
            }

            misc_.pendingNumKey = shortcutIndex;
            misc_.pendingPlainTextNumKey = event->modifiers().testFlag(Qt::ShiftModifier);
            triggerShortcutPaste(shortcutIndex, misc_.pendingPlainTextNumKey);
            event->accept();
            return;
        }
    }
    handleKeyboardEvent(event);
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        currItemsWidget()->cleanShortCutInfo();
        misc_.pendingNumKey = -1;
        misc_.pendingPlainTextNumKey = false;
    }
    else {
        const int releasedShortcutIndex = shortcutIndexForKey(event->key());
        if (releasedShortcutIndex == misc_.pendingNumKey && misc_.pendingNumKey >= 0) {
            misc_.pendingNumKey = -1;
            misc_.pendingPlainTextNumKey = false;
            event->accept();
            return;
        }
    }

    QWidget::keyReleaseEvent(event);
}

bool MPasteWidget::focusNextPrevChild(bool next) {
    Q_UNUSED(next);
    return false;
}

void MPasteWidget::setFocusOnSearch(bool flag) {
    if (flag) {
        ui_.ui->searchEdit->show();
        ui_.searchShowAnim->start();
        ui_.ui->searchEdit->setFocus();
    } else {
        ui_.searchHideAnim->start();
        ui_.ui->searchEdit->clearFocus();
        setFocus();
    }
}
