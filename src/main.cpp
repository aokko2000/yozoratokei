// =============================================================
//  夜空時計 (yozoratokei) — M5Stack CoreS3
//  小さなプラネタリウム: 空にかざすと、その方向の星座・恒星・月が浮かぶ
//  月時計モード: DinBase据え置き用。月齢・満ち欠け・月食・流星群 (iroirotokei移植)
//
//  操作: タップ = プラネタリウム ⇄ 月時計 / 長押し(1秒) = コンパス再補正
//  Scroll Unit (Port A, 任意): 回す=対象を操作 / クリック=対象切替(時間→星座→画面→音量)
//        / 長押し=対象リセット。時間=星空早送り, 星座=送って矢印で誘導, 画面=表示切替, 音量=調節
//  画面プリセット: 標準 / 屋外(高コントラスト・最大輝度) / 夜間(赤・暗順応を保つ)
//  時刻: RTC + 手動時刻合わせ (月時計でScroll長押し → 時刻合わせ画面)。RTCに保存。
//        シリアル(115200): "D 2026-07-20 21:30:00" "T 21:30:00" で設定も可
//        "V"=デバッグ "B"=画面切替 "S"=音 "+/-"=音量
// =============================================================

#include <M5Unified.h>
#include <M5CoreS3.h> // カメラ(GC0308)用。M5.〜 と同じ実体を参照する薄いラッパー
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include "img_converters.h" // fmt2jpg (RGB565→JPEG)

// CoreS3 microSD (SPI)
static constexpr int SD_SCK = 36, SD_MISO = 35, SD_MOSI = 37, SD_CS = 4;
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
// 基準パレット。表示プリセット(標準/屋外/夜間)で COL_* を書き換えて使う。
static constexpr uint16_t BASE_BG      = 0x0021; // ほぼ黒の濃紺
static constexpr uint16_t BASE_TEXT    = 0xCE9F; // 淡い星色
static constexpr uint16_t BASE_ACCENT  = 0xFEA0; // 月色 (amber)
static constexpr uint16_t BASE_WARN    = 0xF9E7; // 赤系
static constexpr uint16_t BASE_DIM     = 0x630C; // 補助テキスト
static constexpr uint16_t BASE_LINE    = 0x2150; // 星座線 (暗い藍)
static constexpr uint16_t BASE_CNAME   = 0x8C71; // 星座名 (くすんだ金)
static constexpr uint16_t BASE_SNAME   = 0x4D93; // 恒星名 (暗い水色)
static constexpr uint16_t BASE_HORIZON = 0x29A6; // 地平線
static constexpr uint16_t BASE_RING    = 0x39C7;

// 実際に描画で参照する色 (applyDisplayPreset で設定)
static uint16_t COL_BG, COL_TEXT, COL_ACCENT, COL_WARN, COL_DIM,
                COL_LINE, COL_CNAME, COL_SNAME, COL_HORIZON, COL_RING;

// 表示プリセット: 0=標準 1=屋外(高コントラスト) 2=夜間(赤) 3=自動(カメラで明るさ調整)
enum DisplayPreset : uint8_t { DP_NORMAL = 0, DP_OUTDOOR, DP_NIGHT, DP_AUTO, DP_COUNT };
static uint8_t displayPreset = DP_NORMAL;
static bool    autoBright    = false; // 自動調光中か (カメラで周囲の明るさを読む)
static const char* DP_NAME[DP_COUNT] = { "標準", "屋外", "夜間", "自動" };
static int     lastAmbientAvg = -1;   // 直近の明るさ指標 (デバッグ表示用)
static uint8_t lastAmbientBri = 0;    // 直近の輝度
static bool    lastAmbientOk  = false;

static void unpack565(uint16_t c, int& r, int& g, int& b) {
  r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F;
}
static uint16_t pack565(int r, int g, int b) {
  r = constrain(r, 0, 31); g = constrain(g, 0, 63); b = constrain(b, 0, 31);
  return (uint16_t)((r << 11) | (g << 5) | b);
}
// 屋外: 明るさとコントラストを持ち上げる
static uint16_t brighten(uint16_t c) {
  int r, g, b; unpack565(c, r, g, b);
  return pack565((int)(r * 1.4f + 3), (int)(g * 1.4f + 6), (int)(b * 1.4f + 3));
}
// 夜間: 輝度を赤に変換 (暗順応を壊さない赤黒表示)
static uint16_t toRed(uint16_t c) {
  int r, g, b; unpack565(c, r, g, b);
  float luma = (r / 31.0f * 0.3f + g / 63.0f * 0.5f + b / 31.0f * 0.2f); // 0..1
  int rr = (int)(luma * 31 + 0.5f);
  return pack565(rr, rr / 6, 0);
}

static void applyDisplayPreset(uint8_t p) {
  displayPreset = p % DP_COUNT;
  autoBright = (displayPreset == DP_AUTO);
  uint16_t (*f)(uint16_t) = nullptr;
  uint8_t bright = 200;
  switch (displayPreset) {
    case DP_OUTDOOR: f = brighten; bright = 255; break;
    case DP_NIGHT:   f = toRed;    bright = 70;  break;
    default:         f = nullptr;  bright = 200; break; // 標準/自動は通常パレット
  }
  COL_BG      = (displayPreset == DP_NIGHT) ? 0x0000 : BASE_BG;
  COL_TEXT    = f ? f(BASE_TEXT)    : BASE_TEXT;
  COL_ACCENT  = f ? f(BASE_ACCENT)  : BASE_ACCENT;
  COL_WARN    = f ? f(BASE_WARN)    : BASE_WARN;
  COL_DIM     = f ? f(BASE_DIM)     : BASE_DIM;
  COL_LINE    = f ? f(BASE_LINE)    : BASE_LINE;
  COL_CNAME   = f ? f(BASE_CNAME)   : BASE_CNAME;
  COL_SNAME   = f ? f(BASE_SNAME)   : BASE_SNAME;
  COL_HORIZON = f ? f(BASE_HORIZON) : BASE_HORIZON;
  COL_RING    = f ? f(BASE_RING)    : BASE_RING;
  if (!autoBright) M5.Display.setBrightness(bright); // 自動時はサンプラーが輝度を決める
}

// ---------------- 状態 ----------------
struct Vec3 { float x, y, z; };

static M5Canvas canvas(&M5.Display);
static bool     spriteOk = false;

enum class Mode { Calibrating, Main, MoonClock, SetTime, Camera };
static Mode     mode            = Mode::Main;
static Mode     modeBeforeCalib = Mode::Main;
static uint32_t calibEndMs      = 0;
static uint32_t lastCalibKickMs = 0;
static bool     debugView       = false;
static bool     imuOk           = false;
static bool     rtcOk           = false;

// 時刻合わせ画面 (Scrollで手動設定。PC/Wi-Fi不要)
static struct tm setTm = {};
static int         setField = 0; // 0=年 1=月 2=日 3=時 4=分
static const char* SETF_NAME[5] = { "年", "月", "日", "時", "分" };

// 時間早送り: 星空・月・イベント表示を進める/戻すオフセット (秒)。0=現在時刻。
static long    skyOffsetSec = 0;

static bool    cameraOk   = false; // GC0308カメラが初期化できたか
static bool    ltrOk      = false; // LTR-553 環境光センサーが初期化できたか
static bool    sdOk       = false; // microSDカードが使えるか
static String  lastSavePath;       // 直近の保存先 (カード表示用)

// ---------------- Scroll Unit (回して星座送り+時間早送り) ----------------
static bool    scrollOk   = false;
static int16_t scrollLast = 0;        // 前回のエンコーダ生値 (差分計算用)
static int16_t selConst   = -1;       // 選択中の星座 (-1=なし)
// ノブが操作する対象。クリックで切替。
enum class Knob : uint8_t { Time, Constellation, Display, Volume };
static constexpr uint8_t KNOB_COUNT = 4;
static Knob    knob        = Knob::Time;
static const char* KNOB_NAME[KNOB_COUNT] = { "時間", "星座", "画面", "音量" };

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

static void startCalibration();               // 前方宣言 (シリアルコマンドから呼ぶ)
static void captureCard();                     // 前方宣言 (カメラ撮影→カード合成)
static void systemTimeFromTm(struct tm& t);   // 前方宣言 (時刻合わせから呼ぶ)
static void writeRtc(struct tm& t);
static void getNow(struct tm& out);

// ---------------- サウンド (iroirotokei譲りのペンタトニック星空音) ----------------
// ノンブロッキングの簡易シーケンサ: loop() から updateSound() を回す
static Preferences prefs;
static bool    soundOn = true;
static bool    spkOk   = false;
static uint8_t sndVol  = 110; // 音量 0〜255 (NVSから復元)

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

// 音量変更 (確認ビープは soundOn に関係なく鳴らして音量が分かるように)
static void changeVolume(int delta) {
  int v = (int)sndVol + delta;
  sndVol = (uint8_t)constrain(v, 0, 255);
  prefs.putUChar("vol", sndVol);
  if (spkOk) { M5.Speaker.setVolume(sndVol); M5.Speaker.tone(1319, 60); }
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

// ---------------- Scroll Unit ドライバ (M5.Ex_I2C 直叩き) ----------------
// I2C 0x40 / ENCODER 0x10(int16 累積) / BUTTON 0x20 / RGB 0x30 / RESET 0x40
static constexpr uint8_t SCROLL_ADDR = 0x40;
static constexpr uint8_t SCROLL_ENC  = 0x10;
static constexpr uint8_t SCROLL_BTN  = 0x20;
static constexpr uint8_t SCROLL_RGB  = 0x30;
static constexpr uint8_t SCROLL_RST  = 0x40;

static void scrollSetLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!scrollOk) return;
  uint8_t rgb[3] = { r, g, b };
  M5.Ex_I2C.writeRegister(SCROLL_ADDR, SCROLL_RGB, rgb, 3, 100000);
}

static void scrollBegin() {
  M5.Ex_I2C.begin();
  uint8_t v = 0;
  scrollOk = M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_BTN, &v, 1, 100000);
  if (scrollOk) {
    uint8_t buf[2];
    if (M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_ENC, buf, 2, 100000))
      scrollLast = (int16_t)(buf[0] | (buf[1] << 8));
  }
  Serial.printf("scroll=%s\n", scrollOk ? "ok" : "none");
}

// ノブの色: 時間=琥珀 星座=水色 画面=白 音量=緑
static void scrollUpdateLed() {
  switch (knob) {
    case Knob::Time:          scrollSetLed(40, 20, 0); break;
    case Knob::Constellation: scrollSetLed(0, 24, 32); break;
    case Knob::Display:       scrollSetLed(24, 24, 24); break;
    case Knob::Volume:        scrollSetLed(0, 36, 8);  break;
  }
}

// 時刻合わせ画面へ入る (現在時刻を初期値にする)
static void enterSetTime() {
  getNow(setTm);
  if (setTm.tm_year + 1900 < 2024) { setTm.tm_year = 2026 - 1900; } // 妙な値なら既定年
  setField = 0;
  mode = Mode::SetTime;
}

// 時刻合わせ: いま選んでいるフィールドを増減
static void adjustSetField(int dir) {
  switch (setField) {
    case 0: setTm.tm_year = constrain(setTm.tm_year + dir, 2024 - 1900, 2099 - 1900); break;
    case 1: setTm.tm_mon  = (setTm.tm_mon + dir + 12) % 12; break;
    case 2: setTm.tm_mday += dir; if (setTm.tm_mday < 1) setTm.tm_mday = 31; if (setTm.tm_mday > 31) setTm.tm_mday = 1; break;
    case 3: setTm.tm_hour = (setTm.tm_hour + dir + 24) % 24; break;
    case 4: setTm.tm_min  = (setTm.tm_min + dir + 60) % 60; break;
  }
}

// 時刻合わせ画面での Scroll 操作 (回す=変更 / 短押し=次へ・確定 / 長押し=中止)
static void setTimeScrollTick() {
  uint8_t buf[2];
  if (M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_ENC, buf, 2, 100000)) {
    int16_t cur = (int16_t)(buf[0] | (buf[1] << 8));
    int16_t delta = (int16_t)(cur - scrollLast);
    scrollLast = cur;
    if (delta != 0) adjustSetField(delta > 0 ? 1 : -1);
  }
  static bool wasDown = false; static uint32_t downMs = 0; static bool longFired = false;
  uint8_t b = 1;
  M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_BTN, &b, 1, 100000);
  bool down = (b == 0);
  uint32_t now = millis();
  if (down && !wasDown) { wasDown = true; downMs = now; longFired = false; }
  if (down && wasDown && !longFired && now - downMs > 700) {
    longFired = true; // 長押し: 中止
    mode = Mode::MoonClock;
    soundToggleOff();
  }
  if (!down && wasDown) {
    wasDown = false;
    if (!longFired) {
      if (setField < 4) { setField++; soundModeSwitch(); } // 次のフィールドへ
      else { // 確定: 内部クロックとRTCに書き込む
        setTm.tm_sec = 0;
        systemTimeFromTm(setTm);
        writeRtc(setTm);
        skyOffsetSec = 0;
        mode = Mode::MoonClock;
        soundChime();
      }
    }
  }
}

static void updateScroll() {
  if (!scrollOk) return;
  if (mode == Mode::SetTime) { setTimeScrollTick(); return; }
  // --- エンコーダ差分 ---
  uint8_t buf[2];
  if (M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_ENC, buf, 2, 100000)) {
    int16_t cur = (int16_t)(buf[0] | (buf[1] << 8));
    int16_t delta = (int16_t)(cur - scrollLast); // int16の巻き戻りも吸収
    scrollLast = cur;
    if (delta != 0) {
      switch (knob) {
        case Knob::Time:
          skyOffsetSec += (long)delta * 300; // 1目盛5分
          break;
        case Knob::Constellation: {
          int base = (selConst < 0) ? 0 : selConst;
          base = (base + (delta > 0 ? 1 : -1) + CONSTELLATION_COUNT) % CONSTELLATION_COUNT;
          selConst = base;
          break;
        }
        case Knob::Display:
          applyDisplayPreset((displayPreset + (delta > 0 ? 1 : DP_COUNT - 1)) % DP_COUNT);
          prefs.putUChar("disp", displayPreset);
          break;
        case Knob::Volume:
          changeVolume((int)delta * 10);
          break;
      }
    }
  }
  // --- ボタン (エッジ検出 + 長押し) ---
  static bool     wasDown   = false;
  static uint32_t downMs    = 0;
  static bool     longFired = false;
  uint8_t b = 1;
  M5.Ex_I2C.readRegister(SCROLL_ADDR, SCROLL_BTN, &b, 1, 100000);
  bool down = (b == 0); // アクティブLow
  uint32_t now = millis();
  if (down && !wasDown) { wasDown = true; downMs = now; longFired = false; }
  if (down && wasDown && !longFired && now - downMs > 700) {
    longFired = true;
    if (mode == Mode::MoonClock) { // 月時計で長押し = 時刻合わせ画面へ
      enterSetTime();
    } else { // 星空: 現在の対象をリセット
      switch (knob) {
        case Knob::Time:          skyOffsetSec = 0; break;
        case Knob::Constellation: selConst = -1;    break;
        case Knob::Display:       applyDisplayPreset(DP_NORMAL); prefs.putUChar("disp", DP_NORMAL); break;
        case Knob::Volume:        sndVol = 110; prefs.putUChar("vol", sndVol);
                                  if (spkOk) M5.Speaker.setVolume(sndVol); break;
      }
      soundToggleOff();
    }
  }
  if (!down && wasDown) {
    wasDown = false;
    if (!longFired) {
      if (mode == Mode::Camera) {
        captureCard(); // カメラ: Scroll短押しで撮影
      } else { // 短押し: ノブの対象を切替
        knob = (Knob)(((uint8_t)knob + 1) % KNOB_COUNT);
        scrollUpdateLed();
        soundModeSwitch();
      }
    }
  }
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

// 時間早送りは星空モードのみ有効。月時計モードは常に本当の現在時刻を表示する。
static time_t skyTime() { return time(nullptr) + (mode == Mode::Main ? skyOffsetSec : 0); }

static void getNow(struct tm& out) {
  time_t now = skyTime();
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

// 時刻はRTC + 手動時刻合わせ(Scroll長押し)で保持する。
// (Wi-Fi/NTPは WiFiManager のポータルがこのボードで不安定=再起動を繰り返すため撤去した)
static constexpr bool ntpSynced = false;

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
  if (work[0] == 'B' || work[0] == 'b') { // 表示プリセット切替 (標準→屋外→夜間)
    applyDisplayPreset((displayPreset + 1) % DP_COUNT);
    prefs.putUChar("disp", displayPreset);
    Serial.printf("OK: display %s\n", DP_NAME[displayPreset]);
    return;
  }
  if (work[0] == '+') { changeVolume(+15); Serial.printf("OK: vol %d\n", sndVol); return; }
  if (work[0] == '-') { changeVolume(-15); Serial.printf("OK: vol %d\n", sndVol); return; }
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
// Meeus「天文計算」49章: 第k朔(新月)の時刻 (ユリウス日, 誤差 数分)
static double newMoonJDE(double k) {
  double T  = k / 1236.85;
  double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * T * T
             - 0.000000150 * T * T * T;
  double E  = 1 - 0.002516 * T - 0.0000074 * T * T;
  double M  = (2.5534 + 29.10535670 * k - 0.0000014 * T * T) * DEG_TO_RAD;      // 太陽の平均近点角
  double Mp = (201.5643 + 385.81693528 * k + 0.0107582 * T * T) * DEG_TO_RAD;   // 月の平均近点角
  double F  = (160.7108 + 390.67050284 * k - 0.0016118 * T * T) * DEG_TO_RAD;   // 緯度引数
  double Om = (124.7746 - 1.56375588 * k + 0.0020672 * T * T) * DEG_TO_RAD;
  double c  = -0.40720 * sin(Mp) + 0.17241 * E * sin(M) + 0.01608 * sin(2 * Mp)
            + 0.01039 * sin(2 * F) + 0.00739 * E * sin(Mp - M) - 0.00514 * E * sin(Mp + M)
            + 0.00208 * E * E * sin(2 * M) - 0.00111 * sin(Mp - 2 * F)
            - 0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M)
            - 0.00042 * sin(3 * Mp) + 0.00042 * E * sin(M + 2 * F)
            + 0.00038 * E * sin(M - 2 * F) + 0.00325 * sin(Om);
  return jde + c;
}

// 月齢 (直前の新月からの経過日数)。0=新月, 約14.77=満月, 約29.53で次の新月。
static double moonAgeDays() {
  double jd = skyTime() / 86400.0 + 2440587.5; // UTC→ユリウス日
  double k  = floor((jd - 2451550.09766) / 29.530588861);
  double nm = newMoonJDE(k);
  if (nm > jd) { k -= 1; nm = newMoonJDE(k); } // 直前の新月にそろえる
  double age = jd - nm;
  if (age < 0) age += 29.530588861;
  return age;
}

// 位相角[deg] 0=新月 180=満月 (月齢から換算。描画と月齢表示を一致させる)
static float moonPhaseDeg() {
  double d = moonAgeDays() / 29.530588861 * 360.0;
  if (d >= 360.0) d -= 360.0;
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
  double jd = skyTime() / 86400.0 + 2440587.5;
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
  time_t now = skyTime();
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
  double d = (double)(skyTime() - 946728000LL) / 86400.0; // J2000からの日数
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

// 星座の平均位置 → 方位[deg] と 高度[deg] (星座送りの誘導矢印用)
static void constellationAzEl(int c, float& azOut, float& elOut) {
  const Constellation& cc = CONSTELLATIONS[c];
  float sx = 0, sy = 0, sz = 0;
  for (int l = 0; l < cc.nLines; ++l) {
    for (int e = 0; e < 2; ++e) {
      uint8_t s = cc.lines[l][e];
      float ra = STARS[s].ra * DEG_TO_RAD, dec = STARS[s].dec * DEG_TO_RAD;
      sx += cosf(dec) * cosf(ra); sy += cosf(dec) * sinf(ra); sz += sinf(dec);
    }
  }
  float ra  = atan2f(sy, sx) * RAD_TO_DEG; if (ra < 0) ra += 360;
  float dec = atan2f(sz, sqrtf(sx * sx + sy * sy)) * RAD_TO_DEG;
  float lst = localSiderealDeg();
  float sinLat = sinf(OBS_LAT_DEG * DEG_TO_RAD), cosLat = cosf(OBS_LAT_DEG * DEG_TO_RAD);
  float H = (lst - ra) * DEG_TO_RAD;
  float sinDec = sinf(dec * DEG_TO_RAD), cosDec = cosf(dec * DEG_TO_RAD);
  float E = -cosDec * sinf(H);
  float N = cosLat * sinDec - sinLat * cosDec * cosf(H);
  float U = sinLat * sinDec + cosLat * cosDec * cosf(H);
  azOut = atan2f(E, N) * RAD_TO_DEG; if (azOut < 0) azOut += 360;
  elOut = asinf(constrain(U, -1.0f, 1.0f)) * RAD_TO_DEG;
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
  uint16_t bright, mid, dim;
  if (displayPreset == DP_NIGHT) { // 夜間: 星も赤で (暗順応を保つ)
    bright = canvas.color565(b, b / 6, 0);
    mid    = canvas.color565(b / 2, b / 12, 0);
    dim    = canvas.color565(b / 5, 0, 0);
  } else {
    bright = canvas.color565(b, b, 255);
    mid    = canvas.color565(b / 2, b / 2, 170);
    dim    = canvas.color565(b / 5, b / 5, 90);
  }

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

// 選択中の星座への誘導 (画面外なら矢印+方向、視野内なら「ここ」)
static void drawConstellationGuide() {
  if (selConst < 0) return;
  float taz, tel;
  constellationAzEl(selConst, taz, tel);
  float daz = taz - azimuthDeg;
  while (daz > 180) daz -= 360;
  while (daz < -180) daz += 360;
  float del = tel - elevationDeg;

  canvas.setFont(&fonts::lgfxJapanGothic_20);
  canvas.setTextDatum(bottom_center);
  canvas.setTextColor(COL_ACCENT);
  String label = String("★ ") + CONSTELLATIONS[selConst].name;
  if (tel < -2) label += " (地平線の下)";
  canvas.drawString(label, 160, 210);

  bool centered = (fabsf(daz) < 24 && fabsf(del) < 18);
  if (centered) {
    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString("この方向です", 160, 232);
    canvas.drawCircle(160, 120, 30, COL_ACCENT);
    canvas.drawCircle(160, 120, 31, COL_ACCENT);
  } else {
    // 中心から目標方向へ矢印 (右=daz+, 上=del+)
    float ang = atan2f(-del, daz); // 画面座標(上が+del)に合わせ y反転
    int cx = 160, cy = 120, len = 46;
    int ex = cx + (int)(cosf(ang) * len), ey = cy + (int)(sinf(ang) * len);
    canvas.drawLine(cx, cy, ex, ey, COL_ACCENT);
    float a1 = ang + 2.6f, a2 = ang - 2.6f;
    canvas.drawLine(ex, ey, ex + (int)(cosf(a1) * 12), ey + (int)(sinf(a1) * 12), COL_ACCENT);
    canvas.drawLine(ex, ey, ex + (int)(cosf(a2) * 12), ey + (int)(sinf(a2) * 12), COL_ACCENT);
    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(COL_DIM);
    const char* lr = daz > 8 ? "右へ" : daz < -8 ? "左へ" : "";
    const char* ud = del > 8 ? "上へ" : del < -8 ? "下へ" : "";
    canvas.drawString(String("体を ") + lr + ud + " 向けて", 160, 232);
  }
}

static void drawSky() {
  // 星座線 (選択中の星座は強調)
  for (int c = 0; c < CONSTELLATION_COUNT; ++c) {
    const Constellation& cc = CONSTELLATIONS[c];
    uint16_t lineCol = (c == selConst) ? COL_ACCENT : COL_LINE;
    for (int l = 0; l < cc.nLines; ++l) {
      uint8_t a = cc.lines[l][0], b = cc.lines[l][1];
      if (starVis[a] && starVis[b] && (starVis[a] == 2 || starVis[b] == 2)) {
        canvas.drawLine(starX[a], starY[a], starX[b], starY[b], lineCol);
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

  drawConstellationGuide();
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

  // 自動調光の状態表示 (カメラが読めているか確認用)
  if (autoBright) {
    canvas.setFont(&fonts::lgfxJapanGothic_12);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COL_ACCENT);
    char ab[40];
    if (lastAmbientOk) snprintf(ab, sizeof(ab), "自動 光%d→輝度%d", lastAmbientAvg, lastAmbientBri);
    else               snprintf(ab, sizeof(ab), "自動 センサー無効");
    canvas.drawString(ab, 160, 4);
  }

  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(bottom_left);
  if (t.tm_year + 1900 < 2024) {
    canvas.setTextColor(COL_WARN);
    canvas.drawString("時刻未設定 (シリアルからNで同期)", 6, 236);
  } else {
    char buf[40];
    // 時間早送り中は「時刻+(±分/日)」を強調表示
    if (skyOffsetSec != 0) {
      long m = labs(skyOffsetSec) / 60;
      char off[16];
      if (m >= 1440)     snprintf(off, sizeof(off), "%+ld日", skyOffsetSec / 86400);
      else if (m >= 60)  snprintf(off, sizeof(off), "%+ldh", skyOffsetSec / 3600);
      else               snprintf(off, sizeof(off), "%+ld分", skyOffsetSec / 60);
      snprintf(buf, sizeof(buf), "早送り %d/%d %02d:%02d %s", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, off);
      canvas.setTextColor(COL_ACCENT);
    } else {
      snprintf(buf, sizeof(buf), "%d/%d %02d:%02d%s", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, ntpSynced ? " NTP" : "");
      canvas.setTextColor(COL_DIM);
    }
    canvas.drawString(buf, 6, 236);
  }
  // Scroll接続時: いまノブが何を操作するか
  canvas.setTextDatum(bottom_right);
  if (scrollOk) {
    String extra;
    if (knob == Knob::Display)     extra = String(":") + DP_NAME[displayPreset];
    else if (knob == Knob::Volume) extra = String(":") + (int)(sndVol * 100 / 255) + "%";
    canvas.setTextColor(COL_ACCENT);
    canvas.drawString(String("◑") + KNOB_NAME[(int)knob] + extra, 314, 236);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("タップ:月時計", 314, 236);
  }

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

// トークン列を中央寄せで1行描画し、field==setField のものを下線で強調
static void drawSetRow(const char* tokens[], const int fieldOf[], int n, int y) {
  const int gap = 5;
  int total = 0;
  for (int i = 0; i < n; ++i) total += canvas.textWidth(tokens[i]) + (i ? gap : 0);
  int x = 160 - total / 2;
  canvas.setTextDatum(top_left);
  for (int i = 0; i < n; ++i) {
    int w = canvas.textWidth(tokens[i]);
    bool active = (fieldOf[i] >= 0 && fieldOf[i] == setField);
    canvas.setTextColor(active ? COL_ACCENT : (fieldOf[i] < 0 ? COL_DIM : COL_TEXT));
    canvas.drawString(tokens[i], x, y);
    if (active) canvas.drawFastHLine(x, y + 40, w, COL_ACCENT);
    x += w + gap;
  }
}

// 時刻合わせ画面 (Scrollで手動設定)
static void drawSetTime() {
  canvas.fillScreen(COL_BG);
  canvas.setTextDatum(top_center);
  canvas.setFont(&fonts::lgfxJapanGothic_20);
  canvas.setTextColor(COL_ACCENT);
  canvas.drawString("時刻合わせ", 160, 10);

  char py[8], pm[8], pd[8], ph[8], pmin[8];
  snprintf(py, 8, "%04d", setTm.tm_year + 1900);
  snprintf(pm, 8, "%02d", setTm.tm_mon + 1);
  snprintf(pd, 8, "%02d", setTm.tm_mday);
  snprintf(ph, 8, "%02d", setTm.tm_hour);
  snprintf(pmin, 8, "%02d", setTm.tm_min);

  canvas.setFont(&fonts::lgfxJapanGothic_36);
  const char* row1[] = { py, "/", pm, "/", pd };
  const int   f1[]   = { 0,  -1,  1,  -1,  2 };
  drawSetRow(row1, f1, 5, 52);
  const char* row2[] = { ph, ":", pmin };
  const int   f2[]   = { 3,  -1,  4 };
  drawSetRow(row2, f2, 3, 118);

  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_ACCENT);
  canvas.drawString(String("設定中: ") + SETF_NAME[setField], 160, 178);
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextColor(COL_DIM);
  canvas.drawString("回す=変更   押す=" + String(setField < 4 ? "次へ" : "確定") + "   長押し=中止", 160, 212);
  canvas.pushSprite(0, 0);
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
  canvas.drawString(String("タップ:星空") + (scrollOk ? " Scroll長押し:時刻合わせ" : ""), 8, 26);

  // 時計 (右上に大きめ) — ここが常に本当の現在時刻
  char buf[64];
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(top_right);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(ntpSynced ? "現在時刻 (NTP)" : "現在時刻", 314, 6);
  canvas.setFont(&fonts::Font7);
  canvas.setTextSize(0.75f);
  canvas.setTextColor(COL_TEXT);
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  canvas.drawString(buf, 314, 22);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextColor(COL_DIM);
  static const char* WD[7] = { "日", "月", "火", "水", "木", "金", "土" };
  snprintf(buf, sizeof(buf), "%d/%d(%s)", t.tm_mon + 1, t.tm_mday, WD[t.tm_wday % 7]);
  canvas.drawString(buf, 314, 62);

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
  if (mode == Mode::Calibrating || mode == Mode::SetTime) return;
  if (t.wasHold()) {
    if (mode == Mode::Camera)         captureCard();  // カメラ: 長押しで撮影
    else if (mode == Mode::MoonClock) toggleSound();  // 月時計: 長押しで音ON/OFF
    else                              startCalibration(); // 星空: 長押しで再キャリブレーション
  } else if (t.wasClicked()) {
    // タップで切替: 星空 → 月時計 → (カメラ) → 星空
    if (mode == Mode::Main)           mode = Mode::MoonClock;
    else if (mode == Mode::MoonClock) mode = cameraOk ? Mode::Camera : Mode::Main;
    else                              mode = Mode::Main;
    soundModeSwitch();
  }
}

// ---------------- カメラ (M5CoreS3のGC0308。内部I2C共有はライブラリが処理) ----------------
static uint32_t cardUntilMs = 0; // このmsまで撮影カードを固定表示

// 星空メモリーカード: いまの映像に日時・月齢・月アイコンを重ねて1枚に
static void captureCard() {
  if (!CoreS3.Camera.get()) return;
  canvas.pushImage(0, 0, CoreS3.Camera.fb->width, CoreS3.Camera.fb->height,
                   (uint16_t*)CoreS3.Camera.fb->buf);
  CoreS3.Camera.free();

  struct tm t; getNow(t);
  float d   = moonPhaseDeg();
  float age = d / 360.f * 29.5306f;

  // タイトル (左上)
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(COL_ACCENT);
  canvas.drawString("夜空時計", 8, 8);
  // 月アイコン (右上)
  drawMoonDisk(295, 26, 15, d, 0);
  // 下部の帯 + 日時・月齢
  canvas.fillRect(0, 198, 320, 42, canvas.color565(8, 10, 22));
  char buf[48];
  canvas.setFont(&fonts::lgfxJapanGothic_20);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_TEXT);
  snprintf(buf, sizeof(buf), "%d/%d %02d:%02d", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
  canvas.drawString(buf, 10, 219);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(COL_ACCENT);
  snprintf(buf, sizeof(buf), "月齢%.1f %s", age, moonName(age));
  canvas.drawString(buf, 312, 219);

  // ここまでのカードをJPEGでSDへ保存 (状態テキストは保存後に描く)
  lastSavePath = "";
  if (sdOk) {
    uint8_t* jpg = nullptr; size_t jlen = 0;
    if (fmt2jpg((uint8_t*)canvas.getBuffer(), 320 * 240 * 2, 320, 240,
                PIXFORMAT_RGB565, 90, &jpg, &jlen)) {
      int n = prefs.getInt("imgn", 0) + 1;
      char path[24];
      snprintf(path, sizeof(path), "/yozora_%04d.jpg", n);
      File f = SD.open(path, FILE_WRITE);
      if (f) {
        f.write(jpg, jlen); f.close();
        prefs.putInt("imgn", n);
        lastSavePath = String(path);
      } else {
        lastSavePath = "SD書込失敗";
      }
      free(jpg);
    } else {
      lastSavePath = "JPEG変換失敗";
    }
  }
  // 保存状態 (左上・タイトル下)
  canvas.setFont(&fonts::lgfxJapanGothic_12);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(sdOk ? COL_TEXT : COL_DIM);
  canvas.drawString(sdOk ? ("保存 " + lastSavePath) : "SDなし (表示のみ)", 8, 28);

  canvas.pushSprite(0, 0);
  cardUntilMs = millis() + 4000;
  soundChime();
}

static void drawCamera() {
  if (millis() < cardUntilMs) return; // 撮影カード表示中は固定
  if (CoreS3.Camera.get()) {
    M5.Display.pushImage(0, 0, CoreS3.Camera.fb->width, CoreS3.Camera.fb->height,
                         (uint16_t*)CoreS3.Camera.fb->buf);
    CoreS3.Camera.free();
  }
  M5.Display.setFont(&fonts::lgfxJapanGothic_16);
  M5.Display.setTextDatum(bottom_left);
  M5.Display.setTextColor(COL_ACCENT, COL_BG);
  M5.Display.drawString("長押し/Scroll:撮影  タップ:戻る", 6, 236);
}

// 自動調光: LTR-553 環境光センサーの値から画面輝度を決める (カメラ不使用=安定)
static void sampleAmbient() {
  if (!ltrOk) { lastAmbientOk = false; return; }
  uint16_t als = CoreS3.Ltr553.getAlsValue(); // 暗い=小さい, 明るい=大きい
  lastAmbientAvg = als;
  lastAmbientOk  = true;
  // ALS 0〜約800 を 輝度 25〜255 に対応 (実機の数値を見て調整可)
  int a = (als > 1500) ? 1500 : (int)als;
  int target = constrain((int)map(a, 0, 800, 25, 255), 25, 255);
  static float cur = 180;
  cur += (target - cur) * 0.30f;                          // なめらかに追従
  lastAmbientBri = (uint8_t)cur;
  M5.Display.setBrightness(lastAmbientBri);
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

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_spk = true;
  CoreS3.begin(cfg); // M5.begin相当 + カメラ電源等のCoreS3固有初期化
  Serial.println("yozoratokei: boot");

  prefs.begin("yozora", false);
  soundOn = prefs.getBool("sound", true);
  sndVol  = prefs.getUChar("vol", 110);
  spkOk = M5.Speaker.isEnabled();
  if (spkOk) M5.Speaker.setVolume(sndVol);

  // 表示プリセットを復元し、パレット・輝度を確定 (スプラッシュ描画より前に必須)
  applyDisplayPreset(prefs.getUChar("disp", DP_NORMAL));

  // Scroll Unit (あれば) を初期化
  scrollBegin();
  if (scrollOk) scrollUpdateLed();

  // 起動スプラッシュ (スプライトを使わず直接描画 → ここが出なければ電源/初期化の問題)
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

  cameraOk = CoreS3.Camera.begin(); // 失敗してもカメラ機能を無効化するだけ
  Serial.printf("camera: %s\n", cameraOk ? "OK" : "FAIL");
  bootStage(cameraOk ? "5 カメラ OK" : "5 カメラ NG(無効)", !cameraOk);

  // LTR-553 環境光センサー (自動調光に使用)
  Ltr5xx_Init_Basic_Para ltrPara = {};
  ltrPara.ps_led_pulse_freq   = LTR5XX_LED_PULSE_FREQ_40KHZ;
  ltrPara.ps_measurement_rate = LTR5XX_PS_MEASUREMENT_RATE_50MS;
  ltrPara.als_gain            = LTR5XX_ALS_GAIN_48X;
  ltrOk = CoreS3.Ltr553.begin(&ltrPara);
  if (ltrOk) CoreS3.Ltr553.setAlsMode(LTR5XX_ALS_ACTIVE_MODE);
  Serial.printf("ltr553: %s\n", ltrOk ? "OK" : "FAIL");

  // microSD (写真カードのJPEG保存に使用。無ければ表示のみ)
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, SPI, 20000000);
  Serial.printf("sd: %s\n", sdOk ? "OK" : "none");
  bootStage("6 起動完了");
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
  updateScroll();
  handleSerialTimeSet();
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

  // 自動調光 (LTR-553を0.5秒おきに読む。カメラ不使用なので全モードで可)
  if (autoBright) {
    static uint32_t lastAmbientMs = 0;
    if (millis() - lastAmbientMs > 500) { lastAmbientMs = millis(); sampleAmbient(); }
  }

  switch (mode) {
    case Mode::Calibrating: tickCalibration(); drawCalibration(); break;
    case Mode::MoonClock:   drawMoonClock();                      break;
    case Mode::SetTime:     drawSetTime();                        break;
    case Mode::Camera:      drawCamera();                         break;
    default:
      if (!imuOk) { mode = Mode::MoonClock; break; }
      drawMain();
      break;
  }
  delay(16);
}
