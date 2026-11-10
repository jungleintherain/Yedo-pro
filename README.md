# YEdo Mod Pro

ม็อดเกม Yu-Gi-Oh! ที่ต่อยอดจาก [EDOPro](https://github.com/edo9300/edopro) (Project Ignis) — มีโหมดเนื้อเรื่อง, Duel World แบบ Duel Links (Ranked / Gate / ภารกิจ / ร้านค้า / พ่อค้าการ์ด / สกิล), Campaign, Sandbox และหน้าตาแบบ Master Duel

## โครงสร้าง repo

| โฟลเดอร์ | เนื้อหา |
|---|---|
| `windows/` | ซอร์สโค้ดเกม (EDOPro + ส่วนที่ YEdo เพิ่ม) รวม `ocgcore` ที่แก้แล้ว — ใช้สร้าง `YEdo Mod Pro.exe` |
| `android/` | ไฟล์สำหรับสร้าง APK (`jni/`, `build.gradle`, `src/`) และ `build-android.bat` |
| `data/` | ข้อมูลเกมของ YEdo: เนื้อเรื่อง, Duel World (ซอง/Structure Deck/ภารกิจ/สกิล), เด็คบอท, สคริปต์สกิล (`expansions/script`, `story-skills.cdb`), บทพูดบอท |
| `tools/skill-generator/` | สคริปต์ Python ที่ใช้สร้างสคริปต์สกิล Lua |

ไฟล์ที่ **ไม่ได้** อยู่ใน repo: รูปการ์ด, เพลง/เสียง และฐานข้อมูลการ์ดของ EDOPro (ดาวน์โหลดจาก EDOPro / repositories ของเกมเอง)

## ไฟล์พร้อมเล่น

ดูที่หน้า **Releases** ของ repo นี้:
- `YEdo-Mod-Pro.exe` — ตัวเกม Windows (ใช้แทน exe ในโฟลเดอร์เกม EDOPro / YEdo Mod Pro เดิม แล้วใส่โฟลเดอร์ `data/` ลงไป)
- `YEdo-Mod-Pro.apk` — ตัวติดตั้ง Android

## สร้างเอง

**Windows (exe):** ใช้ premake5 + MinGW/Visual Studio แบบเดียวกับ EDOPro (`windows/premake5.lua`, target `ygoprodll`)

**Android (apk):**
1. ติดตั้ง Android Studio (มี SDK + Java) และ Git
2. คัดลอก `windows/gframe` และ `windows/ocgcore` ไปไว้ใน `android/deps/edopro/` แล้ว zip โฟลเดอร์ `android/` (ยกเว้น `build-android.bat`) เป็น `yedo-android-overlay.zip`
3. วาง `build-android.bat` + zip ไว้ใน `<โฟลเดอร์เกม>\_android\` แล้วดับเบิลคลิก `build-android.bat` — สคริปต์จะโหลดโปรเจกต์ edopro-android + NDK r21e, คอมไพล์ และได้ `YEdo-Mod-Pro.apk`

## License

ซอร์สโค้ดอิงจาก EDOPro (AGPL-3.0) — ดู `windows/LICENSE` และ `windows/COPYING`
Yu-Gi-Oh! เป็นเครื่องหมายการค้าของ Konami — โปรเจกต์นี้เป็นม็อดแฟนเมด ไม่เกี่ยวข้องกับ Konami
