//
// Created by ragdoll on 2021/5/23.
//

#ifndef MPASTE_MTEXTBROWSER_H
#define MPASTE_MTEXTBROWSER_H

#include <QTextBrowser>

class MTextBrowser : public QTextBrowser {

public:
    explicit MTextBrowser(QWidget *parent = nullptr);

private:
    void wheelEvent(QWheelEvent *e) override;
};


#endif //MPASTE_MTEXTBROWSER_H
