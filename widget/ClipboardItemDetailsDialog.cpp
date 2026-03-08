// input: Depends on ClipboardItemDetailsDialog.h, Qt form widgets, and ClipboardItem metadata accessors.
// output: Implements the clipboard item inspector dialog UI and data binding.
// pos: Widget-layer details dialog implementation for clipboard debugging and transparency.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemDetailsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QTextEdit>
#include <QVBoxLayout>

ClipboardItemDetailsDialog::ClipboardItemDetailsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Clipboard Item Details"));
    resize(760, 620);

    auto *rootLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);

    ui_.typeValue = new QLabel(this);
    ui_.timeValue = new QLabel(this);
    ui_.titleValue = new QLabel(this);
    ui_.urlValue = new QLabel(this);

    for (QLabel *label : {ui_.typeValue, ui_.timeValue, ui_.titleValue, ui_.urlValue}) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
    }

    formLayout->addRow(tr("Type"), ui_.typeValue);
    formLayout->addRow(tr("Time"), ui_.timeValue);
    formLayout->addRow(tr("Title"), ui_.titleValue);
    formLayout->addRow(tr("URL"), ui_.urlValue);
    rootLayout->addLayout(formLayout);

    auto createReadOnlyEditor = [this]() {
        auto *edit = new QTextEdit(this);
        edit->setReadOnly(true);
        edit->setMinimumHeight(120);
        edit->setAcceptRichText(false);
        return edit;
    };

    ui_.normalizedTextEdit = createReadOnlyEditor();
    ui_.normalizedUrlsEdit = createReadOnlyEditor();
    ui_.mimeFormatsEdit = createReadOnlyEditor();

    rootLayout->addWidget(new QLabel(tr("Normalized Text"), this));
    rootLayout->addWidget(ui_.normalizedTextEdit, 1);
    rootLayout->addWidget(new QLabel(tr("Normalized URLs"), this));
    rootLayout->addWidget(ui_.normalizedUrlsEdit, 1);
    rootLayout->addWidget(new QLabel(tr("Raw MIME Formats"), this));
    rootLayout->addWidget(ui_.mimeFormatsEdit, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    rootLayout->addWidget(buttonBox);
}

QString ClipboardItemDetailsDialog::contentTypeLabel(ClipboardItem::ContentType type) const {
    switch (type) {
        case ClipboardItem::Text: return tr("Text");
        case ClipboardItem::Link: return tr("Link");
        case ClipboardItem::Image: return tr("Image");
        case ClipboardItem::RichText: return tr("Rich Text");
        case ClipboardItem::File: return tr("File");
        case ClipboardItem::Color: return tr("Color");
        case ClipboardItem::All:
        default: return tr("All");
    }
}

QString ClipboardItemDetailsDialog::joinUrls(const QList<QUrl> &urls) const {
    QStringList lines;
    for (const QUrl &url : urls) {
        lines << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    return lines.join(QLatin1Char('\n'));
}

void ClipboardItemDetailsDialog::showItem(const ClipboardItem &item) {
    ui_.typeValue->setText(contentTypeLabel(item.getContentType()));
    ui_.timeValue->setText(QLocale::system().toString(item.getTime(), QLocale::LongFormat));
    ui_.titleValue->setText(item.getTitle());
    ui_.urlValue->setText(item.getUrl());
    ui_.normalizedTextEdit->setPlainText(item.getNormalizedText());
    ui_.normalizedUrlsEdit->setPlainText(joinUrls(item.getNormalizedUrls()));

    QStringList formats;
    if (const QMimeData *mimeData = item.getMimeData()) {
        for (const QString &format : mimeData->formats()) {
            formats << format;
        }
    }
    ui_.mimeFormatsEdit->setPlainText(formats.join(QLatin1Char('\n')));

    show();
    raise();
    activateWindow();
}
