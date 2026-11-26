<p align="center">
  <img src="runtime/sce_sys/livearea/contents/bg0.png" alt="Monster Hunter Tri" width="820">
</p>

# Monster Hunter Tri Vita

Wii 向け『モンスターハンター3（トライ）』（日本版 `RMHJ08`）を PlayStation Vita（ARM Cortex-A9 / GXM）向けに静的リコンパイル移植したプロジェクトです。

本プロジェクトは [Grarak/mh3-wii](https://github.com/Grarak/mh3-wii)（v0.3.1 ベース）をフォークし、**日本版ディスク（`RMHJ08`）専用**として関数アドレス・メモリ配置・ファイル構成・日本語フォント・クエスト形式・ランチャー UI などを全面対応させたものです。

ゲームの PowerPC コードをビルド時に C++ へ静的翻訳し、Wii のハードウェア・OS 機能を置き換えるネイティブランタイムとリンクして実行します（エミュレータ・インタプリタ・JIT は使用していません）。描画は PS Vita ではネイティブ GXM、Linux では OpenGL ES 3.0 を使用します。

> **重要**
> 本リポジトリにはゲーム本体のアセットやディスクイメージは一切含まれていません。
> プレイおよびビルドには、ご自身で所有する **日本版『モンスターハンター3（トライ）』ディスク（Game ID: `RMHJ08`）** の **ISO または RVZ イメージ** が必要です。欧州版（`RMHP08`）や北米版（`RMHE08`）には対応していません。

---

## 主な特徴

- **日本版（`RMHJ08`）ネイティブ対応**
  - 日本版 `main.dol` および全 18 種の動的オーバーレイ（RSO）を静的リコンパイル。
  - 日本版の FST ファイル構成、Shift-JIS テキスト／動的フォントアトラス描画、日本版クエストデータ形式（XOR `0xFF` 符号化・`0x3BC` レイアウト）に対応。
  - PS Vita 向けランチャーおよびサブメニュー（クエスト選択・カスタムクエスト作成・設定等）を日本語化。
- **60FPS 動作・可変ステップ（ゲーム速度補正）対応**
  - 視錐台カリング、マルチスレッド描画パイプライン、動的フォントアトラスのスロットキャッシュ、CPU/NEON ホットパス最適化により高フレームレート動作を実現。
  - 60FPS 設定時でもゲーム進行速度が 2 倍速にならないよう、可変ステップ（`tick_sites` / `step_counter_sites` およびアクション・AI・カメラ・エフェクト群）を日本版向けに移植。
- **倍速プレイモード（`L + SELECT`）**
  - プレイ中に `L + SELECT` を押すことで、描画や音声ピッチを維持したまま **通常（1 倍速）⇔ 2 倍速** をトグル切り替え可能（移動や採取の効率化に便利です）。
- **充実したランチャー機能**
  - タイトル画面からの通常起動に加え、モガの村への直行、オフライン／オンライン／闘技大会クエストの直接開始、オリジナルクエスト作成に対応。
  - ワイド画面（16:9 / 4:3）、アップスケール（544 ライン / 448 ライン）、フレーム制限（60 FPS / 30 FPS / オフ）、MSAA（オフ / 2x / 4x）の設定が可能。

### 現在の制限事項・未対応機能

以下の機能は日本版（`RMHJ08`）向けフックが未移植のため、現在メインメニューから非表示にしています。

- **チート機能**: 非対応（メインメニューから非表示）。
- **オンライン／アドホック通信**: 非対応（メインメニューから非表示）。※オフラインで遊べる「オンラインクエスト（街クエスト）」は、ランチャーの「クエスト」メニューから引き続きプレイ可能です。
- **Vita システムキーボード**: 非対応（メインメニューから非表示）。文字入力はゲーム内キーボードを使用してください。

---

## PS Vita へのインストール手順

### 1. 必要なカーネルプラグインの導入

以下のプラグインを導入し、`ur0:tai/config.txt`（または `ux0:tai/config.txt`）の `*KERNEL` セクションに追記して PS Vita を再起動してください。

- [kubridge](https://github.com/bythos14/kubridge/releases)（0.3.1 以降）
- [CapUnlocker](https://github.com/GrapheneCt/CapUnlocker/releases)

```ini
*KERNEL
ur0:tai/kubridge.skprx
ur0:tai/CapUnlocker.skprx
```

（`ux0:tai/` に配置した場合はパスを `ux0:tai/...` に読み替えてください）

> **補足（シェーダーフォールバックについて）**
> ビルド時に事前コンパイルされた GXM シェーダーバンクに未収録のシェーダーに遭遇した場合、実機上で `vitaShaRK` によるランタイムコンパイルへフォールバックする構成になっています。未収録シェーダーが発生した際に備え、`ur0:data/libshacccg.suprx`（PSM Runtime 由来のシェーダーコンパイラ）を導入しておくことを推奨します。

### 2. VPK のインストールとディスクイメージの配置

1. VitaShell を使って `mh3-vita.vpk` をインストールします。
2. ご自身の日本版『モンスターハンター3』ディスクイメージ（`.rvz` または `.iso`）を以下のフォルダへコピーします。

   ```text
   ux0:/data/mh3-wii/
   ```

   ファイル名は任意ですが、拡張子は `.rvz` または `.iso` にしてください。コピー後にもキャッシュ展開用の空き容量が十分残っていることを確認してください。

### 3. 起動とデータ保存先

- LiveArea の「Monster Hunter Tri」アイコンから起動します。
- 初回起動時にディスクイメージの検証が行われ、約 7 MB のメタデータが `ux0:/data/mh3-wii/disc/` に書き込まれます。テクスチャやモデルはゲームロード時に変換・キャッシュされます。
- セーブデータは `ux0:/data/mh3-wii/saves/`（日本版タイトル ID: `524d484a`）に保存されます（詳細は [docs/save-format.md](docs/save-format.md) を参照）。

---

## 操作方法（PS Vita）

ゲーム内では Wii リモコン＋クラシックコントローラ（RVL-005）として認識されます。ボタン配置は元のクラシックコントローラに準拠しており、ゲーム内オプションの操作タイプ（クラシックタイプ 1 / 2）もそのまま機能します。

| クラシックコントローラ | PS Vita | PS TV コントローラ | 主な操作 |
| --- | --- | --- | --- |
| 左スティック | 左スティック | 左スティック | 移動 |
| 右スティック | 右スティック | 右スティック | 攻撃（初期設定）またはカメラ操作 |
| A | ○ | ○ | 決定・アクション |
| B | × | × | キャンセル・回避 |
| X | △ | △ | 操作タイプ準拠 |
| Y | □ | □ | 操作タイプ準拠 |
| 十字キー | 十字キー | 十字キー | メニュー移動・カメラ操作 |
| L | L | L1 | 操作タイプ準拠・メニュータブ |
| R | R | R1 | 操作タイプ準拠・メニュータブ |
| ZL / ZR | — | L2 / R2 | 操作タイプ準拠 |
| + | START | START | ポーズメニュー |
| − | SELECT | SELECT | 操作タイプ準拠 |
| HOME | — | L3 + R3 | HOME メニュー |
| — | **L + SELECT** | **L1 + SELECT** | **倍速プレイ切替（1 倍速 ⇔ 2 倍速）** |

- ランチャーのメニュー操作は `×` 決定、`○` 戻る、`十字キー` 移動です。
- 前面タッチスクリーンは Wii リモコンのポインタとして動作します（モンスターを長押ししてログへドラッグ可能）。
- 詳細なスティック補正や PC（Linux / SDL）での操作は [docs/vita-controls.md](docs/vita-controls.md) を参照してください。

---

## ビルド方法

Linux（または WSL2）環境でのビルドを前提としています。

### 必要なツール

全ターゲット共通:
- Python 3、CMake 3.25 以上、Ninja、Git
- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)（`tools/dotnet/` に配置するか環境変数 `DOTNET_BIN` で指定）
- [nodtool](https://github.com/encounter/nod/releases)（`tools/nodtool` に配置）
- **Clang 21 / LLD 21 / LLVM 21**（`clang-21`, `clang++-21`, `ld.lld-21`, `llvm-ar-21` が `PATH` 上にあること）

PS Vita ビルド追加要件:
- [VitaSDK](https://vitasdk.org)（デフォルト: `/usr/local/vitasdk`、環境変数 `VITASDK` で変更可）
- （オフライン GXM シェーダーバンクを新規コンパイルする場合）Docker および公式 PS Vita SDK 付属の `psp2cgc.exe`（本リポジトリでは配布していません）:
  ```sh
  export PSP2CGC=/absolute/path/to/psp2cgc.exe
  ```

### 1. 日本版ディスクイメージの展開

ご自身の日本版 ROM（`RMHJ08`）を `Assets/DATA` へ展開します。

```sh
tools/nodtool extract -q RMHJ08.rvz Assets/DATA
```

ビルド実行時に自動でトランスレータ（C#）がビルドされ、`generated/` 配下へ PowerPC → C++ 静的翻訳が行われます（初回は数分かかります）。手動で翻訳のみ実行したい場合は `scripts/translate.sh` または `python3 scripts/translate.py` を使用します。

### 2. PS Vita 向けビルド（`build/vita/mh3-vita.vpk`）

```sh
# リリースビルド（ThinLTO）
scripts/build-vita.sh

# 開発ビルド（診断ログ mh3.log・デバッグツール有効）
MH3_DEVELOPMENT=ON scripts/build-vita.sh
```

並列数は環境変数 `BUILD_JOBS` / `OVERLAY_JOBS` で調整できます（例: `BUILD_JOBS=4 OVERLAY_JOBS=2 scripts/build-vita.sh`）。

### 3. Linux 向け検証ビルド

デバッグや動作確認用に、Linux（x86_64 / arm64）上で直接実行できるネイティブビルドも構築できます。

```sh
# Linux (arm64 の例)
MH3_ARCH=arm64 MH3_BUILD_DIR=build/linux-arm64-dev \
  MH3_EXTRA_CMAKE_ARGS='-DMH3_DEVELOPMENT=ON -DMH3_BUILD_VITA_TEXTURE_PACKER=OFF' \
  scripts/build-aurora.sh
```

実行例:

```sh
./build/linux-arm64-dev/MH3 --rom RMHJ08.rvz --village   # 初回起動（モガの村へ直行）
./build/linux-arm64-dev/MH3 --list quests                # クエスト一覧表示
./build/linux-arm64-dev/MH3 --quest 1000                 # クエスト直接起動
./build/linux-arm64-dev/MH3 --launcher                   # ランチャー UI 起動（開発ビルド）
```

---

## 関連ドキュメント

- [docs/vita-controls.md](docs/vita-controls.md) — コントローラ・タッチ・キーボード操作の詳細
- [docs/save-format.md](docs/save-format.md) — セーブデータ構造
- [docs/optimizations/README.md](docs/optimizations/README.md) — Cortex-A9 向け高速化・最適化手法の解説（上流ドキュメント）

---

## クレジット・ライセンス

- **ベースプロジェクト**: [Grarak/mh3-wii](https://github.com/Grarak/mh3-wii)（v0.3.1）
- **ライセンス**:
  - トランスレータおよび WiiCompiled 由来のコードは **GPLv3** です（[LICENSE](LICENSE), `toolchain/ORIGIN.md`, `toolchain/THIRD-PARTY-NOTICES.md`）。
  - Aurora レンダラは **MIT License** です（`aurora-main/LICENSE`）。
  - 組み込みマルチプレイヤーサーバー（`runtime/src/net/local_server*`）は [MH3 Server Project](https://github.com/sepalani/MH3SP) 由来であり、**AGPL-3.0-or-later** が適用されます。
- **免責事項**:
  - 『モンスターハンター3（トライ）』は株式会社カプコンの著作物・登録商標です。本プロジェクトは非公式の移植プロジェクトであり、権利元とは一切関係ありません。
