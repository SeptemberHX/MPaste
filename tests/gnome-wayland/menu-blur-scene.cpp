#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QMenu>
#include <QMimeData>
#include "widget/MPasteSettingsWidget.h"
#include "widget/ClipboardItemPreviewDialog.h"
#include "widget/ClipboardItemDetailsDialog.h"
#include "widget/ClipboardItemRenameDialog.h"
#include "widget/AboutWidget.h"
#include "widget/BoardInternalHelpers.h"
#include "utils/ThemeManager.h"
#include "utils/MPasteSettings.h"
class Scene : public QWidget {
    QMenu menu{this};
public:
    Scene(){setWindowTitle("MPaste Blur Test");resize(900,650);BoardHelpers::applyMenuTheme(&menu);
      for(auto label:{"Paste as plain text", "Details", "Rename", "Preview", "Save", "Pin"})menu.addAction(label);
      menu.addSeparator();menu.addAction("Favorite");menu.addAction("Delete");}
    void paintEvent(QPaintEvent*)override{QPainter p(this);for(int y=0;y<height();y+=12)for(int x=0;x<width();x+=12)p.fillRect(x,y,12,12,((x/12+y/12)%2)?QColor("#e8eee5"):QColor("#2a5948"));p.setPen(Qt::black);p.setFont(QFont("Sans",28));p.drawText(QRect(30,50,800,550),Qt::AlignCenter,"SHARP BACKGROUND\nBEHIND FROSTED MENU");}
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_F12) { menu.popup(mapToGlobal(QPoint(250,120))); return; }
        QMimeData mime; mime.setText("Frosted preview: sharp text over blurred background.");
        ClipboardItem item(QPixmap(), &mime);
        QDialog *dialog = nullptr;
        if (event->key() == Qt::Key_F2) dialog = new MPasteSettingsWidget(this);
        if (event->key() == Qt::Key_F3) {
            auto *preview = new ClipboardItemPreviewDialog(this);
            preview->showItem(item); dialog = preview;
        }
        if (event->key() == Qt::Key_F4) {
            auto *details = new ClipboardItemDetailsDialog(this);
            details->showItem(item); dialog = details;
        }
        if (event->key() == Qt::Key_F5) dialog = new AboutWidget(this);
        if (event->key() == Qt::Key_F6) dialog = new ClipboardItemRenameDialog("Frosted alias",this);
        if (dialog) { dialog->setWindowTitle("MPaste Glass Popup"); dialog->show(); }
    }
};
int main(int argc,char**argv){QApplication app(argc,argv);MPasteSettings::getInst()->setThemeMode(qEnvironmentVariableIsSet("MPASTE_TEST_LIGHT") ? MPasteSettings::ThemeLight : MPasteSettings::ThemeDark);ThemeManager::instance()->initialize();Scene scene;scene.show();return app.exec();}
