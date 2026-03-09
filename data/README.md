<!--
input: 娓氭繆绂嗛幍鈧仦鐐垫窗瑜版洜娈戦惇鐔风杽缂佹挻鐎妴浣戒捍鐠愶絼绗岄弬鍥︽閸欐ê瀵查妴?output: 鐎电懓顦婚幓鎰返閻╊喖缍嶇痪褎鐏﹂弸鍕嚛閺勫簼绗岄弬鍥︽濞撳懎宕熼妴?pos: 閻╊喖缍嶇痪褏娣幎銈嗘瀮濡楋絼绗岄崣妯绘纯缁撅附娼崗銉ュ經閵?update: 娑撯偓閺冿附鍨滅悮顐ｆ纯閺傚府绱濋崝鈥崇箑閺囧瓨鏌婇幋鎴犳畱瀵偓婢跺瓨鏁為柌濠忕礉娴犮儱寮烽幍鈧仦鐐垫畱閺傚洣娆㈡径鍦畱 README.md閵?-->

# data

> 娑撯偓閺冿附鍨滈幍鈧仦鐐垫畱閺傚洣娆㈡径瑙勬箒閹碘偓閸欐ê瀵查敍宀冾嚞閺囧瓨鏌婇幋鎴欌偓?- 鐎规矮缍呴敍姝瀉ta 鐏炲倹澹欐潪钘夊鐠愬瓨婢橀弫鐗堝祦濡€崇€烽妴浣哥碍閸掓瀵叉稉搴㈠瘮娑斿懎瀵茬憴鍕灟閵?- 娓氭繆绂嗛敍姝坱 Core/Gui 閻?mime閵嗕礁娴橀崓蹇庣瑢閺冨爼妫跨猾璇茬€烽妴?- 鏉堟挸鍤敍姘讲鐎涙ê鍋嶉妴浣稿讲濮ｆ棁绶濋妴浣稿讲閹垹顦查惃鍕殶閹诡喖顕挒掳鈧?
## Files
- `ClipboardItem.h`: 閸﹂缍?閹恒儱褰涙竟鐗堟閿涙稑濮涢懗?婢圭増妲?ClipboardItem 閻ㄥ嫬鍙曞鈧猾璇茬€烽妴浣蜂繆閸欐灚鈧焦蝎閹存牕鍤遍弫鑸偓?- `LocalSaver.cpp`: 閸﹂缍?鐎圭偟骞囬弬鍥︽閿涙稑濮涢懗?鐎圭偟骞?LocalSaver 閻ㄥ嫯绻嶇悰宀勨偓鏄忕帆娑撳氦顢戞稉鎭掆偓?- `LocalSaver.h`: 閸﹂缍?閹恒儱褰涙竟鐗堟閿涙稑濮涢懗?婢圭増妲?LocalSaver 閻ㄥ嫬鍙曞鈧猾璇茬€烽妴浣蜂繆閸欐灚鈧焦蝎閹存牕鍤遍弫鑸偓?- `OpenGraphItem.cpp`: 閸﹂缍?鐎圭偟骞囬弬鍥︽閿涙稑濮涢懗?鐎圭偟骞?OpenGraphItem 閻ㄥ嫯绻嶇悰宀勨偓鏄忕帆娑撳氦顢戞稉鎭掆偓?- `OpenGraphItem.h`: 閸﹂缍?閹恒儱褰涙竟鐗堟閿涙稑濮涢懗?婢圭増妲?OpenGraphItem 閻ㄥ嫬鍙曞鈧猾璇茬€烽妴浣蜂繆閸欐灚鈧焦蝎閹存牕鍤遍弫鑸偓?- `README.md`: 閸﹂缍?閻╊喖缍嶇拠瀛樻閿涙稑濮涢懗?閹崵绮ㄩ張顒傛窗瑜版洝浜寸拹锝冣偓浣哄閺夌喍绗岄弬鍥︽濞撳懎宕熼妴?
## Recent Notes
- `ClipboardItem` now caches searchable plain-text content to reduce repeated keyword scan cost.
- `ClipboardItem` now exposes a lightweight content fingerprint for dedup candidate lookup.
- `ClipboardItem` now avoids deprecated Qt hash APIs to keep Qt 6 builds warning-clean.
- `ClipboardItem` now standardizes protocol text and raw MIME payloads into normalized URLs/text for shared type recognition.
- Normalization now only trusts explicit URL/file evidence (`urls`, `text/uri-list`, `x-special/gnome-copied-files`, `x-special/nautilus-clipboard`) and no longer upgrades ordinary plain text heuristically.
- Native URL MIME is now only trusted when it is local-file data or matches the visible text payload, avoiding accidental promotion from stray platform clipboard formats.
- Explicit private formats such as Windows `FileName(W)` and `UniformResourceLocator(W)` are now normalized alongside Linux file-copy protocols.
- `LocalSaver` now writes `.mpaste v3` and migrates legacy/v2 files to v3 automatically when a save directory is loaded.
- `ClipboardItem` now extracts a stable identity from WPS/Kingsoft single-image HTML payloads, so different copied images no longer collapse into the same dedupe bucket.
- `ClipboardItem` now uses raw-string regex literals for WPS HTML image identity parsing, avoiding MinGW unknown-escape warning noise.
