// =============================================================
//  夜空時計 (yozoratokei) — M5Stack CoreS3
//  小さなプラネタリウム: 空にかざすと、その方向の星座・恒星・月が浮かぶ
//  月時計モード: DinBase据え置き用。月齢・満ち欠け・月食・流星群 (iroirotokei移植)
//
//  操作: タップ = プラネタリウム ⇄ 月時計 / 長押し(1秒) = コンパス再補正
//  時刻: RTC↔内部クロック + Wi-Fi NTP。シリアル(115200)から
//        "N"=NTP同期 "D 2026-07-20 21:30:00" "T 21:30:00" "V"=デバッグ表示
// =============================================================

#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include "star_catalog.h"

// ---------------- 観測地・コンパス設定 ----------------
static constexpr float OBS_LAT_DEG = 35.7f;   // 観測地: 東京 (北緯)
static constexpr float OBS_LON_DEG = 139.7f;  // 観測地: 東京 (東経)
static constexpr float MAG_DECLINATION_DEG = -8.0f; // 磁気偏角: 日本(東京近辺)は約8°西偏 → -8
static constexpr float LPF_ALPHA           = 0.15f; // センサー平滑化
static constexpr uint32_t CALIB_SECONDS    = 15;    // 8の字キャリブレーション時間
static constexpr uint8_t  CALIB_STRENGTH   = 64;

// 実機で方位・仰角・画面の向きが合わないときはここで符号を調整する
static constexpr float SIGN_AX = 1, SIGN_AY = 1, SIGN_AZ = 1;
static constexpr float SIGN_MX = 1, SIGN_MY = 1, SIGN_MZ = 1;
static constexpr float SCREEN_SX = 1, SCREEN_SY = 1;

// 投影: 焦点距離[px]。300 ≒ 横56°の視野
static constexpr float FOCAL_PX = 300.0f;

// ---------------- 色 (夜空トーン) ----------------
static constexpr uint16_t COL_BG      = 0x0021; // ほぼ黒の濃紺
static constexpr uint16_t COL_TEXT    = 0xCE9F; // 淡い星色
static constexpr uint16_t COL_ACCENT  = 0xFEA0; // 月色 (amber)
static constexpr uint16_t COL_WARN    = 0xF9E7; // 赤系
static constexpr uint16_t COL_DIM     = 0x630C; // 補助テキスト
static constexpr uint16_t COL_LINE    = 0x2150; // 星座線 (暗い藍)
static constexpr uint16_t COL_CNAME   = 0x8C71; // 星座名 (くすんだ金)
static constexpr uint16_t COL_SNAME   = 0x4D93; // 恒星名 (暗い水色)
static constexpr uint16_t COL_HORIZON = 0x29A6; // 地平線
static constexpr uint16_t COL_RING    = 0x39C7;

// ---------------- 状態 ----------------
struct Vec3 { float x, y, z; };

static M5Canvas canvas(&M5.Display);
static bool     spriteOk = false;

enum class Mode { Calibrating, Main, MoonClock };
static Mode     mode            = Mode::Main;
static Mode     modeBeforeCalib = Mode::Main;
static uint32_t calibEndMs      = 0;
static uint32_t lastCalibKickMs = 0;
static bool     debugView       = false;
static bool     imuOk           = false;
static bool     rtcOk           = false;

static Vec3 lpAccel, lpMag;
static bool haveFilter = false;
static bool haveAzEl   = false;
static float azimuthDeg = 0, elevationDeg = 0;
// 世界座標軸(東・北・天頂)をデバイス座標系で表したもの (姿勢そのもの)
static Vec3 gEast, gNorth, gUp;

// 星の投影結果 (毎フレーム更新)
static int16_t starX[STAR_COUNT], starY[STAR_COUNT];
static uint8_t starVis[STAR_COUNT]; // 0=不可視 1=画面外だが線描画に使える 2=画面内
static float   sinDecT[STAR_COUNT], cosDecT[STAR_COUNT];

static const char* DIR16[16] = {
  "北", "北北東", "北東", "東北東", "東", "東南東", "南東", "南南東",
  "南", "南南西", "南西", "西南西", "西", "西北西", "北西", "北北西"
};

static void startCalibration(); // 前方宣言 (シリアルコマンドから呼ぶ)

// ---------------- サウンド (iroirotokei譲りのペンタトニック星空音) ----------------
// ノンブロッキングの簡易シーケンサ: loop() から updateSound() を回す
static Preferences prefs;
static bool soundOn = true;
static bool spkOk   = false;

struct ToneStep { uint16_t freq; uint16_t durMs; uint16_t gapMs; };
static ToneStep sndSeq[6];
static int      sndLen = 0, sndPos = 0;
static uint32_t sndNextMs = 0;

static void playSeq(const ToneStep* steps, int n) {
  if (!soundOn || !spkOk) return;
  if (n > 6) n = 6;
  memcpy(sndSeq, steps, n * sizeof(ToneStep));
  sndLen = n;
  sndPos = 0;
  sndNextMs = millis();
}

static void updateSound() {
  if (sndPos >= sndLen || millis() < sndNextMs) return;
  const ToneStep& s = sndSeq[sndPos];
  M5.Speaker.tone(s.freq, s.durMs);
  sndNextMs = millis() + s.durMs + s.gapMs;
  ++sndPos;
}

// ペンタトニック(五音)音階 2オクターブ分。夜空のような浮遊感のある音階。
static const uint16_t PENTA[] = {523, 587, 659, 784, 880, 1047, 1175, 1319, 1568, 1760};

// 星のまたたき (モード切替)
static void soundModeSwitch() {
  static const ToneStep s[] = {{1568, 50, 25}, {2093, 80, 0}};
  playSeq(s, 2);
}
// 遠い鐘のような上昇アルペジオ (キャリブレーション完了 / 月時計の毎正時)
static void soundChime() {
  static const ToneStep s[] = {{1047, 130, 70}, {1568, 130, 70}, {2093, 240, 0}};
  playSeq(s, 3);
}
static void soundToggleOn() {
  static const ToneStep s[] = {{784, 45, 25}, {1568, 70, 0}};
  playSeq(s, 2);
}
static void soundToggleOff() { // オフにする直前の低い一音 (大地のイメージ)
  static const ToneStep s[] = {{392, 80, 0}};
  playSeq(s, 1);
}

// 星座を中央にとらえたときのモチーフ (星座ごとに違う3音)
static void soundConstellation(int c) {
  int i1 = c % 10, i2 = (i1 + 2) % 10, i3 = (i1 + 4) % 10;
  ToneStep s[3] = {{PENTA[i1], 100, 50}, {PENTA[i2], 100, 50}, {PENTA[i3], 180, 0}};
  playSeq(s, 3);
}
// 明るい星が中央に来たときのきらめき (高音域ペンタトニック)
static void soundSparkle() {
  ToneStep s[1] = {{uint16_t(PENTA[5 + (int)(esp_random() % 5)] * 2), 45, 0}};
  playSeq(s, 1);
}

static void toggleSound() {
  if (soundOn) { soundToggleOff(); soundOn = false; } // 鳴らしてからOFF
  else         { soundOn = true; soundToggleOn(); }
  prefs.putBool("sound", soundOn);
}

// 星座発見アナウンス: 中央の星座が0.5秒安定したら一度だけ鳴らす
static int      lastCenterC   = -2;
static int      announcedC    = -1;
static uint32_t centerSinceMs = 0;
static void announceConstellation(int c) {
  uint32_t now = millis();
  if (c != lastCenterC) { lastCenterC = c; centerSinceMs = now; return; }
  if (c >= 0 && c != announcedC && now - centerSinceMs > 500) {
    announcedC = c;
    soundConstellation(c);
  }
  if (c < 0 && now - centerSinceMs > 3000) announcedC = -1; // 外れてしばらくしたら再び鳴らせる
}

// ---------------- ベクトル演算 ----------------
static Vec3 cross(const Vec3& a, const Vec3& b) {
  Vec3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
  return r;
}
static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vlen(const Vec3& v) { return sqrtf(dot(v, v)); }
static bool normalize(Vec3& v) {
  float l = vlen(v);
  if (l < 1e-6f) return false;
  v.x /= l; v.y /= l; v.z /= l;
  return true;
}

// ---------------- 時計 (iroirotokei から移植) ----------------
static void systemTimeFromTm(struct tm& t) {
  time_t epoch = mktime(&t);
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
}

static void writeRtc(struct tm& t) {
  if (!rtcOk) return;
  mktime(&t); // tm_wday を正規化
  m5::rtc_datetime_t dt;
  dt.date.year    = t.tm_year + 1900;
  dt.date.month   = t.tm_mon + 1;
  dt.date.date    = t.tm_mday;
  dt.date.weekDay = t.tm_wday;
  dt.time.hours   = t.tm_hour;
  dt.time.minutes = t.tm_min;
  dt.time.seconds = t.tm_sec;
  M5.Rtc.setDateTime(dt);
}

static void getNow(struct tm& out) {
  time_t now = time(nullptr);
  localtime_r(&now, &out);
}

// ビルド時刻 (__DATE__ = "Jul 20 2026") を tm に変換
static void buildTimeTm(struct tm& t) {
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int day = 1, year = 2026, hh = 0, mm = 0, ss = 0;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  t = {};
  const char* p = strstr(months, mon);
  t.tm_mon  = p ? int(p - months) / 3 : 0;
  t.tm_mday = day;
  t.tm_year = year - 1900;
  t.tm_hour = hh;
  t.tm_min  = mm;
  t.tm_sec  = ss;
  mktime(&t);
}

static void setupClock() {
  setenv("TZ", "JST-9", 1);
  tzset();
  rtcOk = M5.Rtc.isEnabled();
  struct tm bt;
  buildTimeTm(bt);
  if (rtcOk) {
    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year >= 2023) {
      struct tm t = {};
      t.tm_year = dt.date.year - 1900;
      t.tm_mon  = dt.date.month - 1;
      t.tm_mday = dt.date.date;
      t.tm_hour = dt.time.hours;
      t.tm_min  = dt.time.minutes;
      t.tm_sec  = dt.time.seconds;
      systemTimeFromTm(t);
    } else {
      writeRtc(bt); // RTC未設定ならビルド時刻を書く
      systemTimeFromTm(bt);
    }
  } else {
    systemTimeFromTm(bt);
  }
}

// --- Wi-Fi + NTP 自動時刻同期 (保存済みWi-Fi設定で接続、失敗したら諦める) ---
enum SyncState : uint8_t { SY_IDLE, SY_CONNECTING, SY_WAITTIME };
static SyncState syncState   = SY_IDLE;
static uint32_t  syncStartMs = 0;
static bool      ntpSynced   = false;

static void startNtpSync() {
  if (syncState != SY_IDLE) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  syncState   = SY_CONNECTING;
  syncStartMs = millis();
}

static void stopWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  syncState = SY_IDLE;
}

static void updateNtpSync() {
  if (syncState == SY_IDLE) return;
  if (syncState == SY_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      configTzTime("JST-9", "ntp.jst.mfeed.ad.jp", "pool.ntp.org", "time.google.com");
      syncState   = SY_WAITTIME;
      syncStartMs = millis();
    } else if (millis() - syncStartMs > 15000) {
      stopWifi();
    }
    return;
  }
  struct tm t;
  getNow(t);
  if (t.tm_year + 1900 >= 2024) {
    writeRtc(t);
    stopWifi();
    ntpSynced = true;
    Serial.println("OK: NTP time synced");
  } else if (millis() - syncStartMs > 15000) {
    stopWifi();
  }
}

// シリアルからの操作  "N"=NTP同期  "D 2026-07-20 21:30:00"  "T 21:30:00"  "V"=デバッグ
static void handleSerialTimeSet() {
  static String line;
  bool complete = false;
  // available()の値ぶんだけに制限し、read()失敗でも必ず抜ける (USB CDC未接続時の無限ループ対策)
  int avail = Serial.available();
  while (avail-- > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\r' || ch == '\n') {
      if (line.length() > 0) { complete = true; break; }
    } else if (line.length() < 40) {
      line += (char)ch;
    }
  }
  if (!complete) return;
  String work = line;
  line = "";
  work.trim();
  if (work.length() < 1) return;

  if (work[0] == 'N' || work[0] == 'n') {
    Serial.println("OK: starting NTP sync...");
    startNtpSync();
    return;
  }
  if (work[0] == 'V' || work[0] == 'v') {
    debugView = !debugView;
    Serial.printf("OK: debug %s\n", debugView ? "on" : "off");
    return;
  }
  if (work[0] == 'M' || work[0] == 'm') { // タッチ代替: モード切替
    if (mode != Mode::Calibrating) mode = (mode == Mode::Main) ? Mode::MoonClock : Mode::Main;
    Serial.println("OK: mode switched");
    return;
  }
  if (work[0] == 'C' || work[0] == 'c') { // タッチ代替: 再キャリブレーション
    startCalibration();
    Serial.println("OK: calibration started");
    return;
  }
  if (work[0] == 'S' || work[0] == 's') { // 音ON/OFF
    toggleSound();
    Serial.printf("OK: sound %s\n", soundOn ? "on" : "off");
    return;
  }
  struct tm t;
  getNow(t);
  int y, mo, d, hh, mm, ss;
  bool ok = false;
  if ((work[0] == 'D' || work[0] == 'd') &&
      sscanf(work.c_str() + 1, " %d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) == 6) {
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
    ok = true;
  } else if ((work[0] == 'T' || work[0] == 't') &&
             sscanf(work.c_str() + 1, " %d:%d:%d", &hh, &mm, &ss) == 3) {
    t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
    ok = true;
  }
  if (ok) {
    systemTimeFromTm(t);
    writeRtc(t);
    Serial.println("OK: time set");
  } else {
    Serial.println("ERR: use N / V / D yyyy-mm-dd hh:mm:ss / T hh:mm:ss");
  }
}

// ---------------- 月の計算 (iroirotokei移植 + 位置計算) ----------------
// 月の位相角 0°=新月 180°=満月 (Meeus簡易式、誤差は数時間程度)
static float moonPhaseDeg() {
  double jd = time(nullptr) / 86400.0 + 2440587.5; // UTC→ユリウス日
  double T  = (jd - 2451545.0) / 36525.0;
  double Mp = fmod(134.963 + 477198.8676 * T, 360.0) * 0.0174533;
  double M  = fmod(357.529 + 35999.0503  * T, 360.0) * 0.0174533;
  double lm = fmod(218.316 + 481267.8813 * T, 360.0) + 6.289 * sin(Mp);
  double ls = fmod(280.459 + 36000.7698  * T, 360.0) + 1.915 * sin(M);
  double d  = fmod(lm - ls, 360.0);
  if (d < 0) d += 360.0;
  return (float)d;
}

static const char* moonName(float age) {
  if (age < 1.5f)  return "新月";
  if (age < 5.5f)  return "三日月";
  if (age < 9.0f)  return "上弦の月";
  if (age < 13.0f) return "十三夜";
  if (age < 16.2f) return "満月";
  if (age < 20.0f) return "十六夜";
  if (age < 24.0f) return "下弦の月";
  if (age < 28.0f) return "二十六夜";
  return "新月";
}

// 月の赤経・赤緯 (簡易式: 黄経黄緯→赤道座標。誤差1°程度、表示用途には十分)
static void moonEquatorial(float& raDeg, float& decDeg) {
  double jd = time(nullptr) / 86400.0 + 2440587.5;
  double T  = (jd - 2451545.0) / 36525.0;
  double Mp = fmod(134.963 + 477198.8676 * T, 360.0) * 0.0174533; // 平均近点角
  double F  = fmod(93.272  + 483202.0175 * T, 360.0) * 0.0174533; // 緯度引数
  double lon = fmod(218.316 + 481267.8813 * T, 360.0) + 6.289 * sin(Mp); // 黄経
  double lat = 5.128 * sin(F);                                           // 黄緯
  double eps = 23.4393 * 0.0174533;
  double lr = lon * 0.0174533, br = lat * 0.0174533;
  double x = cos(br) * cos(lr);
  double y = cos(eps) * cos(br) * sin(lr) - sin(eps) * sin(br);
  double z = sin(eps) * cos(br) * sin(lr) + cos(eps) * sin(br);
  raDeg = (float)(atan2(y, x) * 57.29578);
  if (raDeg < 0) raDeg += 360.0f;
  decDeg = (float)(asin(z) * 57.29578);
}

// --- 月食テーブル (日本で見られるもの、JSTの食の最大時刻) ---
// 2026-03-03 は国立天文台の公表値 (欠け始め18:50 皆既20:04-21:03 最大20:33 終了22:18)。
struct EclipseEv { int16_t y; int8_t mo, d, hh, mm; bool total; };
static const EclipseEv ECLIPSES[] = {
  {2026,  3,  3, 20, 33, true },
  {2028,  7,  7,  3, 20, false},
  {2029,  1,  1,  1, 52, true },
  {2030,  6, 16,  3, 33, false},
  {2032,  4, 26,  0, 13, true },
  {2032, 10, 19, 19,  2, true },
};

static time_t eclipseEpoch(const EclipseEv& e) {
  struct tm t = {};
  t.tm_year = e.y - 1900;
  t.tm_mon  = e.mo - 1;
  t.tm_mday = e.d;
  t.tm_hour = e.hh;
  t.tm_min  = e.mm;
  return mktime(&t);
}

// 次の月食 (終了済みはスキップ)。diffSec = 食の最大までの秒 (負=最大は過ぎた)
static const EclipseEv* nextEclipse(long* diffSec) {
  time_t now = time(nullptr);
  for (const auto& e : ECLIPSES) {
    long d = (long)(eclipseEpoch(e) - now);
    if (d > -4 * 3600) {
      if (diffSec) *diffSec = d;
      return &e;
    }
  }
  return nullptr;
}

// いま月食の最中か: 0=なし 1=部分食 2=皆既中
static int eclipseNow() {
  long d;
  const EclipseEv* e = nextEclipse(&d);
  if (!e) return 0;
  long a = labs(d);
  if (e->total && a <= 30 * 60) return 2;
  if (a <= 105 * 60) return 1;
  return 0;
}

// --- 流星群テーブル (毎年の極大日, span=前後この日数は「活動中」) ---
struct MeteorEv { int8_t mo, d; const char* name; uint8_t zhr; int8_t span; };
static const MeteorEv METEORS[] = {
  { 1,  4, "しぶんぎ座流星群",   110, 2},
  { 4, 22, "4月こと座流星群",     18, 2},
  { 5,  6, "みずがめ座η流星群",   50, 3},
  { 7, 30, "みずがめ座δ流星群",   25, 3},
  { 8, 13, "ペルセウス座流星群",  100, 4},
  {10, 21, "オリオン座流星群",    20, 3},
  {11, 17, "しし座流星群",        15, 2},
  {12, 14, "ふたご座流星群",     150, 3},
  {12, 22, "こぐま座流星群",      10, 2},
};

static int meteorDiffDays(const MeteorEv& m, const struct tm& t) {
  struct tm p = t;
  p.tm_mon  = m.mo - 1;
  p.tm_mday = m.d;
  p.tm_hour = 12; p.tm_min = 0; p.tm_sec = 0;
  mktime(&p);
  int diff = p.tm_yday - t.tm_yday;
  if (diff > 182)  diff -= 365;
  if (diff < -182) diff += 365;
  return diff;
}

static const MeteorEv* meteorActive(int* daysToPeak) {
  struct tm t;
  getNow(t);
  for (const auto& m : METEORS) {
    int diff = meteorDiffDays(m, t);
    if (abs(diff) <= m.span) {
      if (daysToPeak) *daysToPeak = diff;
      return &m;
    }
  }
  return nullptr;
}

static const MeteorEv* meteorNext(int* days) {
  struct tm t;
  getNow(t);
  int best = 999;
  const MeteorEv* bev = nullptr;
  for (const auto& m : METEORS) {
    int diff = meteorDiffDays(m, t);
    if (diff < 0) diff += 365;
    if (diff < best) { best = diff; bev = &m; }
  }
  if (days) *days = best;
  return bev;
}

// 月を描く (欠け際まで正確な形。皆既中は赤銅色) — iroirotokeiから移植しcanvas化
static void drawMoonDisk(int cx, int cy, int R, float d, int ecl) {
  uint16_t lit  = (ecl == 2) ? canvas.color565(190, 70, 40) : canvas.color565(245, 240, 222);
  uint16_t dark = canvas.color565(28, 30, 46);
  float c = cosf(d * 0.0174533f);
  for (int y = -R; y <= R; ++y) {
    int w = (int)sqrtf((float)(R * R - y * y));
    if (w <= 0) continue;
    canvas.drawFastHLine(cx - w, cy + y, 2 * w + 1, dark);
    int x0, x1;
    if (d <= 180.f) { x0 = (int)(w * c);  x1 = w; }             // 満ちていく: 右から光る
    else            { x0 = -w;            x1 = (int)(-w * c); } // 欠けていく: 左が残る
    if (x1 > x0) canvas.drawFastHLine(cx + x0, cy + y, x1 - x0 + 1, lit);
  }
}

// 暈(かさ): 月のまわりのぼんやりした光の輪
static void drawMoonHalo(int cx, int cy, int R) {
  canvas.drawCircle(cx, cy, R + 6,  canvas.color565(38, 40, 66));
  canvas.drawCircle(cx, cy, R + 12, canvas.color565(26, 28, 50));
  canvas.drawCircle(cx, cy, R + 18, canvas.color565(16, 18, 38));
}

// ---------------- 姿勢の計算 (傾き補償コンパス) ----------------
static bool computeAttitude() {
  auto d = M5.Imu.getImuData();
  Vec3 a = { SIGN_AX * d.accel.x, SIGN_AY * d.accel.y, SIGN_AZ * d.accel.z };
  Vec3 m = { SIGN_MX * d.mag.x,   SIGN_MY * d.mag.y,   SIGN_MZ * d.mag.z   };
  if (vlen(m) < 1e-3f || vlen(a) < 0.3f) return false;

  if (!haveFilter) {
    lpAccel = a; lpMag = m; haveFilter = true;
  } else {
    lpAccel.x += LPF_ALPHA * (a.x - lpAccel.x);
    lpAccel.y += LPF_ALPHA * (a.y - lpAccel.y);
    lpAccel.z += LPF_ALPHA * (a.z - lpAccel.z);
    lpMag.x   += LPF_ALPHA * (m.x - lpMag.x);
    lpMag.y   += LPF_ALPHA * (m.y - lpMag.y);
    lpMag.z   += LPF_ALPHA * (m.z - lpMag.z);
  }

  Vec3 up = lpAccel;
  if (!normalize(up)) return false;
  Vec3 east = cross(lpMag, up); // (磁)北×上 = 東
  if (!normalize(east)) return false;
  Vec3 north = cross(up, east);

  // 磁気偏角の補正: 水平面内で回転して真北に合わせる
  float dr = MAG_DECLINATION_DEG * DEG_TO_RAD;
  float cd = cosf(dr), sd = sinf(dr);
  Vec3 tn = { north.x * cd - east.x * sd, north.y * cd - east.y * sd, north.z * cd - east.z * sd };
  Vec3 te = { east.x * cd + north.x * sd, east.y * cd + north.y * sd, east.z * cd + north.z * sd };
  gNorth = tn; gEast = te; gUp = up;

  float az = atan2f(-gEast.z, -gNorth.z) * RAD_TO_DEG;
  if (az < 0) az += 360.0f;
  float sinEl = constrain(-up.z, -1.0f, 1.0f);
  azimuthDeg   = az;
  elevationDeg = asinf(sinEl) * RAD_TO_DEG;
  haveAzEl = true;
  return true;
}

// ---------------- 天球 → 画面投影 ----------------
static float localSiderealDeg() {
  double d = (double)(time(nullptr) - 946728000LL) / 86400.0; // J2000からの日数
  double lst = fmod(280.46061837 + 360.98564736629 * d + OBS_LON_DEG, 360.0);
  if (lst < 0) lst += 360.0;
  return (float)lst;
}

// 世界座標(東,北,天頂)の方向ベクトルを画面座標へ。戻り値: 0=不可視 1=画面外 2=画面内
static uint8_t projectENU(float E, float N, float U, int16_t& outX, int16_t& outY) {
  float vx = E * gEast.x + N * gNorth.x + U * gUp.x;
  float vy = E * gEast.y + N * gNorth.y + U * gUp.y;
  float vz = E * gEast.z + N * gNorth.z + U * gUp.z;
  float f = -vz; // 「かざす」方向(-Z)の成分
  if (f < 0.20f) return 0;
  int x = 160 + (int)(SCREEN_SX * vx / f * FOCAL_PX);
  int y = 120 - (int)(SCREEN_SY * vy / f * FOCAL_PX);
  if (x < -200 || x > 520 || y < -200 || y > 440) return 0;
  outX = (int16_t)x; outY = (int16_t)y;
  return (x >= 0 && x < 320 && y >= 0 && y < 240) ? 2 : 1;
}

// 赤経・赤緯 → 画面座標
static uint8_t projectEquatorial(float raDeg, float decDeg, float lst,
                                 float sinLat, float cosLat, int16_t& x, int16_t& y) {
  float H = (lst - raDeg) * DEG_TO_RAD; // 時角
  float sinDec = sinf(decDeg * DEG_TO_RAD), cosDec = cosf(decDeg * DEG_TO_RAD);
  float sinH = sinf(H), cosH = cosf(H);
  float E = -cosDec * sinH;
  float N = cosLat * sinDec - sinLat * cosDec * cosH;
  float U = sinLat * sinDec + cosLat * cosDec * cosH;
  return projectENU(E, N, U, x, y);
}

static void projectStars() {
  float lst = localSiderealDeg();
  float sinLat = sinf(OBS_LAT_DEG * DEG_TO_RAD);
  float cosLat = cosf(OBS_LAT_DEG * DEG_TO_RAD);
  for (int i = 0; i < STAR_COUNT; ++i) {
    float H = (lst - STARS[i].ra) * DEG_TO_RAD;
    float sinH = sinf(H), cosH = cosf(H);
    float E = -cosDecT[i] * sinH;
    float N = cosLat * sinDecT[i] - sinLat * cosDecT[i] * cosH;
    float U = sinLat * sinDecT[i] + cosLat * cosDecT[i] * cosH;
    starVis[i] = projectENU(E, N, U, starX[i], starY[i]);
  }
}

// ---------------- プラネタリウム描画 ----------------
static void drawHorizon() {
  int16_t px = 0, py = 0;
  uint8_t pv = 0;
  for (int az = 0; az <= 360; az += 10) {
    float r = az * DEG_TO_RAD;
    int16_t x, y;
    uint8_t v = projectENU(sinf(r), cosf(r), 0.0f, x, y);
    if (v && pv) canvas.drawLine(px, py, x, y, COL_HORIZON);
    px = x; py = y; pv = v;
  }
  static const char* CARD[4] = { "北", "東", "南", "西" };
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(middle_center);
  for (int i = 0; i < 4; ++i) {
    float r = i * 90 * DEG_TO_RAD;
    int16_t x, y;
    if (projectENU(sinf(r), cosf(r), 0.03f, x, y) == 2) {
      canvas.setTextColor((i == 0) ? COL_WARN : COL_HORIZON);
      canvas.drawString(CARD[i], x, y - 10);
    }
  }
}

static void drawStarGlyph(int x, int y, int8_t mag10, int idx) {
  float tw = 0.80f + 0.20f * sinf(millis() * 0.004f + idx * 1.7f);
  uint8_t b = (uint8_t)(255 * tw);
  uint16_t bright = canvas.color565(b, b, 255);
  uint16_t mid    = canvas.color565(b / 2, b / 2, 170);
  uint16_t dim    = canvas.color565(b / 5, b / 5, 90);

  if (idx == S_PLEIADES) { // すばるは小さな星の集まりとして描く
    canvas.drawPixel(x, y, bright);
    canvas.drawPixel(x - 3, y - 2, mid);
    canvas.drawPixel(x + 2, y - 3, mid);
    canvas.drawPixel(x + 3, y + 1, mid);
    canvas.drawPixel(x - 2, y + 3, mid);
    canvas.drawPixel(x + 1, y + 2, dim);
    return;
  }
  if (mag10 <= 5) {
    canvas.drawCircle(x, y, 4, dim);
    canvas.fillCircle(x, y, 2, mid);
    canvas.drawPixel(x, y, bright);
    canvas.drawFastHLine(x - 6, y, 13, dim);
    canvas.drawFastVLine(x, y - 6, 13, dim);
  } else if (mag10 <= 15) {
    canvas.drawCircle(x, y, 3, dim);
    canvas.fillCircle(x, y, 1, mid);
    canvas.drawPixel(x, y, bright);
  } else if (mag10 <= 25) {
    canvas.fillCircle(x, y, 1, dim);
    canvas.drawPixel(x, y, mid);
  } else if (mag10 <= 35) {
    canvas.drawPixel(x, y, mid);
  } else {
    canvas.drawPixel(x, y, dim);
  }
}

// 空に浮かぶ月 (位置は月の実位置、位相つき小さめディスク+暈)
static void drawSkyMoon() {
  float lst = localSiderealDeg();
  float sinLat = sinf(OBS_LAT_DEG * DEG_TO_RAD);
  float cosLat = cosf(OBS_LAT_DEG * DEG_TO_RAD);
  float ra, dec;
  moonEquatorial(ra, dec);
  int16_t x, y;
  if (projectEquatorial(ra, dec, lst, sinLat, cosLat, x, y) != 2) return;
  float d = moonPhaseDeg();
  int ecl = eclipseNow();
  drawMoonHalo(x, y, 9);
  drawMoonDisk(x, y, 9, d, ecl);
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(COL_ACCENT);
  float age = d / 360.f * 29.5306f;
  char buf[24];
  snprintf(buf, sizeof(buf), "月齢%.0f", age);
  canvas.drawString(buf, x + 14, y - 14);
}

static void drawSky() {
  // 星座線
  for (int c = 0; c < CONSTELLATION_COUNT; ++c) {
    const Constellation& cc = CONSTELLATIONS[c];
    for (int l = 0; l < cc.nLines; ++l) {
      uint8_t a = cc.lines[l][0], b = cc.lines[l][1];
      if (starVis[a] && starVis[b] && (starVis[a] == 2 || starVis[b] == 2)) {
        canvas.drawLine(starX[a], starY[a], starX[b], starY[b], COL_LINE);
      }
    }
  }
  // 星
  for (int i = 0; i < STAR_COUNT; ++i) {
    if (starVis[i] == 2) drawStarGlyph(starX[i], starY[i], STARS[i].mag10, i);
  }
  // 月
  drawSkyMoon();
  // 有名な星の名前
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(COL_SNAME);
  for (int i = 0; i < STAR_NAME_COUNT; ++i) {
    uint8_t s = STAR_NAMES[i].idx;
    if (starVis[s] == 2) canvas.drawString(STAR_NAMES[i].name, starX[s] + 7, starY[s] - 11);
  }
  // 星座名: 見えている星座の中心に表示。画面中央に最も近い星座は図鑑ふうに大きく
  int bestC = -1;
  float bestDist = 110.0f;
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(middle_center);
  for (int c = 0; c < CONSTELLATION_COUNT; ++c) {
    const Constellation& cc = CONSTELLATIONS[c];
    int sx = 0, sy = 0, n = 0;
    for (int l = 0; l < cc.nLines; ++l) {
      for (int e = 0; e < 2; ++e) {
        uint8_t s = cc.lines[l][e];
        if (starVis[s] == 2) { sx += starX[s]; sy += starY[s]; ++n; }
      }
    }
    if (n < 3) continue;
    int cx = sx / n, cy = sy / n;
    canvas.setTextColor(COL_CNAME);
    canvas.drawString(cc.name, cx, cy);
    float dist = sqrtf(float(cx - 160) * (cx - 160) + float(cy - 120) * (cy - 120));
    if (dist < bestDist) { bestDist = dist; bestC = c; }
  }
  if (bestC >= 0) {
    canvas.setFont(&fonts::lgfxJapanGothic_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COL_ACCENT);
    canvas.drawString(CONSTELLATIONS[bestC].name, 160, 22);
  }
  announceConstellation(bestC);

  // 有名な星が中央マーカー近くに来たら、きらめき音
  static uint8_t  lastSparkleStar = 255;
  static uint32_t lastSparkleMs   = 0;
  int found = -1;
  for (int i = 0; i < STAR_NAME_COUNT; ++i) {
    uint8_t s = STAR_NAMES[i].idx;
    if (starVis[s] != 2) continue;
    int dx = starX[s] - 160, dy = starY[s] - 120;
    if (dx * dx + dy * dy < 22 * 22) { found = s; break; }
  }
  if (found >= 0) {
    if ((uint8_t)found != lastSparkleStar && millis() - lastSparkleMs > 1200) {
      lastSparkleStar = (uint8_t)found;
      lastSparkleMs = millis();
      soundSparkle();
    }
  } else {
    lastSparkleStar = 255;
  }
}

static void drawOverlay() {
  struct tm t;
  getNow(t);

  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(COL_TEXT);
  int dirIdx = ((int)roundf(azimuthDeg / 22.5f)) % 16;
  int el = (int)roundf(elevationDeg);
  canvas.drawString(String(DIR16[dirIdx]) + " " + String((int)roundf(azimuthDeg)) + "°", 6, 4);
  canvas.setTextDatum(top_right);
  canvas.drawString((el >= 0 ? "+" : "") + String(el) + "°", 314, 4);

  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(bottom_left);
  if (t.tm_year + 1900 < 2024) {
    canvas.setTextColor(COL_WARN);
    canvas.drawString("時刻未設定 (シリアルからNで同期)", 6, 236);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d %02d:%02d", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String(buf) + (ntpSynced ? " NTP" : ""), 6, 236);
  }
  canvas.setTextDatum(bottom_right);
  canvas.setTextColor(COL_DIM);
  canvas.drawString("タップ:月時計", 314, 236);

  canvas.drawCircle(160, 120, 5, COL_DIM);
  canvas.drawPixel(160, 120, COL_TEXT);

  if (debugView) {
    auto d = M5.Imu.getImuData();
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.setCursor(6, 190);
    canvas.printf("A %+.2f %+.2f %+.2f", d.accel.x, d.accel.y, d.accel.z);
    canvas.setCursor(6, 200);
    canvas.printf("M %+6.1f %+6.1f %+6.1f", d.mag.x, d.mag.y, d.mag.z);
    Vec3 mv = { d.mag.x, d.mag.y, d.mag.z };
    canvas.setCursor(6, 210);
    canvas.printf("|M| %.1f LST %.1f heap %u", vlen(mv), localSiderealDeg(), (unsigned)ESP.getFreeHeap());
  }
}

static void drawMain() {
  canvas.fillScreen(COL_BG);
  if (!haveAzEl) {
    canvas.setFont(&fonts::lgfxJapanGothic_24);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEXT, COL_BG);
    canvas.drawString("センサー待機中...", 160, 120);
    canvas.pushSprite(0, 0);
    return;
  }
  projectStars();
  drawHorizon();
  drawSky();
  drawOverlay();
  canvas.pushSprite(0, 0);
}

// ---------------- 月時計モード (DinBase据え置き用) ----------------
static void drawBackgroundStars(int avoidX, int avoidY, int avoidR) {
  uint32_t s = 20260303;
  for (int i = 0; i < 90; ++i) {
    s = s * 1664525u + 1013904223u;
    int x = 4 + (s >> 8) % 312;
    int y = 4 + (s >> 18) % 232;
    int dx = x - avoidX, dy = y - avoidY;
    if (dx * dx + dy * dy < avoidR * avoidR) continue;
    float tw = 0.7f + 0.3f * sinf(millis() * 0.003f + i * 2.1f);
    uint8_t b = (uint8_t)((70 + (s % 130)) * tw);
    canvas.drawPixel(x, y, canvas.color565(b, b, b + 20));
  }
}

static void drawMoonClock() {
  struct tm t;
  getNow(t);
  float d   = moonPhaseDeg();
  float age = d / 360.f * 29.5306f;
  float k   = (1 - cosf(d * 0.0174533f)) / 2; // 輝面比
  int   ecl = eclipseNow();

  const int moonX = 110, moonY = 104, moonR = 62;
  canvas.fillScreen(canvas.color565(5, 6, 18));
  drawBackgroundStars(moonX, moonY, moonR + 24);

  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(canvas.color565(128, 128, 152));
  canvas.drawString("月時計", 8, 6);
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(String("タップ:星空 長押し:音") + (soundOn ? "ON" : "OFF"), 8, 26);

  // 時計 (右上に大きめ)
  char buf[64];
  canvas.setFont(&fonts::Font7);
  canvas.setTextSize(0.7f);
  canvas.setTextDatum(top_right);
  canvas.setTextColor(COL_TEXT);
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  canvas.drawString(buf, 314, 8);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextColor(COL_DIM);
  static const char* WD[7] = { "日", "月", "火", "水", "木", "金", "土" };
  snprintf(buf, sizeof(buf), "%d/%d(%s)", t.tm_mon + 1, t.tm_mday, WD[t.tm_wday % 7]);
  canvas.drawString(buf, 314, 46);

  // 月 (暈つき)
  drawMoonHalo(moonX, moonY, moonR);
  drawMoonDisk(moonX, moonY, moonR, d, ecl);

  // 右側: 月齢と名前
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(canvas.color565(169, 180, 214));
  snprintf(buf, sizeof(buf), "月齢 %.1f", age);
  canvas.drawString(buf, 196, 84);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(canvas.color565(242, 244, 255));
  canvas.drawString(moonName(age), 196, 104);
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextColor(canvas.color565(169, 180, 214));
  int toFull = (int)(fmodf(180.f - d + 360.f, 360.f) / 12.19f + 0.5f);
  int toNew  = (int)(fmodf(360.f - d, 360.f) / 12.19f + 0.5f);
  if (toFull == 0 || (age >= 13.0f && age < 16.2f))
    snprintf(buf, sizeof(buf), "輝面比 %d%%", (int)(k * 100 + 0.5f));
  else
    snprintf(buf, sizeof(buf), "輝面比 %d%%", (int)(k * 100 + 0.5f));
  canvas.drawString(buf, 196, 136);
  if (age >= 13.0f && age < 16.2f)
    snprintf(buf, sizeof(buf), "新月まで %d日", toNew);
  else
    snprintf(buf, sizeof(buf), "満月まで %d日", toFull);
  canvas.drawString(buf, 196, 156);

  // 下段: 月食・流星群の情報 (2行、左寄せで全幅を使う)
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(bottom_left);
  if (ecl == 2) {
    canvas.setTextColor(COL_WARN);
    canvas.drawString("皆既月食の最中です!", 8, 218);
  } else if (ecl == 1) {
    canvas.setTextColor(COL_WARN);
    canvas.drawString("月食が進行中です!", 8, 218);
  } else {
    long ds;
    const EclipseEv* e = nextEclipse(&ds);
    if (e && ds < 40LL * 86400) {
      canvas.setTextColor(COL_ACCENT);
      snprintf(buf, sizeof(buf), "%s月食 %d/%d %02d:%02d (あと%ld日)",
               e->total ? "皆既" : "部分", e->mo, e->d, e->hh, e->mm, ds / 86400);
      canvas.drawString(buf, 8, 218);
    }
  }
  int days;
  const MeteorEv* ma = meteorActive(&days);
  if (ma) {
    canvas.setTextColor(COL_ACCENT);
    if (days == 0)      snprintf(buf, sizeof(buf), "%s 今夜極大!", ma->name);
    else if (days > 0)  snprintf(buf, sizeof(buf), "%s 活動中 (極大まで%d日)", ma->name, days);
    else                snprintf(buf, sizeof(buf), "%s 活動中", ma->name);
  } else {
    const MeteorEv* mn = meteorNext(&days);
    canvas.setTextColor(COL_DIM);
    snprintf(buf, sizeof(buf), "次: %s %d/%d (あと%d日)", mn->name, mn->mo, mn->d, days);
  }
  canvas.drawString(buf, 8, 234);

  canvas.pushSprite(0, 0);
}

// ---------------- キャリブレーション ----------------
static void startCalibration() {
  modeBeforeCalib = (mode == Mode::Calibrating) ? Mode::Main : mode;
  mode = Mode::Calibrating;
  calibEndMs = millis() + CALIB_SECONDS * 1000;
  lastCalibKickMs = 0;
  haveFilter = false;
  haveAzEl = false;
}

static void tickCalibration() {
  uint32_t now = millis();
  if (now - lastCalibKickMs >= 1000) {
    M5.Imu.setCalibration(0, 0, CALIB_STRENGTH);
    lastCalibKickMs = now;
  }
  if ((int32_t)(calibEndMs - now) <= 0) {
    M5.Imu.setCalibration(0, 0, 0);
    M5.Imu.saveOffsetToNVS();
    mode = modeBeforeCalib;
    soundChime();
  }
}

static void drawCalibration() {
  canvas.fillScreen(COL_BG);
  drawBackgroundStars(160, -100, 0);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(COL_ACCENT, COL_BG);
  canvas.drawString("コンパス補正中", 160, 50);
  canvas.setFont(&fonts::lgfxJapanGothic_20);
  canvas.setTextColor(COL_TEXT, COL_BG);
  canvas.drawString("本体をゆっくり大きく", 160, 95);
  canvas.drawString("8の字に回してください", 160, 120);

  int remain = (int)((calibEndMs - millis() + 999) / 1000);
  canvas.setFont(&fonts::lgfxJapanGothic_36);
  canvas.drawString(String(remain), 160, 165);

  float progress = 1.0f - (float)(calibEndMs - millis()) / (CALIB_SECONDS * 1000.0f);
  progress = constrain(progress, 0.0f, 1.0f);
  canvas.drawRect(60, 200, 200, 12, COL_RING);
  canvas.fillRect(62, 202, (int)(196 * progress), 8, COL_ACCENT);
  canvas.pushSprite(0, 0);
}

// ---------------- 入力 ----------------
static void handleTouch() {
  auto t = M5.Touch.getDetail();
  if (mode == Mode::Calibrating) return;
  if (t.wasHold()) {
    if (mode == Mode::MoonClock) toggleSound(); // 月時計: 長押しで音ON/OFF
    else startCalibration();                    // 星空: 長押しで再キャリブレーション
  } else if (t.wasClicked()) {
    mode = (mode == Mode::Main) ? Mode::MoonClock : Mode::Main;
    soundModeSwitch();
  }
}

// ---------------- メイン ----------------
// 起動の進捗を画面に直接描く (どこで止まったか見えるように)
static int bootLine = 0;
static void bootStage(const char* msg, bool ng = false) {
  Serial.printf("boot: %s\n", msg);
  M5.Display.setFont(&fonts::lgfxJapanGothic_12);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(ng ? COL_WARN : COL_DIM, COL_BG);
  M5.Display.drawString(msg, 8, 160 + bootLine * 16);
  ++bootLine;
}

static uint32_t ntpKickAtMs = 3000; // Wi-Fi起動は画面が出た後に遅らせる (起動失敗の切り分け)

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_spk = true;
  M5.begin(cfg);
  Serial.println("yozoratokei: boot");

  prefs.begin("yozora", false);
  soundOn = prefs.getBool("sound", true);
  spkOk = M5.Speaker.isEnabled();
  if (spkOk) M5.Speaker.setVolume(110);

  // 起動スプラッシュ (スプライトを使わず直接描画 → ここが出なければ電源/初期化の問題)
  M5.Display.setBrightness(160);
  M5.Display.fillScreen(COL_BG);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setFont(&fonts::lgfxJapanGothic_24);
  M5.Display.setTextColor(COL_ACCENT, COL_BG);
  M5.Display.drawString("夜空時計", 160, 100);
  M5.Display.setFont(&fonts::lgfxJapanGothic_16);
  M5.Display.setTextColor(COL_DIM, COL_BG);
  M5.Display.drawString("起動中...", 160, 136);

  // 描画スプライト: まず内部RAM、だめならPSRAM、それでもだめなら8bit
  canvas.setColorDepth(16);
  spriteOk = (canvas.createSprite(320, 240) != nullptr);
  if (!spriteOk) {
    canvas.setPsram(true);
    spriteOk = (canvas.createSprite(320, 240) != nullptr);
  }
  if (!spriteOk) {
    canvas.setPsram(false);
    canvas.setColorDepth(8);
    spriteOk = (canvas.createSprite(320, 240) != nullptr);
  }
  Serial.printf("sprite=%s heap=%u psram=%u\n", spriteOk ? "ok" : "FAIL",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  bootStage(spriteOk ? "1 画面バッファ OK" : "1 画面バッファ NG", !spriteOk);
  if (!spriteOk) return; // loop側もspriteOkを見て停止

  setupClock();
  bootStage(rtcOk ? "2 時計 OK" : "2 時計 NG(RTCなし)", !rtcOk);

  for (int i = 0; i < STAR_COUNT; ++i) {
    sinDecT[i] = sinf(STARS[i].dec * DEG_TO_RAD);
    cosDecT[i] = cosf(STARS[i].dec * DEG_TO_RAD);
  }
  bootStage("3 星カタログ OK");

  imuOk = (M5.Imu.getType() != m5::imu_none);
  Serial.printf("imu=%d rtc=%d\n", (int)M5.Imu.getType(), (int)rtcOk);
  bootStage(imuOk ? "4 センサー OK" : "4 センサー NG", !imuOk);
  bool haveOffsets = imuOk && M5.Imu.loadOffsetFromNVS();
  bootStage("5 起動完了");
  delay(600); // 進捗を見せる

  if (!imuOk) {
    mode = Mode::MoonClock; // IMUがなくても月時計モードだけは動かす
  } else if (haveOffsets) {
    mode = Mode::Main;
  } else {
    startCalibration();
  }
  Serial.println("setup done");
}

void loop() {
  if (!spriteOk) { delay(100); return; }

  M5.update();
  handleTouch();
  handleSerialTimeSet();
  // Wi-Fi+NTPは起動から3秒後に開始 (起動を軽くする)
  if (ntpKickAtMs && millis() > ntpKickAtMs) {
    ntpKickAtMs = 0;
    startNtpSync();
  }
  updateNtpSync();
  updateSound();

  // 月時計モード中は毎正時にチャイム
  if (mode == Mode::MoonClock) {
    static int lastChimeKey = -1;
    struct tm t;
    getNow(t);
    if (t.tm_min == 0) {
      int key = t.tm_yday * 24 + t.tm_hour;
      if (key != lastChimeKey) { lastChimeKey = key; soundChime(); }
    }
  }

  if (imuOk && M5.Imu.update()) {
    computeAttitude();
  }

  switch (mode) {
    case Mode::Calibrating: tickCalibration(); drawCalibration(); break;
    case Mode::MoonClock:   drawMoonClock();                      break;
    default:
      if (!imuOk) { mode = Mode::MoonClock; break; }
      drawMain();
      break;
  }
  delay(16);
}
