#pragma once

#include <QString>
#include <QStringList>
#include <QList>

class QJsonObject;

namespace Farman {

// 外部アプリ呼び出しを表す 1 エントリ。
//
// 「ターミナル」「テキストエディタ」もこの型の builtin=true 特殊エントリとして
// 扱う。フェーズ 1 は組み込み 2 件のみ UI に出すが、データモデルとしては
// 任意件数のエントリを保持できる (フェーズ 2 でユーザー定義の追加 UI を被せる前提)。
struct UserCommand {
  // CommandRegistry に登録するコマンド ID。
  // - builtin: "user.cmd.terminal" / "user.cmd.editor" で固定
  // - 任意:    "user.cmd.<uuid>"
  QString id;

  // 表示名 (Tools メニュー / Settings 一覧の見出し)
  QString name;

  // 実行ファイル (絶対パス or PATH 上のバイナリ名)
  QString program;

  // 引数テンプレート。1 引数 = 1 要素。プレースホルダ ({dir}/{file}/{otherDir}/
  // {files}/{name}/{ext}) を含むものは PlaceholderExpander が展開する。
  QStringList argsTemplate;

  // 作業ディレクトリのテンプレート。空ならアクティブペインの cwd を使う。
  QString workingDirTemplate;

  // Tools メニューに項目を出すか。
  bool showInToolsMenu = true;

  // 組み込み (削除不可・ID 固定) かどうか。
  bool builtin = false;

  // builtin 種別: "terminal" / "editor" / 空。Settings → General タブの
  // 専用 UI とのバインドキーとして使う。
  QString builtinKind;
};

// JSON ↔ UserCommand。入力が壊れていても落ちないように緩めにパースする。
QJsonObject userCommandToJson(const UserCommand& cmd);
UserCommand userCommandFromJson(const QJsonObject& obj);

// Ubuntu 24.04 LTS / macOS / Windows それぞれの環境向けに、組み込みターミナル・
// テキストエディタの初期定義を返す。順序は { terminal, editor }。
// Linux 系の判定は Q_OS_LINUX 全般。Ubuntu 以外でも動く可能性のある最大公約数
// (gnome-terminal / gnome-text-editor) を選んでいる。動かない場合はユーザーが
// Settings で書き換える前提。
QList<UserCommand> defaultBuiltinUserCommands();

// 組み込みコマンド (builtinKind = "terminal" / "editor") の表示名を、現在の UI
// 言語で翻訳して返す。名前は settings.json にも保存されるが、保存された値は生成
// 時の言語で固定されてしまうため、表示・ロード時は必ずこの関数で再導出する。
// 訳語は Farman::ExternalAppsTab コンテキストの "Terminal" / "Text Editor" を共有。
QString builtinUserCommandName(const QString& kind);

// 引数テンプレートの 1 行表記 (ユーザーが UI で入力する形) と QStringList の相互変換。
// `QProcess::splitCommand()` ベースで、シェル風のクォート (`"..."`, `'...'`) を尊重する。
QString     joinArgsTemplate(const QStringList& args);
QStringList splitArgsTemplate(const QString& line);

} // namespace Farman
