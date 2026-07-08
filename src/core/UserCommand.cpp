#include "UserCommand.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>

namespace Farman {

QJsonObject userCommandToJson(const UserCommand& cmd) {
  QJsonObject obj;
  obj["id"]                 = cmd.id;
  obj["name"]               = cmd.name;
  obj["program"]            = cmd.program;
  QJsonArray args;
  for (const QString& a : cmd.argsTemplate) args.append(a);
  obj["argsTemplate"]       = args;
  obj["workingDirTemplate"] = cmd.workingDirTemplate;
  obj["showInToolsMenu"]    = cmd.showInToolsMenu;
  if (cmd.builtin) {
    obj["builtin"]     = true;
    obj["builtinKind"] = cmd.builtinKind;
  }
  return obj;
}

UserCommand userCommandFromJson(const QJsonObject& obj) {
  UserCommand cmd;
  cmd.id                  = obj.value("id").toString();
  cmd.name                = obj.value("name").toString();
  cmd.program             = obj.value("program").toString();
  cmd.workingDirTemplate  = obj.value("workingDirTemplate").toString();
  cmd.showInToolsMenu     = obj.value("showInToolsMenu").toBool(true);
  cmd.builtin             = obj.value("builtin").toBool(false);
  cmd.builtinKind         = obj.value("builtinKind").toString();

  // 組み込みコマンドの表示名は、保存された値 (生成時の言語で固定) ではなく
  // 現在の UI 言語で再導出する。これで日本語環境で保存後に英語で起動しても
  // Tools メニュー等が英語表示になる。
  if (cmd.builtin) {
    const QString localized = builtinUserCommandName(cmd.builtinKind);
    if (!localized.isEmpty()) cmd.name = localized;
  }

  const QJsonArray args = obj.value("argsTemplate").toArray();
  for (const QJsonValue& v : args) {
    cmd.argsTemplate.append(v.toString());
  }
  return cmd;
}

QString builtinUserCommandName(const QString& kind) {
  // 訳語は Farman::ExternalAppsTab コンテキストの既存エントリを共有する
  // (translate の第 1 引数をそのコンテキスト名に合わせている)。
  if (kind == QLatin1String("terminal"))
    return QCoreApplication::translate("Farman::ExternalAppsTab", "Terminal");
  if (kind == QLatin1String("editor"))
    return QCoreApplication::translate("Farman::ExternalAppsTab", "Text Editor");
  return QString();
}

QList<UserCommand> defaultBuiltinUserCommands() {
  UserCommand terminal;
  terminal.id              = QStringLiteral("user.cmd.terminal");
  terminal.name            = builtinUserCommandName(QStringLiteral("terminal"));
  terminal.builtin         = true;
  terminal.builtinKind     = QStringLiteral("terminal");
  terminal.showInToolsMenu = true;

  UserCommand editor;
  editor.id              = QStringLiteral("user.cmd.editor");
  editor.name            = builtinUserCommandName(QStringLiteral("editor"));
  editor.builtin         = true;
  editor.builtinKind     = QStringLiteral("editor");
  editor.showInToolsMenu = true;

#if defined(Q_OS_MAC)
  // macOS: open(1) でユーザーが選んだデフォルトアプリに任せず、Apple 標準の
  // Terminal.app / TextEdit.app を明示する。{dir} や {file} は引数で渡し、
  // 作業ディレクトリは指定しない (open 自体が対象アプリ側に伝える)。
  terminal.program       = QStringLiteral("/usr/bin/open");
  terminal.argsTemplate  = { QStringLiteral("-a"), QStringLiteral("Terminal"),
                             QStringLiteral("{dir}") };
  editor.program         = QStringLiteral("/usr/bin/open");
  editor.argsTemplate    = { QStringLiteral("-a"), QStringLiteral("TextEdit"),
                             QStringLiteral("{file}") };
#elif defined(Q_OS_WIN)
  // Windows: 標準コマンドプロンプトを開きつつ、{dir} へ cd するワンライナー。
  // /K = 実行後シェル維持、/D = ドライブ越え対応。
  terminal.program       = QStringLiteral("cmd.exe");
  terminal.argsTemplate  = { QStringLiteral("/K"), QStringLiteral("cd"),
                             QStringLiteral("/D"), QStringLiteral("{dir}") };
  editor.program         = QStringLiteral("notepad");
  editor.argsTemplate    = { QStringLiteral("{file}") };
#else
  // Linux: Ubuntu 24.04 LTS を基準。gnome-terminal は GNOME 標準。
  // gnome-text-editor は 22.10 以降の標準テキストエディタ (gedit の後継)。
  // 他ディストリ / DE では存在しない可能性があるが、その場合は Settings で
  // ユーザーが書き換える前提。
  terminal.program       = QStringLiteral("gnome-terminal");
  terminal.argsTemplate  = { QStringLiteral("--working-directory={dir}") };
  editor.program         = QStringLiteral("gnome-text-editor");
  editor.argsTemplate    = { QStringLiteral("{file}") };
#endif

  return { terminal, editor };
}

QString joinArgsTemplate(const QStringList& args) {
  // 1 引数を 1 要素として保持しているので、表示用には空白区切りに戻す。
  // 引数自体に空白を含むものはダブルクォートで囲み、内部の `"` はバックスラッシュで
  // エスケープする (再分割時に splitCommand が解釈できる形)。
  QStringList parts;
  parts.reserve(args.size());
  for (const QString& a : args) {
    const bool needsQuote = a.contains(QLatin1Char(' '))
                          || a.contains(QLatin1Char('\t'))
                          || a.isEmpty();
    if (!needsQuote) {
      parts << a;
      continue;
    }
    QString escaped = a;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    parts << (QLatin1Char('"') + escaped + QLatin1Char('"'));
  }
  return parts.join(QLatin1Char(' '));
}

QStringList splitArgsTemplate(const QString& line) {
  // QProcess::splitCommand はシェル風のクォート / エスケープを処理してくれる。
  // 空文字列でも空リストを返すだけなので呼び出し側で扱えばよい。
  return QProcess::splitCommand(line);
}

} // namespace Farman
