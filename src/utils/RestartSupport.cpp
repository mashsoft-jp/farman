#include "RestartSupport.h"

#include <QDeadlineTimer>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <sys/types.h>
#endif

namespace Farman {
namespace RestartSupport {

void waitForProcessExit(qint64 pid, int timeoutMs) {
  if (pid <= 0) {
    return;
  }
#ifdef Q_OS_WIN
  // 既に終了している / 開けない場合 (権限や存在しない pid) は待たずに戻る。
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (!h) {
    return;
  }
  WaitForSingleObject(h, static_cast<DWORD>(timeoutMs));
  CloseHandle(h);
#else
  // kill(pid, 0) は存在確認だけを行う (シグナルは送らない)。ESRCH = もう居ない。
  // EPERM は「居るが権限が無い」なので待ち続ける対象 (= 他ユーザーの同 pid は
  // 現実には起きないが、安全側に倒して timeout で打ち切る)。
  QDeadlineTimer deadline(timeoutMs);
  while (!deadline.hasExpired()) {
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      return;
    }
    QThread::msleep(25);
  }
#endif
}

}  // namespace RestartSupport
}  // namespace Farman
