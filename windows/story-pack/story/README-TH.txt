EDOPro Story Mode
=================

วิธีติดตั้ง
-----------
1. ก๊อปทุกอย่างในแพ็คนี้ไปวางในโฟลเดอร์ EDOPro ที่ติดตั้งอยู่ (โฟลเดอร์เดียวกับ EDOPro.exe)
   - EDOPro-Story.exe          -> ตัวเกมที่มีโหมดเนื้อเรื่อง
   - story\                    -> เนื้อเรื่อง เด็คคู่ต่อสู้ ฟอนต์ไทย
   - WindBot\Dialogs\story_*.json -> บทพูดของบอทระหว่างดวล
2. เปิดเกมด้วย EDOPro-Story.exe (EDOPro.exe ตัวเดิมยังใช้ได้ตามปกติ)
3. เมนูหลัก -> กด "Story Mode"

วิธีเล่น
--------
- เลือกบททางซ้าย เลือกเด็คของตัวเองด้านล่าง แล้วกด "เริ่มดวล"
- ดูบทพูดก่อนดวล -> เกมจะเปิดห้อง เรียกบอท กด Ready และ Start ให้อัตโนมัติ
- ชนะ = ผ่านบทและปลดล็อกบทถัดไป (บันทึกใน story\progress.json)

วิธีเพิ่ม/แก้บทเอง (ไม่ต้อง build ใหม่)
--------------------------------------
1. เอาเด็คจากเว็บ (เช่น ygoprodeck.com) กด Export เป็นไฟล์ .ydk
   วางไว้ใน story\decks\
2. เปิด story\story.json แล้วเพิ่มบทใหม่ในรายการ "chapters":

  {
    "id": "ch6",
    "title": "ชื่อบท",
    "unlock_after": "ch5",              <- ต้องผ่านบทไหนก่อน (ลบทิ้ง = เปิดให้เล่นเลย)
    "description": "คำอธิบายบท",
    "opponent": "ชื่อคู่ต่อสู้",
    "deck": "ชื่อไฟล์.ydk",
    "engine": "Lucky",                  <- AI ที่ใช้เล่น (ดูด้านล่าง)
    "dialog": "story_kaito",            <- บทพูดระหว่างดวล (ไม่ใส่ก็ได้)
    "start_lp": 8000,
    "intro": [ { "speaker": "ชื่อ", "text": "ประโยค" } ],
    "win":   [ { "speaker": "ชื่อ", "text": "ประโยคตอนชนะ" } ],
    "lose":  [ { "speaker": "ชื่อ", "text": "ประโยคตอนแพ้" } ]
  }

3. ปิด-เปิดหน้า Story Mode ใหม่ เกมจะโหลด story.json ใหม่ทันที

engine (AI)
-----------
- "Lucky"  = AI ทั่วไป เล่นได้ทุกเด็คแต่ไม่ค่อยฉลาด
- ถ้าเด็คตรงกับ AI ที่มีอยู่ ให้ใช้ชื่อ "deck" จาก WindBot\bots.json จะเล่นเก่งกว่ามาก
  เช่น "Blue-Eyes", "DarkMagician", "Dragun", "SkyStriker", "SacredBeast",
  "Salamangreat", "Trickstar", "Zoodiac", "Lightsworn" ฯลฯ

หมายเหตุ
--------
- บทพูดใน story.json เป็น UTF-8 (เปิดด้วย Notepad / VS Code ได้)
- ถ้าภาษาไทยขึ้นเป็นกล่อง ให้ตรวจว่ามีไฟล์ story\fonts\NotoSansThai-Regular.ttf
- ฟอนต์ Noto Sans Thai ใช้สัญญาอนุญาต SIL OFL (ดู story\fonts\OFL.txt)
