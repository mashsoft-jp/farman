; ----------------------------------------------------------------------------
;  farman Windows installer (Inno Setup 6 script)
;
;  CI / ローカルから呼び出すコマンド例:
;    iscc /DAppVersion=0.9.0 /DSrcDir=build\Release windows\farman.iss
;
;  /DAppVersion:  製品バージョン (省略時は 0.0.0)
;  /DSrcDir:      windeployqt + vcpkg DLL コピー後の Release ディレクトリ
;                 (省略時は ..\build\Release。リポジトリルートから ISCC を
;                  叩く場合は build\Release を指定する)
;
;  Authenticode 署名は本スクリプトでは扱わない。SPEC.md "コード署名" 節の
;  通り、配布数が増えてから signtool 経由で追加対応する想定。
; ----------------------------------------------------------------------------

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef SrcDir
  #define SrcDir "..\build\Release"
#endif

[Setup]
; 一意な AppId を固定する (アップデート時の同一インストール識別キー)。
; 一度決めたら変更しないこと — 変えるとアップグレード時に二重インストール扱いになる。
AppId={{E8B18C7E-539D-47E2-8B43-84784FD85538}
AppName=farman
AppVersion={#AppVersion}
AppVerName=farman {#AppVersion}
AppPublisher=Mashsoft Inc.
AppPublisherURL=https://github.com/mashsoft-jp/farman
AppSupportURL=https://github.com/mashsoft-jp/farman/issues
AppUpdatesURL=https://github.com/mashsoft-jp/farman/releases

; {autopf} は admin 時 = Program Files、user 時 = LocalAppData\Programs を解決する。
; PrivilegesRequiredOverridesAllowed=dialog でユーザーに選ばせる。
DefaultDirName={autopf}\farman
DefaultGroupName=farman
DisableProgramGroupPage=yes

; 生成物名: farman-setup-windows-x64.exe (release.yml 側でタグ付きにリネーム)
OutputBaseFilename=farman-setup-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; 個別ユーザー権限でも入れられるよう、デフォルトは lowest。起動時のダイアログで
; 「すべてのユーザーにインストール」を選ぶと管理者昇格 + Program Files へ。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

; 64-bit のみ (32-bit Windows サポートはしない)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; インストーラ自身のアイコン (Add/Remove Programs にも表示される)
SetupIconFile=..\images\icon.ico
UninstallDisplayIcon={app}\farman.exe
UninstallDisplayName=farman {#AppVersion}

; インストール完了時の readme / ライセンス表示は省略 (将来追加してもよい)

[Languages]
Name: "english";  MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
; デスクトップショートカットは任意 (デフォルト OFF)
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; windeployqt + vcpkg DLL コピー後の Release ディレクトリ一式を {app} に展開。
; recursesubdirs で imageformats / platforms 等のプラグインディレクトリも丸ごとコピー。
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; スタートメニューは常に作成
Name: "{autoprograms}\farman"; Filename: "{app}\farman.exe"
; デスクトップショートカットは [Tasks] で選ばれたときのみ
Name: "{autodesktop}\farman";  Filename: "{app}\farman.exe"; Tasks: desktopicon
; アンインストーラへのスタートメニューエントリ (DisableProgramGroupPage=yes でも有効)
Name: "{autoprograms}\farman アンインストール"; Filename: "{uninstallexe}"

[Run]
; インストール完了後、optional に farman を起動するチェックボックスを出す。
; silent モード時 (/SILENT) はスキップ。
Filename: "{app}\farman.exe"; Description: "{cm:LaunchProgram,farman}"; Flags: nowait postinstall skipifsilent
