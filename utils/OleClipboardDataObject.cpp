// input: OleClipboardDataObject.h, Win32 OLE APIs.
// output: IDataObject serving Embed Source as TYMED_ISTORAGE.
// pos: utils layer — OLE clipboard playback.
#include "OleClipboardDataObject.h"

#ifdef Q_OS_WIN

#include <ole2.h>
#include <QDebug>
#include <QSet>

// ── IEnumFORMATETC implementation ─────────────────────────────────────

class OleFormatEnumerator : public IEnumFORMATETC {
public:
    OleFormatEnumerator(const QList<FORMATETC> &fmts)
        : refCount_(1), fmts_(fmts), index_(0) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refCount_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) override {
        ULONG fetched = 0;
        while (fetched < celt && index_ < fmts_.size()) {
            rgelt[fetched] = fmts_[index_];
            ++fetched; ++index_;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        index_ += static_cast<int>(celt);
        return (index_ <= fmts_.size()) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { index_ = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **ppEnum) override {
        auto *c = new OleFormatEnumerator(fmts_);
        c->index_ = index_;
        *ppEnum = c;
        return S_OK;
    }

private:
    ~OleFormatEnumerator() = default;
    LONG refCount_;
    QList<FORMATETC> fmts_;
    int index_;
};

// ── Helpers ───────────────────────────────────────────────────────────

static CLIPFORMAT cfForName(const wchar_t *name) {
    return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));
}

/// Extract the Windows format name from a Qt MIME name like
/// "application/x-qt-windows-mime;value=\"Rich Text Format\"".
static CLIPFORMAT cfFromQtMime(const QString &mime) {
    static const QString prefix = QStringLiteral("application/x-qt-windows-mime;value=\"");
    if (mime.startsWith(prefix) && mime.endsWith(QLatin1Char('"'))) {
        const QString name = mime.mid(prefix.size(), mime.size() - prefix.size() - 1);
        return static_cast<CLIPFORMAT>(
            RegisterClipboardFormatW(reinterpret_cast<LPCWSTR>(name.utf16())));
    }
    return 0;
}

// ── OleClipboardDataObject ────────────────────────────────────────────

OleClipboardDataObject::OleClipboardDataObject() = default;
OleClipboardDataObject::~OleClipboardDataObject() = default;

OleClipboardDataObject *OleClipboardDataObject::create(const QMimeData *mimeData) {
    if (!mimeData) return nullptr;

    static const CLIPFORMAT cfEmbedSource = cfForName(L"Embed Source");
    static const CLIPFORMAT cfObjectDescriptor = cfForName(L"Object Descriptor");
    static const CLIPFORMAT cfRtf = cfForName(L"Rich Text Format");

    // Only use this path if the mimeData actually has Embed Source.
    const QString embedMime = QStringLiteral("application/x-qt-windows-mime;value=\"Embed Source\"");
    if (!mimeData->hasFormat(embedMime))
        return nullptr;
    const QByteArray embedData = mimeData->data(embedMime);
    if (embedData.isEmpty())
        return nullptr;

    // Verify it's actually a serialized compound storage.
    {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, embedData.size());
        if (!hg) return nullptr;
        memcpy(GlobalLock(hg), embedData.constData(), embedData.size());
        GlobalUnlock(hg);
        ILockBytes *lb = nullptr;
        HRESULT hr = CreateILockBytesOnHGlobal(hg, TRUE, &lb);
        if (FAILED(hr)) { GlobalFree(hg); return nullptr; }
        hr = StgIsStorageILockBytes(lb);
        lb->Release();
        if (hr != S_OK) {
            qInfo() << "[ole-dataobject] Embed Source is not a compound storage, skipping";
            return nullptr;
        }
    }

    auto *obj = new OleClipboardDataObject;

    // ── Add Embed Source with TYMED_ISTORAGE ──
    {
        FormatEntry e;
        e.fmt = {cfEmbedSource, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTORAGE};
        e.data = embedData;
        e.isStorage = true;
        obj->entries_.append(e);
    }

    // ── Add Object Descriptor (TYMED_HGLOBAL) ──
    {
        const QString odMime = QStringLiteral("application/x-qt-windows-mime;value=\"Object Descriptor\"");
        const QByteArray odData = mimeData->data(odMime);
        if (!odData.isEmpty()) {
            FormatEntry e;
            e.fmt = {cfObjectDescriptor, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            e.data = odData;
            obj->entries_.append(e);
        }
    }

    // ── Add RTF (TYMED_HGLOBAL) ──
    {
        const QString rtfMime = QStringLiteral("application/x-qt-windows-mime;value=\"Rich Text Format\"");
        const QByteArray rtfData = mimeData->data(rtfMime);
        if (!rtfData.isEmpty()) {
            FormatEntry e;
            e.fmt = {cfRtf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            e.data = rtfData;
            obj->entries_.append(e);
        }
    }

    // ── Add CF_UNICODETEXT ──
    if (mimeData->hasText()) {
        const QString text = mimeData->text();
        if (!text.isEmpty()) {
            const int byteLen = (text.size() + 1) * 2;
            QByteArray buf(byteLen, '\0');
            memcpy(buf.data(), text.utf16(), text.size() * 2);
            FormatEntry e;
            e.fmt = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            e.data = buf;
            obj->entries_.append(e);
        }
    }

    // NOTE: CF_HTML is intentionally NOT provided here.  Constructing
    // it manually is fragile (double-wrapped <html> tags, wrong offsets)
    // and causes Word to prefer broken HTML over perfect RTF.  Without
    // CF_HTML, Word falls back to RTF which preserves all formatting,
    // fonts, images, and layout.

    // ── Add remaining safe Windows formats ──
    for (const QString &mime : mimeData->formats()) {
        if (mime == QStringLiteral("text/plain")
            || mime == QStringLiteral("text/html")
            || mime == QStringLiteral("text/uri-list"))
            continue; // handled above
        const CLIPFORMAT cf = cfFromQtMime(mime);
        if (cf == 0) continue;
        if (cf == cfEmbedSource || cf == cfObjectDescriptor || cf == cfRtf)
            continue; // already added

        // Skip dangerous OLE container formats.
        const QString lower = mime.toLower();
        if (lower.contains(QStringLiteral("embedded object"))
            || lower.contains(QStringLiteral("link source"))
            || lower.contains(QStringLiteral("ownerlink"))
            || lower.contains(QStringLiteral("native"))
            || lower.contains(QStringLiteral("objectlink")))
            continue;

        const QByteArray data = mimeData->data(mime);
        if (data.isEmpty()) continue;

        FormatEntry e;
        e.fmt = {cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        e.data = data;
        obj->entries_.append(e);
    }

    qInfo().noquote() << QStringLiteral("[ole-dataobject] created with %1 formats, embedSource=%2 bytes")
        .arg(obj->entries_.size()).arg(embedData.size());
    return obj;
}

// ── shouldUseNativeOlePath ────────────────────────────────────────────

bool OleClipboardDataObject::shouldUseNativeOlePath(const QMimeData *mimeData) {
    if (!mimeData) return false;
    // Use the native OLE path when raw CF_HTML bytes are present —
    // this is the only way to preserve the <style> block that Qt's
    // QWindowsMimeHtml strips during the text/html → CF_HTML conversion.
    static const QString cfHtmlMime =
        QStringLiteral("application/x-qt-windows-mime;value=\"HTML Format\"");
    return mimeData->hasFormat(cfHtmlMime) && !mimeData->data(cfHtmlMime).isEmpty();
}

// ── createGeneral (Ditto-style) ──────────────────────────────────────

OleClipboardDataObject *OleClipboardDataObject::createGeneral(const QMimeData *mimeData) {
    if (!mimeData) return nullptr;

    auto *obj = new OleClipboardDataObject;
    QSet<CLIPFORMAT> added;

    // ── 1. CF_UNICODETEXT from text/plain ──
    if (mimeData->hasText()) {
        const QString text = mimeData->text();
        if (!text.isEmpty()) {
            const int byteLen = (text.size() + 1) * 2;
            QByteArray buf(byteLen, '\0');
            memcpy(buf.data(), text.utf16(), text.size() * 2);
            FormatEntry e;
            e.fmt = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            e.data = buf;
            obj->entries_.append(e);
            added.insert(CF_UNICODETEXT);
        }
    }

    // ── 2. All application/x-qt-windows-mime;value="X" formats ──
    for (const QString &mime : mimeData->formats()) {
        // Skip standard MIME types — handled specially or not at all.
        if (mime == QStringLiteral("text/plain")
            || mime == QStringLiteral("text/html")
            || mime == QStringLiteral("text/uri-list")
            || mime == QStringLiteral("text/plain;charset=utf-8")
            || mime.startsWith(QStringLiteral("application/x-qt-image"))
            || mime.startsWith(QStringLiteral("image/")))
            continue;

        const CLIPFORMAT cf = cfFromQtMime(mime);
        if (cf == 0) continue;
        if (added.contains(cf)) continue;

        // Block OLE container formats.  Even though our IDataObject CAN
        // serve Embed Source as TYMED_ISTORAGE, Word prefers it over
        // CF_HTML/RTF and creates an embedded OLE object instead of
        // inline formatted text.  Blocking it forces Word to use
        // CF_HTML (with our faithful raw bytes including <style>) or RTF.
        const QString lower = mime.toLower();
        if (lower.contains(QStringLiteral("embed source"))
            || lower.contains(QStringLiteral("embedded object"))
            || lower.contains(QStringLiteral("object descriptor"))
            || lower.contains(QStringLiteral("link source"))
            || lower.contains(QStringLiteral("ownerlink"))
            || lower.contains(QStringLiteral("native"))
            || lower.contains(QStringLiteral("objectlink")))
            continue;

        const QByteArray data = mimeData->data(mime);
        if (data.isEmpty()) continue;

        FormatEntry e;
        e.fmt = {cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        e.data = data;
        obj->entries_.append(e);
        added.insert(cf);
    }

    if (obj->entries_.isEmpty()) {
        delete obj;
        return nullptr;
    }

    qInfo().noquote() << QStringLiteral("[ole-dataobject] createGeneral %1 formats")
        .arg(obj->entries_.size());
    return obj;
}

// ── IUnknown ──────────────────────────────────────────────────────────

HRESULT OleClipboardDataObject::QueryInterface(REFIID riid, void **ppv) {
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
        *ppv = static_cast<IDataObject *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG OleClipboardDataObject::AddRef() {
    return InterlockedIncrement(&refCount_);
}

ULONG OleClipboardDataObject::Release() {
    LONG r = InterlockedDecrement(&refCount_);
    if (r == 0) delete this;
    return r;
}

// ── IDataObject ───────────────────────────────────────────────────────

HRESULT OleClipboardDataObject::serveHGlobal(const QByteArray &data, STGMEDIUM *pMedium) {
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hg) return E_OUTOFMEMORY;
    memcpy(GlobalLock(hg), data.constData(), data.size());
    GlobalUnlock(hg);
    pMedium->tymed = TYMED_HGLOBAL;
    pMedium->hGlobal = hg;
    pMedium->pUnkForRelease = nullptr;
    return S_OK;
}

HRESULT OleClipboardDataObject::serveIStorage(const QByteArray &data, STGMEDIUM *pMedium) {
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hg) return E_OUTOFMEMORY;
    memcpy(GlobalLock(hg), data.constData(), data.size());
    GlobalUnlock(hg);

    ILockBytes *lb = nullptr;
    HRESULT hr = CreateILockBytesOnHGlobal(hg, TRUE, &lb);
    if (FAILED(hr)) { GlobalFree(hg); return hr; }

    IStorage *stg = nullptr;
    hr = StgOpenStorageOnILockBytes(lb, nullptr,
        STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &stg);
    lb->Release();
    if (FAILED(hr)) return hr;

    pMedium->tymed = TYMED_ISTORAGE;
    pMedium->pstg = stg; // caller will Release
    pMedium->pUnkForRelease = nullptr;
    return S_OK;
}

HRESULT OleClipboardDataObject::GetData(FORMATETC *pFmt, STGMEDIUM *pMedium) {
    if (!pFmt || !pMedium) return E_INVALIDARG;
    memset(pMedium, 0, sizeof(*pMedium));

    for (const FormatEntry &e : entries_) {
        if (e.fmt.cfFormat != pFmt->cfFormat)
            continue;
        if (e.isStorage && (pFmt->tymed & TYMED_ISTORAGE)) {
            HRESULT hr = serveIStorage(e.data, pMedium);
            qInfo().noquote() << QStringLiteral("[ole-dataobject] GetData cf=%1 ISTORAGE hr=0x%2 bytes=%3")
                .arg(pFmt->cfFormat).arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')).arg(e.data.size());
            return hr;
        }
        if (!e.isStorage && (pFmt->tymed & TYMED_HGLOBAL)) {
            HRESULT hr = serveHGlobal(e.data, pMedium);
            qInfo().noquote() << QStringLiteral("[ole-dataobject] GetData cf=%1 HGLOBAL hr=0x%2 bytes=%3")
                .arg(pFmt->cfFormat).arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')).arg(e.data.size());
            return hr;
        }
        qInfo().noquote() << QStringLiteral("[ole-dataobject] GetData cf=%1 TYMED_MISMATCH requested=0x%2 have=%3")
            .arg(pFmt->cfFormat).arg(pFmt->tymed, 0, 16).arg(e.isStorage ? "ISTORAGE" : "HGLOBAL");
        return DV_E_TYMED;
    }
    {
        wchar_t fmtName[256] = {};
        GetClipboardFormatNameW(pFmt->cfFormat, fmtName, 255);
        qInfo().noquote() << QStringLiteral("[ole-dataobject] GetData cf=%1 NOT_FOUND name=\"%2\" tymed=0x%3")
            .arg(pFmt->cfFormat)
            .arg(QString::fromWCharArray(fmtName))
            .arg(pFmt->tymed, 0, 16);
    }
    return DV_E_FORMATETC;
}

HRESULT OleClipboardDataObject::GetDataHere(FORMATETC *, STGMEDIUM *) {
    return E_NOTIMPL;
}

HRESULT OleClipboardDataObject::QueryGetData(FORMATETC *pFmt) {
    if (!pFmt) return E_INVALIDARG;
    for (const FormatEntry &e : entries_) {
        if (e.fmt.cfFormat == pFmt->cfFormat) {
            if (e.isStorage && (pFmt->tymed & TYMED_ISTORAGE)) return S_OK;
            if (!e.isStorage && (pFmt->tymed & TYMED_HGLOBAL)) return S_OK;
            return DV_E_TYMED;
        }
    }
    return DV_E_FORMATETC;
}

HRESULT OleClipboardDataObject::GetCanonicalFormatEtc(FORMATETC *, FORMATETC *pOut) {
    if (pOut) pOut->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

HRESULT OleClipboardDataObject::SetData(FORMATETC *, STGMEDIUM *, BOOL) {
    return E_NOTIMPL;
}

HRESULT OleClipboardDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppEnum) {
    if (dwDirection != DATADIR_GET || !ppEnum) return E_INVALIDARG;
    QList<FORMATETC> fmts;
    fmts.reserve(entries_.size());
    for (const FormatEntry &e : entries_)
        fmts.append(e.fmt);
    *ppEnum = new OleFormatEnumerator(fmts);
    qInfo().noquote() << QStringLiteral("[ole-dataobject] EnumFormatEtc called, %1 formats").arg(fmts.size());
    return S_OK;
}

HRESULT OleClipboardDataObject::DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) { return OLE_E_ADVISENOTSUPPORTED; }
HRESULT OleClipboardDataObject::DUnadvise(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
HRESULT OleClipboardDataObject::EnumDAdvise(IEnumSTATDATA **) { return OLE_E_ADVISENOTSUPPORTED; }

#endif // Q_OS_WIN
