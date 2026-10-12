param([Parameter(Mandatory=$true)][ValidateSet('x64','Win32')][string]$Platform,[Parameter(Mandatory=$true)][string]$RefName)
$ErrorActionPreference='Stop'
$header=Get-Content -LiteralPath 'src/Version.h' -Raw
$version=[regex]::Match($header,'#define VIEWRESERVATION_VERSION_STRING "([0-9.]+)"').Groups[1].Value
if(!$version){throw 'Product version missing'}
if($RefName.StartsWith('v') -and $RefName -cne "v$version"){throw 'Tag differs from product version'}
$arch=if($Platform -eq 'Win32'){'x86'}else{'x64'}
$binary="bin/$Platform/Release/ViewReservation.tvtp"
if(!(Test-Path -LiteralPath $binary)){throw 'Plugin build output missing'}
if([Diagnostics.FileVersionInfo]::GetVersionInfo($binary).FileVersion -ne $version){throw 'Plugin resource version differs'}
if((Get-FileHash README.md).Hash -ne (Get-FileHash README.txt).Hash){throw 'README pair differs'}
$out="package/ViewReservation_v${version}_${arch}"
if(Test-Path -LiteralPath $out){throw 'Use a clean package staging directory'}
$null=New-Item -ItemType Directory -Path "$out/docs" -Force
Copy-Item -LiteralPath $binary -Destination $out
foreach($name in @('README.md','README.txt','LICENSE')){Copy-Item -LiteralPath $name -Destination $out}
Copy-Item -LiteralPath 'docs/Documentation_JP.txt' -Destination "$out/docs"
@"
ViewReservation v$version ($arch) - ビルド済み配布

TVTest 0.10.0以降で使用するプラグインです。TvAIrのPluginsフォルダへ配置するものではありません。
Visual Studioでのビルドは不要です。使用しているTVTestと同じアーキテクチャの配布物を選んでください。
TVTestを終了し、ViewReservation.tvtpをTVTestのPluginsフォルダへ配置します。
TVTestを起動し、番組表で番組を右クリックして視聴予約・録画予約を確認してください。
パネル機能は初期OFFです。プラグイン設定で「パネルを使用する」をONにしてTVTestを再起動してください。
更新時もTVTestを終了し、同じViewReservation.tvtpを置き換えます。既存ViewReservation.iniを残すと予約とパネル設定を引き継げます。
詳しい使用方法はREADMEとdocs/Documentation_JP.txtを参照してください。
"@ | Set-Content -LiteralPath "$out/INSTALL.txt" -Encoding utf8
Compress-Archive -Path "$out/*" -DestinationPath "package/ViewReservation_v${version}_${arch}.zip"
