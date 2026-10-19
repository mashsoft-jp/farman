#pragma once

#include <QtGlobal>

namespace Farman {
namespace RestartSupport {

// 再起動で起動された新プロセスに渡す隠しコマンドラインオプション名 ("--wait-pid <pid>")。
// 値は再起動を要求した旧プロセスの PID。
constexpr const char* kWaitPidOption = "wait-pid";

// pid のプロセスが終了するまで待つ (最長 timeoutMs)。pid が存在しない / 既に終了して
// いればすぐ戻る。
//
// 再起動は「新プロセスを起動 → 旧プロセスを即終了」の順で行うため、新プロセスが main()
// に入った時点では旧プロセスがまだ生きていることがある。そのまま続けると
//   - 単一インスタンス判定が旧プロセスを検出し、「既に起動中」として即終了してしまう
//   - Windows では旧プロセスが掴んだままのプラグイン DLL を、起動時の導入 / 更新 / 削除
//     (PluginInstaller::applyPending) が差し替え・削除できない
// ため、新プロセスは旧プロセスの終了を待ってから起動処理を進める。
void waitForProcessExit(qint64 pid, int timeoutMs = 15000);

}  // namespace RestartSupport
}  // namespace Farman
