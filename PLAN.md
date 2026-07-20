# 夜空時計 (yozoratokei) — 設計メモ

色時計 [iroirotokei](https://github.com/aokko2000/iroirotokei) の姉妹機。
「かざすと、その先の夜空が見える」デバイス。M5Stack Global Innovation Contest 2026 への2作目応募も視野
(締切 2026-08-07 23:59 PST。複数応募の可否は要項 https://m5stack.com/global-innovation-contest-2026 を要確認)。

## コンセプト

- **かざして star finder**: 空に向けると、その方向にある星座・恒星・月が画面に浮かぶ
- **幻想的な描画**: 星はグロー+またたき、星座線はふわっと浮かぶ、月は暈(かさ)つきで満ち欠けを正確に
- **月・月食・流星群**: iroirotokeiの月時計の資産(月齢計算・月食/流星群テーブル)を引き継ぐ
- 音: iroirotokei譲りのペンタトニック星空サウンド

## ハードウェア: M5Stack CoreS3 (手元にあり)

- ESP32-S3, 2.0インチ IPS LCD 320×240 (ILI9342C), 静電タッチ (GT911)
- **IMU: BMI270 + BMM150 (磁気センサーあり!)** ← StopWatchと違いコンパスが使える。これが本体験の鍵
- RTC BM8563 / スピーカー AW88298 / マイク / カメラ(未使用予定) / PortA・B・C / バッテリー500mAh

## 技術方針

1. **方位・仰角の推定**: BMM150(磁気)+BMI270(加速度)で傾き補償コンパス。
   - 8の字キャリブレーション(初回、オフセット保存)
   - 磁気偏角の補正 (日本はおよそ -8°、地域設定で調整)
2. **天球計算**: 恒星(赤経・赤緯)→ 現在時刻の地方恒星時+観測地(既定: 東京 35.7N 139.7E)で方位・高度へ変換
3. **星カタログ内蔵**: 明るい恒星 約100個 + 主要星座15個の星座線 (数KBのテーブル)
4. **月**: Meeus簡易式の月齢・位相角 (iroirotokeiの `moonPhaseDeg()` を移植) + 月の位置(黄経黄緯→赤道座標→地平座標)
5. **月食/流星群テーブル**: iroirotokeiの `ECLIPSES[]` / `METEORS[]` をそのまま移植
   (2026-03-03 皆既月食は国立天文台公表値: 欠け始め18:50 / 皆既20:04-21:03 / 最大20:33 / 終了22:18 JST)
6. **時刻**: Wi-Fi+NTP自動同期 (iroirotokeiのWiFiManager+configTzTimeの実装パターンを移植)

## 参考実装 (移植元)

`C:\iroirotokei\src\main.cpp` に以下の実績コードあり:
- 月齢計算 `moonPhaseDeg()` / 月の描画 `drawMoonDisk()` (欠け際ロジック)
- 月食 `ECLIPSES[]` / 流星群 `METEORS[]` テーブルと判定関数
- ペンタトニック音階 `PENTA[]` とサウンドシーケンサ
- Wi-Fi設定ポータル(WiFiManager)+NTP同期+RTC書き込みの一連の流れ
- PlatformIO構成の作法 (platformio.ini)

## 進め方 (段階)

1. PlatformIO環境 (board: CoreS3, M5Unified) + Hello表示
2. コンパス+傾き → 「いま向いている方位・仰角」を画面表示 (キャリブレーション込み) ← 最重要・最初の山
3. 天球計算 → 画面に「その方向の星」をプロット
4. 星座線・名前表示、グロー/またたき演出
5. 月の描画 (位置+満ち欠け+暈)
6. 月食・流星群イベント演出、サウンド
7. GitHub公開 (新リポジトリ yozoratokei) → 必要ならコンテスト応募

## 注意

- iroirotokei は審査中: mainブランチ凍結中 (開発は v2-dev)。本プロジェクトはiroirotokeiに一切触れない
- ユーザーのGitHub: aokko2000 (コミットは匿名アドレス aokko2000@users.noreply.github.com を使用)
