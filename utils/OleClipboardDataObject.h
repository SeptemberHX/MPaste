// input: Win32 OLE/COM APIs, QMimeData.
// output: Custom IDataObject that serves Embed Source as TYMED_ISTORAGE
//         for perfect Word paste fidelity.
// pos: utils layer — OLE clipboard playback for Office content.
#ifndef MPASTE_OLECLIPBOARDDATAOBJECT_H
#define MPASTE_OLECLIPBOARDDATAOBJECT_H

#include <QtGlobal> // for Q_OS_WIN

#ifdef Q_OS_WIN

#include <QMimeData>
#include <windows.h>
#include <objidl.h>

/// Custom IDataObject that wraps a QMimeData and additionally serves
/// OLE container formats (Embed Source, Object Descriptor) with the
/// correct TYMED.  This allows Word to consume the pasted data via
/// its native OLE path instead of falling back to RTF/HTML.
///
/// Usage:
///   auto *obj = OleClipboardDataObject::create(mimeData);
///   if (obj) { OleSetClipboard(obj); obj->Release(); }
///
/// The object is reference-counted.  OleSetClipboard AddRef's it;
/// the caller should Release after the call.
class OleClipboardDataObject : public IDataObject {
public:
    /// Create from a QMimeData.  Returns nullptr if the mimeData has
    /// no Embed Source data.  Caller owns one reference.
    static OleClipboardDataObject *create(const QMimeData *mimeData);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *pFmt, STGMEDIUM *pMedium) override;
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *pFmt, STGMEDIUM *pMedium) override;
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *pFmt) override;
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *pIn, FORMATETC *pOut) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *pFmt, STGMEDIUM *pMedium, BOOL fRelease) override;
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppEnum) override;
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override;
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override;
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override;

private:
    OleClipboardDataObject();
    ~OleClipboardDataObject();

    struct FormatEntry {
        FORMATETC fmt;
        QByteArray data;       // raw bytes for HGLOBAL formats
        bool isStorage = false; // true → data is serialized IStorage
    };

    LONG refCount_ = 1;
    QList<FormatEntry> entries_;

    HRESULT serveHGlobal(const QByteArray &data, STGMEDIUM *pMedium);
    HRESULT serveIStorage(const QByteArray &data, STGMEDIUM *pMedium);
};

#endif // Q_OS_WIN
#endif // MPASTE_OLECLIPBOARDDATAOBJECT_H
