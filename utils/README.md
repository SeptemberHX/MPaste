<!--
input: 渚濊禆鎵€灞炵洰褰曠殑鐪熷疄缁撴瀯銆佽亴璐ｄ笌鏂囦欢鍙樺寲銆?
output: 瀵瑰鎻愪緵鐩綍绾ф灦鏋勮鏄庝笌鏂囦欢娓呭崟銆?
pos: 鐩綍绾х淮鎶ゆ枃妗ｄ笌鍙樻洿绾︽潫鍏ュ彛銆?
update: 涓€鏃︽垜琚洿鏂帮紝鍔″繀鏇存柊鎴戠殑寮€澶存敞閲婏紝浠ュ強鎵€灞炵殑鏂囦欢澶圭殑 README.md銆?
-->

# utils

> 涓€鏃︽垜鎵€灞炵殑鏂囦欢澶规湁鎵€鍙樺寲锛岃鏇存柊鎴戙€?
- 瀹氫綅锛歶tils 灞傛壙杞借法骞冲彴绯荤粺鑳藉姏涓庨€氱敤鏈嶅姟銆?
- 渚濊禆锛歈t 骞冲彴鎶借薄銆佺郴缁?API 涓庣綉缁滆兘鍔涖€?
- 杈撳嚭锛氱洃鎺с€佺儹閿€佸崟瀹炰緥銆佸钩鍙版ˉ鎺ョ瓑鍩虹鏈嶅姟銆?

## Files
- `ClipboardMonitor.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 ClipboardMonitor 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `ClipboardMonitor.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 ClipboardMonitor 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?
- `HotKeyManager.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 HotKeyManager 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `HotKeyManager.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 HotKeyManager 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?
- `MPasteSettings.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 MPasteSettings 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `MPasteSettings.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 MPasteSettings 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?
- `OpenGraphFetcher.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 OpenGraphFetcher 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `OpenGraphFetcher.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 OpenGraphFetcher 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?
- `PlatformRelated.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 PlatformRelated 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `PlatformRelated.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 PlatformRelated 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?
- `README.md`: 鍦颁綅=鐩綍璇存槑锛涘姛鑳?鎬荤粨鏈洰褰曡亴璐ｃ€佺害鏉熶笌鏂囦欢娓呭崟銆?
- `SingleApplication.cpp`: 鍦颁綅=瀹炵幇鏂囦欢锛涘姛鑳?瀹炵幇 SingleApplication 鐨勮繍琛岄€昏緫涓庤涓恒€?
- `SingleApplication.h`: 鍦颁綅=鎺ュ彛澹版槑锛涘姛鑳?澹版槑 SingleApplication 鐨勫叕寮€绫诲瀷銆佷俊鍙枫€佹Ы鎴栧嚱鏁般€?

- ClipboardMonitor now downloads WPS single-image HTML payloads during capture when needed, so newly captured WPS images can be materialized into standard PNG MIME before the item is saved.
- `ClipboardMonitor` now uses raw-string regex literals for WPS HTML image URL extraction, avoiding MinGW unknown-escape warning noise.

- ClipboardMonitor now gives WPS/Kingsoft staged clipboard writes an extra settle window, so one Ctrl+C is less likely to emit both an intermediate payload and the final rich payload as two separate items.

