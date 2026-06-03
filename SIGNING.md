# SIGNING.md — macOS コード署名のセットアップ手順

このドキュメントは、farman の **macOS バイナリに Apple Developer ID 署名 +
公証 (Notarization) を CI で付与する**ためのセットアップ手順を、最初に踏んだ
人間の経験を踏まえた**操作可能なランブック**としてまとめたものです。

- 対象: GitHub Actions の `release.yml` から自動的に署名・公証する構成
  (v0.9.5〜実装済み)
- 想定読者: 証明書の更新、別マシンでの再 setup、メンバー追加等で同じ作業を
  もう一度やる人
- このドキュメントには**機密情報 (鍵・パスワード等) は一切含まれません**。
  Apple ID メール等の PII はプレースホルダ `<your-apple-id@example.com>` で
  記述します

> Windows 署名は未対応です (タスク追跡: SPEC.md「バックログ」/「リリースと
> 配布」参照)。本ドキュメントは macOS のみを扱います。

---

## 0. 全体像

最終的に CI (`release.yml`) は次の順序で署名・公証を行います:

```
1. MACOS_CERTIFICATE_BASE64 等の Secret が揃っているか判定
2. 一時キーチェーンに .p12 を import
3. macdeployqt で .app に Qt frameworks を埋め込む
4. codesign --force --deep --options runtime --timestamp --sign <ID> farman.app
5. create-dmg で farman.dmg を生成
6. codesign --force --timestamp --sign <ID> farman.dmg   ← DMG コンテナも署名
7. xcrun notarytool submit farman.dmg --wait
8. xcrun stapler staple farman.dmg
```

あなたがやるのは **Secret を repo に登録する**ところまで。CI は自動で進めます。

---

## 1. 前提

- **Apple Developer Program 加入** (法人または個人)
  - 法人なら **D-U-N-S 番号** + 法人名の Apple 登録が必要 (新規は数週間)
  - メンバーシップの組織名は証明書 (Developer ID Application: **<組織名>**)
    に焼き込まれるので、**実態と一致**していることを発行前に確認
- **Account Holder 権限** で Apple Developer Portal にサインインできること
  - Developer ID Application 証明書は Account Holder のみが発行可能
- ローカル Mac に **Xcode** または **Keychain Access** が使えること
- **gh CLI** (GitHub CLI) がインストール済 + 該当 repo に管理権限で認証済

---

## 2. Developer ID Application 証明書を作る

2 通りあります。Xcode が入っていれば方式 A が一番簡単。

### 方式 A: Xcode から

1. **Xcode → Settings (⌘,) → Accounts** タブ
2. 会社チームの Apple ID を追加 / 選択
3. 右下 **Manage Certificates...**
4. 左下の **`+`** → **Developer ID Application** を選択
5. 自動で証明書 + 秘密鍵が **login キーチェーン**に作られる

`+` のメニューに "Developer ID Application" が出ない場合は Account Holder 権限が
無い → 別の Apple ID で再ログイン、または方式 B へ。

### 方式 B: 証明書ポータル + CSR (確実 / Xcode 不要)

1. **キーチェーンアクセス**を起動 → メニュー
   **キーチェーンアクセス → 証明書アシスタント → 認証局に証明書を要求...**
2. 入力:
   - ユーザのメールアドレス: あなたのメール
   - 通称 (Common Name): 任意 (会社名等)
   - CA のメールアドレス: 空でよい
   - **「ディスクに保存」**を選択 → 続ける
3. `.certSigningRequest` ファイルを保存
   - ※このとき**秘密鍵がキーチェーンに生成される**。これが後で必須なので消さない
4. https://developer.apple.com/account → **Certificates, IDs & Profiles → Certificates → `+`**
5. **Developer ID Application** を選択 → Continue
6. 手順 3 の `.certSigningRequest` をアップロード → Continue
7. 発行された `.cer` をダウンロード
8. `.cer` を**ダブルクリック**してキーチェーンに登録 (手順 3 の秘密鍵と自動で対になる)

### 完了確認 (どちらの方式でも)

キーチェーンアクセス →
**「ログイン」キーチェーン → 「自分の証明書」** タブに

```
Developer ID Application: <組織名> (XXXXXXXXXX)
```

が出ていて、**▶ を展開すると秘密鍵がぶら下がっている**ことを確認します。
括弧内の 10 桁が **Team ID** です (この後で `MACOS_NOTARIZATION_TEAM_ID` に使う)。

---

## 3. `.p12` ファイルに書き出す

Secret に入れるのは「証明書 + 秘密鍵を 1 つに固めた `.p12`」を base64 化した
ものです。

1. キーチェーンアクセスの**「自分の証明書」タブ**で
   **Developer ID Application: ...** を選択
   - ⚠️ **「すべての項目」「証明書」タブから選ぶと書き出し時に
     `個人情報交換 (.p12)` がグレーアウトして選べない**。これは選択に秘密鍵が
     含まれていないため。必ず「自分の証明書」タブから選ぶこと
2. 右クリック → **「書き出す」**
3. ファイル形式: **個人情報交換 (.p12)** を選択
4. 保存場所とパスワードを設定 (パスワードは後で `MACOS_CERTIFICATE_PASSWORD`
   に入れる値。空にしないこと)

→ 例: `~/Documents/MyCompany_DeveloperID.p12`

---

## 4. アプリ用パスワード (Notarization 用) を発行する

公証 (notarytool) は通常の Apple ID パスワードではなく**アプリ用パスワード**を
使います。**`.p12` のパスワードとは完全に別物**です (実際この区別を最初に
取り違えて HTTP 401 を踏みました)。

| | 何 | 生成元 |
|---|---|---|
| `MACOS_CERTIFICATE_PASSWORD` | `.p12` を暗号化する自分で決めるパスワード | キーチェーンの書き出しダイアログ |
| `MACOS_NOTARIZATION_PASSWORD` | **Apple が発行する** `xxxx-xxxx-xxxx-xxxx` 形式 | https://account.apple.com → セキュリティ → アプリ用パスワード |

手順:

1. https://account.apple.com に、**Developer Team のメンバーである Apple ID**
   でサインイン
2. **サインインとセキュリティ → アプリ用パスワード**
3. **「+」/ アプリ用パスワードを生成** → ラベル (例: `farman notarization`)
4. 表示される `abcd-efgh-ijkl-mnop` をコピー
5. 一度閉じると再表示できないので、Secret に登録するまで保管

> 2 要素認証が有効な Apple ID でのみアプリ用パスワードが発行できます。
> Developer Program の Apple ID は通常 2FA 必須なので問題なし。

---

## 5. (推奨) ローカルで認証を事前検証

Secret 登録の**前に**、ローカルで `notarytool` に同じ値を渡して
Apple の公証サーバが受け付けるかを確認します。これをやれば CI で初回から
40 分待って 401 という事故を防げます。

```bash
xcrun notarytool history \
  --apple-id "<MACOS_NOTARIZATION_APPLE_ID と同じメール>" \
  --team-id "<10 桁 Team ID>" \
  --password "<アプリ用パスワード>"
```

- **「No submission history」**などが表示される → **認証 OK** (3 値が正しい)
- **HTTP 401** → 3 値のいずれかが間違っている (アプリ用パスワードと
  ログインパスワードの取り違えが最頻パターン)

---

## 6. GitHub Secrets に登録する

5 つの Secret を `mashsoft-jp/farman` の Actions secrets に登録します。

| Secret 名 | 値 |
|---|---|
| `MACOS_CERTIFICATE_BASE64` | 手順 3 で書き出した `.p12` を base64 化した文字列 |
| `MACOS_CERTIFICATE_PASSWORD` | `.p12` のパスワード (手順 3 で設定したもの) |
| `MACOS_NOTARIZATION_APPLE_ID` | Apple ID メール |
| `MACOS_NOTARIZATION_TEAM_ID` | 10 桁の Team ID (証明書名の括弧内) |
| `MACOS_NOTARIZATION_PASSWORD` | アプリ用パスワード (手順 4 で発行したもの) |

### 安全な登録コマンド

`.p12` の中身を**画面に出さず**Secret に流すパターン:

```bash
# 1. 証明書 (base64 を画面に出さずパイプで直接 Secret に)
base64 -i ~/Documents/MyCompany_DeveloperID.p12 \
  | gh secret set MACOS_CERTIFICATE_BASE64 --repo mashsoft-jp/farman

# 2. .p12 のパスワード (伏字入力)
gh secret set MACOS_CERTIFICATE_PASSWORD --repo mashsoft-jp/farman

# 3. Apple ID (秘密ではないので --body で OK)
gh secret set MACOS_NOTARIZATION_APPLE_ID --repo mashsoft-jp/farman \
  --body "<your-apple-id@example.com>"

# 4. Team ID (秘密ではない)
gh secret set MACOS_NOTARIZATION_TEAM_ID --repo mashsoft-jp/farman \
  --body "XXXXXXXXXX"

# 5. アプリ用パスワード (伏字入力)
gh secret set MACOS_NOTARIZATION_PASSWORD --repo mashsoft-jp/farman
```

`gh secret set` は引数なしで実行すると `Paste your secret:` と**伏字で**入力を
受け付けるので、シェル履歴にも残りません。

### 確認

```bash
gh secret list --repo mashsoft-jp/farman
```

5 つすべてが並んでいれば登録完了。値は表示されません (これも正常)。

---

## 7. CI でビルド + 検証する

### ビルドを走らせる

通常のリリースフロー (CLAUDE.md「リリースフロー」参照) に従い、
作業ブランチの先端に `v*-test` タグを打って push:

```bash
git tag -a v0.9.5-test -m "v0.9.5 test build (signing)"
git push origin v0.9.5-test
```

`release.yml` が自動で走り、Draft Release に署名済 DMG が添付されます
(macOS ジョブは 30〜40 分)。

### 出来上がりの DMG を検証する

Draft Release から DMG をダウンロードして、ローカルで:

```bash
DMG=./farman-v0.9.5-test-macos-arm64.dmg

# (1) 公証チケットが staple されているか
xcrun stapler validate "$DMG"
#   → "The validate action worked!" が出れば OK

# (2) DMG が Notarized Developer ID として受理されるか
spctl -a -t open --context context:primary-signature -vv "$DMG"
#   → "accepted, source=Notarized Developer ID" なら OK

# (3) DMG をマウントして中の .app を検証
MP=$(hdiutil attach "$DMG" -nobrowse -readonly | grep -o '/Volumes/.*' | head -1)
spctl -a -t exec -vvv "$MP/farman.app"
#   → "accepted, source=Notarized Developer ID, origin=Developer ID Application: ..." なら OK
codesign -dvv --verbose=4 "$MP/farman.app" | grep -E "Authority|TeamId"
#   → Authority に Developer ID + Apple Root CA、TeamIdentifier が一致
hdiutil detach "$MP"
```

3 つすべて pass すれば、**エンドユーザーが Gatekeeper 警告なしで開ける状態**です。

---

## 8. 既知の罠・FAQ

ここまでで踏んだ落とし穴をまとめます。最初に setup する人 (= 過去の自分) を
ハマらせないために。

### Q. `.p12` 書き出しで「個人情報交換 (.p12)」がグレーアウトする

A. キーチェーンの選択タブが「すべての項目」「証明書」になっている。
**「自分の証明書」タブから**証明書を選び直すと有効化される。`.p12` には
秘密鍵が必須で、これらのタブでは選択に鍵が含まれない。

### Q. CI の Notarize ステップで HTTP 401

A. ほぼ確実に `MACOS_NOTARIZATION_PASSWORD` に**アプリ用パスワード以外**
(`.p12` のパスワード or ログインパスワード) を入れている。手順 4 を再確認し、
手順 5 のローカル検証で 401 が出ないことを確認してから Secret を再設定。

### Q. CI の Notarize で `Error: HTTPError ... -1001 The request timed out`

A. Apple の公証サーバへの状態ポーリングが一時的にネットワーク詰まりで失敗
した。**アップロードは成功している**ことが多い。コード/署名は正常なので
`gh run rerun <run-id> --failed` で macOS ジョブだけ再実行すれば通る。

### Q. 配布 DMG が `spctl -t open` で "no usable signature" になる

A. DMG コンテナ自体が codesign されていない。`release.yml` の「Deploy
(macOS)」ステップで `create-dmg` の直後に
`codesign --force --timestamp --sign "$MACOS_IDENTITY_HASH" build/farman.dmg`
を入れること (v0.9.5〜実装済み)。公証 + staple だけだと `stapler validate` は
通るが `spctl` 評価では署名なし扱いになる。

### Q. macOS の CI ビルドが固まる (ハング → SIGTERM)

A. `cmake --build` に `--parallel` 無制限を渡しているのが原因。
**macOS arm64 ランナーは 3 vCPU / 7 GB と小さく**、`-O2` での重い翻訳単位
(Qt 依存) が同時多発するとメモリ逼迫 → clang がストール。`release.yml` で
**`--parallel 2`** に明示制限してある (Linux と同じ)。Windows ランナーは
メモリに余裕があり無制限可。

### Q. 証明書の組織名が現実の社名と違うまま発行された

A. Apple Developer メンバーシップの登録名が D&B レコード等とずれていると
発生する。Apple Developer サポートに**修正依頼 → 反映後に証明書を再発行**。
ズレたまま署名すると公開バイナリの "Developer ID Application: <ずれた社名>"
にそのまま現れる。修正は数日〜数週間かかることがあるので、署名前に
**Identities ページで表示される組織名を必ず確認**。

### Q. Apple Developer Program の更新 / 証明書の失効

A. Developer ID Application 証明書の有効期限は **5 年**。期限が近づいたら:
1. このドキュメントの手順 2 で**新しい証明書を作る** (旧証明書はしばらく併存可能)
2. 手順 3 で新しい `.p12` を書き出す
3. 手順 6 の `MACOS_CERTIFICATE_BASE64` / `MACOS_CERTIFICATE_PASSWORD` を
   新しい値で**上書き** (`gh secret set` は既存上書き)
4. v*-test で署名フローが通ることを再検証

Apple ID + Team ID + アプリ用パスワードは継続利用可能 (アプリ用パスワードを
失効させた場合は再発行 → `MACOS_NOTARIZATION_PASSWORD` を上書き)。

---

## 9. 関連ドキュメント

- [SPEC.md](SPEC.md) — 「リリースと配布」→「コード署名 (CI)」節 (リファレンス)
- [.github/workflows/release.yml](.github/workflows/release.yml) — 実装本体
- [CLAUDE.md](CLAUDE.md) — リリースフロー全体 (v*-test → 検証 → v* 正式)
- Apple: [Customizing the notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
