// =============================================================
//  star_catalog.h — 夜空時計の星カタログ
//  明るい恒星 113個 (J2000 赤経・赤緯, 度) + 主要18星座の星座線
//  RA/Dec は星座線の描画に十分な精度 (±0.1°程度) で収録
// =============================================================
#pragma once
#include <stdint.h>

struct Star {
  float  ra;     // 赤経 [deg] (J2000)
  float  dec;    // 赤緯 [deg] (J2000)
  int8_t mag10;  // 等級×10 (シリウス=-15)
};

// STARS[] と同じ順序で並べること
enum : uint8_t {
  // オリオン座
  S_BETELGEUSE, S_RIGEL, S_BELLATRIX, S_MINTAKA, S_ALNILAM, S_ALNITAK, S_SAIPH,
  // おおいぬ座
  S_SIRIUS, S_MIRZAM, S_ADHARA, S_WEZEN, S_ALUDRA,
  // こいぬ座
  S_PROCYON, S_GOMEISA,
  // ふたご座
  S_POLLUX, S_CASTOR, S_ALHENA, S_WASAT, S_MEBSUTA,
  // おうし座 (すばる含む)
  S_ALDEBARAN, S_ELNATH, S_ZETA_TAU, S_GAMMA_TAU, S_PLEIADES,
  // ぎょしゃ座
  S_CAPELLA, S_MENKALINAN, S_THETA_AUR, S_IOTA_AUR, S_EPS_AUR,
  // カシオペヤ座
  S_CAPH, S_SCHEDAR, S_GAMMA_CAS, S_RUCHBAH, S_SEGIN,
  // おおぐま座 (北斗七星)
  S_DUBHE, S_MERAK, S_PHECDA, S_MEGREZ, S_ALIOTH, S_MIZAR, S_ALKAID,
  // こぐま座
  S_POLARIS, S_KOCHAB, S_PHERKAD, S_DELTA_UMI, S_EPS_UMI, S_ZETA_UMI, S_ETA_UMI,
  // さそり座
  S_ANTARES, S_DSCHUBBA, S_BETA_SCO, S_PI_SCO, S_SIGMA_SCO, S_TAU_SCO, S_EPS_SCO,
  S_MU_SCO, S_ZETA_SCO, S_ETA_SCO, S_SARGAS, S_IOTA_SCO, S_KAPPA_SCO, S_SHAULA, S_UPSILON_SCO,
  // いて座 (南斗六星・ティーポット)
  S_NUNKI, S_KAUS_AUSTRALIS, S_DELTA_SGR, S_LAMBDA_SGR, S_PHI_SGR, S_ZETA_SGR, S_GAMMA_SGR, S_TAU_SGR,
  // こと座
  S_VEGA, S_SHELIAK, S_SULAFAT, S_DELTA_LYR, S_ZETA_LYR,
  // はくちょう座 (北十字)
  S_DENEB, S_SADR, S_GIENAH_CYG, S_DELTA_CYG, S_ALBIREO,
  // わし座
  S_ALTAIR, S_TARAZED, S_ALSHAIN, S_ZETA_AQL, S_THETA_AQL,
  // しし座
  S_REGULUS, S_DENEBOLA, S_ALGIEBA, S_ZETA_LEO, S_MU_LEO, S_EPS_LEO, S_ETA_LEO, S_ZOSMA, S_THETA_LEO,
  // おとめ座
  S_SPICA, S_PORRIMA, S_VINDEMIATRIX, S_DELTA_VIR, S_BETA_VIR,
  // うしかい座
  S_ARCTURUS, S_IZAR, S_ETA_BOO, S_GAMMA_BOO, S_DELTA_BOO, S_BETA_BOO,
  // ペガスス座 (秋の四辺形, アンドロメダ座αを含む)
  S_MARKAB, S_SCHEAT, S_ALGENIB, S_ALPHERATZ, S_ENIF,
  // 単独の明星
  S_CANOPUS, S_FOMALHAUT,
  STAR_COUNT
};

static const Star STARS[STAR_COUNT] = {
  // オリオン座
  {  88.79f,  +7.41f,   5 },  // ベテルギウス
  {  78.63f,  -8.20f,   1 },  // リゲル
  {  81.28f,  +6.35f,  16 },  // ベラトリックス
  {  83.00f,  -0.30f,  22 },  // ミンタカ (三ツ星)
  {  84.05f,  -1.20f,  17 },  // アルニラム (三ツ星)
  {  85.19f,  -1.94f,  18 },  // アルニタク (三ツ星)
  {  86.94f,  -9.67f,  21 },  // サイフ
  // おおいぬ座
  { 101.29f, -16.72f, -15 },  // シリウス
  {  95.67f, -17.96f,  20 },  // ミルザム
  { 104.66f, -28.97f,  15 },  // アダラ
  { 107.10f, -26.39f,  18 },  // ウェズン
  { 111.02f, -29.30f,  25 },  // アルドラ
  // こいぬ座
  { 114.83f,  +5.22f,   4 },  // プロキオン
  { 111.79f,  +8.29f,  29 },  // ゴメイサ
  // ふたご座
  { 116.33f, +28.03f,  11 },  // ポルックス
  { 113.65f, +31.89f,  16 },  // カストル
  {  99.43f, +16.40f,  19 },  // アルヘナ
  { 110.03f, +21.98f,  35 },  // ワサト
  { 100.98f, +25.13f,  31 },  // メブスタ
  // おうし座
  {  68.98f, +16.51f,   9 },  // アルデバラン
  {  81.57f, +28.61f,  17 },  // エルナト
  {  84.41f, +21.14f,  30 },  // ζ Tau
  {  64.95f, +15.63f,  37 },  // γ Tau (ヒアデス)
  {  56.87f, +24.11f,  16 },  // すばる (プレアデス星団)
  // ぎょしゃ座
  {  79.17f, +46.00f,   1 },  // カペラ
  {  89.88f, +44.95f,  19 },  // メンカリナン
  {  89.93f, +37.21f,  26 },  // θ Aur
  {  74.25f, +33.17f,  27 },  // ι Aur
  {  75.49f, +43.82f,  30 },  // ε Aur
  // カシオペヤ座
  {   2.29f, +59.15f,  23 },  // カフ
  {  10.13f, +56.54f,  22 },  // シェダル
  {  14.18f, +60.72f,  25 },  // γ Cas
  {  21.45f, +60.24f,  27 },  // ルクバー
  {  28.60f, +63.67f,  34 },  // セギン
  // おおぐま座
  { 165.93f, +61.75f,  18 },  // ドゥーベ
  { 165.46f, +56.38f,  24 },  // メラク
  { 178.46f, +53.69f,  24 },  // フェクダ
  { 183.86f, +57.03f,  33 },  // メグレズ
  { 193.51f, +55.96f,  18 },  // アリオト
  { 200.98f, +54.93f,  23 },  // ミザール
  { 206.89f, +49.31f,  19 },  // アルカイド
  // こぐま座
  {  37.95f, +89.26f,  20 },  // 北極星 (ポラリス)
  { 222.68f, +74.16f,  21 },  // コカブ
  { 230.18f, +71.83f,  31 },  // フェルカド
  { 263.05f, +86.59f,  44 },  // δ UMi
  { 251.49f, +82.04f,  42 },  // ε UMi
  { 236.02f, +77.79f,  43 },  // ζ UMi
  { 244.38f, +75.75f,  50 },  // η UMi
  // さそり座
  { 247.35f, -26.43f,  10 },  // アンタレス
  { 240.08f, -22.62f,  23 },  // ジュバ
  { 241.36f, -19.81f,  26 },  // β Sco
  { 239.71f, -26.11f,  29 },  // π Sco
  { 245.30f, -25.59f,  29 },  // σ Sco
  { 248.97f, -28.22f,  28 },  // τ Sco
  { 252.54f, -34.29f,  23 },  // ε Sco
  { 252.97f, -38.05f,  30 },  // μ Sco
  { 253.50f, -42.40f,  36 },  // ζ Sco
  { 258.04f, -43.24f,  33 },  // η Sco
  { 264.33f, -43.00f,  19 },  // サルガス
  { 266.90f, -40.13f,  30 },  // ι Sco
  { 265.62f, -39.03f,  24 },  // κ Sco
  { 263.40f, -37.10f,  16 },  // シャウラ (毒針)
  { 262.69f, -37.30f,  27 },  // υ Sco
  // いて座
  { 283.82f, -26.30f,  20 },  // ヌンキ
  { 276.04f, -34.38f,  19 },  // カウス・アウストラリス
  { 275.25f, -29.83f,  27 },  // δ Sgr
  { 277.00f, -25.42f,  28 },  // λ Sgr
  { 281.41f, -26.99f,  32 },  // φ Sgr
  { 285.65f, -29.88f,  26 },  // ζ Sgr
  { 271.45f, -30.42f,  30 },  // γ Sgr
  { 286.73f, -27.67f,  33 },  // τ Sgr
  // こと座
  { 279.23f, +38.78f,   0 },  // ベガ
  { 282.52f, +33.36f,  35 },  // シェリアク
  { 284.74f, +32.69f,  32 },  // スラファト
  { 283.63f, +36.90f,  43 },  // δ Lyr
  { 281.19f, +37.61f,  44 },  // ζ Lyr
  // はくちょう座
  { 310.36f, +45.28f,  13 },  // デネブ
  { 305.56f, +40.26f,  22 },  // サドル
  { 311.55f, +33.97f,  25 },  // ギェナー (ε Cyg)
  { 296.24f, +45.13f,  29 },  // δ Cyg
  { 292.68f, +27.96f,  31 },  // アルビレオ
  // わし座
  { 297.70f,  +8.87f,   8 },  // アルタイル
  { 296.56f, +10.61f,  27 },  // タラゼド
  { 298.83f,  +6.41f,  37 },  // アルシャイン
  { 286.35f, +13.86f,  30 },  // ζ Aql
  { 302.83f,  -0.82f,  32 },  // θ Aql
  // しし座
  { 152.09f, +11.97f,  14 },  // レグルス
  { 177.26f, +14.57f,  21 },  // デネボラ
  { 154.99f, +19.84f,  26 },  // アルギエバ
  { 154.17f, +23.42f,  34 },  // ζ Leo
  { 148.19f, +26.00f,  39 },  // μ Leo
  { 146.46f, +23.77f,  30 },  // ε Leo
  { 151.83f, +16.76f,  35 },  // η Leo
  { 168.53f, +20.52f,  26 },  // ゾスマ
  { 168.56f, +15.43f,  33 },  // θ Leo
  // おとめ座
  { 201.30f, -11.16f,  10 },  // スピカ
  { 190.42f,  -1.45f,  27 },  // ポリマ
  { 195.54f, +10.96f,  29 },  // ヴィンデミアトリクス
  { 193.90f,  +3.40f,  34 },  // δ Vir
  { 177.67f,  +1.76f,  36 },  // β Vir
  // うしかい座
  { 213.92f, +19.18f,   0 },  // アークトゥルス
  { 221.25f, +27.07f,  24 },  // イザール
  { 208.67f, +18.40f,  27 },  // η Boo
  { 218.02f, +38.31f,  30 },  // γ Boo
  { 228.88f, +33.31f,  35 },  // δ Boo
  { 225.49f, +40.39f,  35 },  // β Boo
  // ペガスス座
  { 346.19f, +15.21f,  25 },  // マルカブ
  { 345.94f, +28.08f,  24 },  // シェアト
  {   3.31f, +15.18f,  28 },  // アルゲニブ
  {   2.10f, +29.09f,  21 },  // アルフェラッツ (α And)
  { 326.05f,  +9.88f,  24 },  // エニフ
  // 単独の明星
  {  95.99f, -52.70f,  -7 },  // カノープス
  { 344.41f, -29.62f,  12 },  // フォーマルハウト
};

// ---------------- 星座線 ----------------
typedef const uint8_t LinePair[2];

static LinePair LINES_ORI[] = {
  { S_BETELGEUSE, S_BELLATRIX }, { S_BETELGEUSE, S_ALNITAK }, { S_BELLATRIX, S_MINTAKA },
  { S_MINTAKA, S_ALNILAM }, { S_ALNILAM, S_ALNITAK },
  { S_MINTAKA, S_RIGEL }, { S_ALNITAK, S_SAIPH }, { S_RIGEL, S_SAIPH },
};
static LinePair LINES_CMA[] = {
  { S_SIRIUS, S_MIRZAM }, { S_SIRIUS, S_WEZEN }, { S_WEZEN, S_ADHARA }, { S_WEZEN, S_ALUDRA },
};
static LinePair LINES_CMI[] = { { S_PROCYON, S_GOMEISA } };
static LinePair LINES_GEM[] = {
  { S_POLLUX, S_WASAT }, { S_WASAT, S_ALHENA }, { S_CASTOR, S_MEBSUTA }, { S_MEBSUTA, S_WASAT },
};
static LinePair LINES_TAU[] = {
  { S_GAMMA_TAU, S_ALDEBARAN }, { S_ALDEBARAN, S_ZETA_TAU }, { S_GAMMA_TAU, S_ELNATH },
};
static LinePair LINES_AUR[] = {
  { S_CAPELLA, S_MENKALINAN }, { S_MENKALINAN, S_THETA_AUR }, { S_THETA_AUR, S_ELNATH },
  { S_ELNATH, S_IOTA_AUR }, { S_IOTA_AUR, S_EPS_AUR }, { S_EPS_AUR, S_CAPELLA },
};
static LinePair LINES_CAS[] = {
  { S_CAPH, S_SCHEDAR }, { S_SCHEDAR, S_GAMMA_CAS }, { S_GAMMA_CAS, S_RUCHBAH }, { S_RUCHBAH, S_SEGIN },
};
static LinePair LINES_UMA[] = {
  { S_DUBHE, S_MERAK }, { S_MERAK, S_PHECDA }, { S_PHECDA, S_MEGREZ }, { S_MEGREZ, S_DUBHE },
  { S_MEGREZ, S_ALIOTH }, { S_ALIOTH, S_MIZAR }, { S_MIZAR, S_ALKAID },
};
static LinePair LINES_UMI[] = {
  { S_POLARIS, S_DELTA_UMI }, { S_DELTA_UMI, S_EPS_UMI }, { S_EPS_UMI, S_ZETA_UMI },
  { S_ZETA_UMI, S_KOCHAB }, { S_KOCHAB, S_PHERKAD }, { S_PHERKAD, S_ETA_UMI }, { S_ETA_UMI, S_ZETA_UMI },
};
static LinePair LINES_SCO[] = {
  { S_BETA_SCO, S_DSCHUBBA }, { S_DSCHUBBA, S_PI_SCO }, { S_DSCHUBBA, S_SIGMA_SCO },
  { S_SIGMA_SCO, S_ANTARES }, { S_ANTARES, S_TAU_SCO }, { S_TAU_SCO, S_EPS_SCO },
  { S_EPS_SCO, S_MU_SCO }, { S_MU_SCO, S_ZETA_SCO }, { S_ZETA_SCO, S_ETA_SCO },
  { S_ETA_SCO, S_SARGAS }, { S_SARGAS, S_IOTA_SCO }, { S_IOTA_SCO, S_KAPPA_SCO },
  { S_KAPPA_SCO, S_SHAULA }, { S_SHAULA, S_UPSILON_SCO },
};
static LinePair LINES_SGR[] = {
  { S_GAMMA_SGR, S_DELTA_SGR }, { S_DELTA_SGR, S_KAUS_AUSTRALIS }, { S_KAUS_AUSTRALIS, S_ZETA_SGR },
  { S_ZETA_SGR, S_PHI_SGR }, { S_PHI_SGR, S_DELTA_SGR }, { S_DELTA_SGR, S_LAMBDA_SGR },
  { S_LAMBDA_SGR, S_PHI_SGR }, { S_PHI_SGR, S_NUNKI }, { S_NUNKI, S_TAU_SGR }, { S_TAU_SGR, S_ZETA_SGR },
};
static LinePair LINES_LYR[] = {
  { S_VEGA, S_ZETA_LYR }, { S_ZETA_LYR, S_SHELIAK }, { S_SHELIAK, S_SULAFAT },
  { S_SULAFAT, S_DELTA_LYR }, { S_DELTA_LYR, S_ZETA_LYR },
};
static LinePair LINES_CYG[] = {
  { S_DENEB, S_SADR }, { S_SADR, S_ALBIREO }, { S_SADR, S_GIENAH_CYG }, { S_SADR, S_DELTA_CYG },
};
static LinePair LINES_AQL[] = {
  { S_TARAZED, S_ALTAIR }, { S_ALTAIR, S_ALSHAIN }, { S_ALTAIR, S_ZETA_AQL }, { S_ALTAIR, S_THETA_AQL },
};
static LinePair LINES_LEO[] = {
  { S_REGULUS, S_ETA_LEO }, { S_ETA_LEO, S_ALGIEBA }, { S_ALGIEBA, S_ZETA_LEO },
  { S_ZETA_LEO, S_MU_LEO }, { S_MU_LEO, S_EPS_LEO },
  { S_ALGIEBA, S_ZOSMA }, { S_ZOSMA, S_DENEBOLA }, { S_DENEBOLA, S_THETA_LEO },
  { S_THETA_LEO, S_ZOSMA }, { S_THETA_LEO, S_REGULUS },
};
static LinePair LINES_VIR[] = {
  { S_SPICA, S_PORRIMA }, { S_PORRIMA, S_DELTA_VIR }, { S_DELTA_VIR, S_VINDEMIATRIX }, { S_PORRIMA, S_BETA_VIR },
};
static LinePair LINES_BOO[] = {
  { S_ARCTURUS, S_IZAR }, { S_IZAR, S_DELTA_BOO }, { S_DELTA_BOO, S_BETA_BOO },
  { S_BETA_BOO, S_GAMMA_BOO }, { S_GAMMA_BOO, S_ARCTURUS }, { S_ARCTURUS, S_ETA_BOO },
};
static LinePair LINES_PEG[] = {
  { S_MARKAB, S_SCHEAT }, { S_SCHEAT, S_ALPHERATZ }, { S_ALPHERATZ, S_ALGENIB },
  { S_ALGENIB, S_MARKAB }, { S_MARKAB, S_ENIF },
};

struct Constellation {
  const char*     name;   // 日本語名
  const LinePair* lines;
  uint8_t         nLines;
};
#define CONST_ENTRY(name, arr) { name, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }

static const Constellation CONSTELLATIONS[] = {
  CONST_ENTRY("オリオン座",   LINES_ORI),
  CONST_ENTRY("おおいぬ座",   LINES_CMA),
  CONST_ENTRY("こいぬ座",     LINES_CMI),
  CONST_ENTRY("ふたご座",     LINES_GEM),
  CONST_ENTRY("おうし座",     LINES_TAU),
  CONST_ENTRY("ぎょしゃ座",   LINES_AUR),
  CONST_ENTRY("カシオペヤ座", LINES_CAS),
  CONST_ENTRY("おおぐま座",   LINES_UMA),
  CONST_ENTRY("こぐま座",     LINES_UMI),
  CONST_ENTRY("さそり座",     LINES_SCO),
  CONST_ENTRY("いて座",       LINES_SGR),
  CONST_ENTRY("こと座",       LINES_LYR),
  CONST_ENTRY("はくちょう座", LINES_CYG),
  CONST_ENTRY("わし座",       LINES_AQL),
  CONST_ENTRY("しし座",       LINES_LEO),
  CONST_ENTRY("おとめ座",     LINES_VIR),
  CONST_ENTRY("うしかい座",   LINES_BOO),
  CONST_ENTRY("ペガスス座",   LINES_PEG),
};
static const uint8_t CONSTELLATION_COUNT = sizeof(CONSTELLATIONS) / sizeof(CONSTELLATIONS[0]);

// ---------------- 有名な星の名前ラベル ----------------
struct StarName { uint8_t idx; const char* name; };
static const StarName STAR_NAMES[] = {
  { S_SIRIUS,         "シリウス" },
  { S_PROCYON,        "プロキオン" },
  { S_BETELGEUSE,     "ベテルギウス" },
  { S_RIGEL,          "リゲル" },
  { S_POLLUX,         "ポルックス" },
  { S_CASTOR,         "カストル" },
  { S_ALDEBARAN,      "アルデバラン" },
  { S_PLEIADES,       "すばる" },
  { S_CAPELLA,        "カペラ" },
  { S_POLARIS,        "北極星" },
  { S_ANTARES,        "アンタレス" },
  { S_VEGA,           "ベガ" },
  { S_DENEB,          "デネブ" },
  { S_ALTAIR,         "アルタイル" },
  { S_REGULUS,        "レグルス" },
  { S_SPICA,          "スピカ" },
  { S_ARCTURUS,       "アークトゥルス" },
  { S_CANOPUS,        "カノープス" },
  { S_FOMALHAUT,      "フォーマルハウト" },
};
static const uint8_t STAR_NAME_COUNT = sizeof(STAR_NAMES) / sizeof(STAR_NAMES[0]);
