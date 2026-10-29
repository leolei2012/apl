# APL 根源码与本地版本快照

APL业务只在工程根 `src/`、`include/`、`config/` 开发。`apl_sdk_template/air-care/<版本>` 是供Web配置、真实C仿真和SDK交付使用的冻结快照，不是另一份开发库。

## 版本契约

- `template.json.version` 必须等于目录名，产品绑定精确的 `id + version`。
- `sourceVersion` 必须等于快照 `include/apl.h` 的 `APL_VERSION_STRING`。根库的APL/core/HMI版本宏和三个C文件的版本静态断言在发布检查点同步。
- 产品的 `sdkVersion` 是用户自己的交付版本，与模板版本、根源码版本分别记录。
- 已验证并提交的版本目录保持冻结。修改根库后创建新版本，不覆盖旧目录，也不批量修改产品数据库。

## 本地0.1.1

`apl.air-care@0.1.1` 固定APL 0.1.1，包含纯净化自动档修复：PM2.5≤35输出睡眠风速、36–75一档、76–150二档、>150三档；请求仍为Auto，sleep保持0，不触发睡眠熄屏。已有多传感器取最差规则未重写，加湿器/一体机的新表差异仍待独立修订。

根库9个文件逐字节复制，另从根 `tests/test_sdk_release.c` 固定消费验证程序。`source-snapshot.json` 记录这些来源文件的SHA-256。模板声明自己的消费程序，生成器在其前面加入产品能力、默认值和参数宏的静态断言，再由同一构建/执行/下载门禁验证。导出的CMake测试也使用该消费程序。

新版本目录内的 `.gitattributes` 使用 `-text` 保留冻结时的原始字节，避免Git自动换行转换改变来源哈希；空白规则识别合法CRLF行尾并保留其他空白检查。该设置只作用于新版本，不改旧目录或仓库全局配置。

0.1.0保留原源码、默认消费程序及行为。仿真桥接明确支持sourceVersion 0.1.0和0.1.1，不默认接受未来未知版本。

## 从根库冻结新版本

先完成根C回归，再统一版本宏、版本断言与消费程序的版本检查。下面是在工程根目录、PowerShell 7中创建新目录的步骤。示例0.1.1已存在，直接运行会拒绝覆盖；后续改用新版本，并选择上一已冻结版本作包装骨架。

```powershell
$version = '0.1.1'
$base = (Resolve-Path -LiteralPath 'apl_sdk_template/air-care/0.1.0').Path
$target = Join-Path (Get-Location) "apl_sdk_template/air-care/$version"
if (Test-Path -LiteralPath $target) { throw '版本已存在，禁止覆盖' }
$sourceVersion = [regex]::Match((Get-Content -LiteralPath include/apl.h -Raw), 'APL_VERSION_STRING\s+"([^"]+)"').Groups[1].Value
if ($sourceVersion -ne $version) { throw '先同步并验证根库版本' }
Copy-Item -LiteralPath $base -Destination $target -Recurse
Set-Content -LiteralPath (Join-Path $target '.gitattributes') -Value '* -text whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol' -Encoding ascii
$files = @('include/apl.h','include/apl_core.h','include/apl_hmi.h','include/apl_type.h',
  'config/apl_core_config.h','config/apl_hmi_config.h','src/apl.c','src/apl_core.c','src/apl_hmi.c')
$hashes = [ordered]@{}
foreach ($path in $files) {
  Copy-Item -LiteralPath $path -Destination (Join-Path $target $path) -Force
  $hashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ((Get-FileHash -LiteralPath (Join-Path $target $path)).Hash.ToLowerInvariant() -ne $hashes[$path]) { throw "复制不一致：$path" }
}
New-Item -ItemType Directory -Path (Join-Path $target 'verification') -Force | Out-Null
Copy-Item -LiteralPath tests/test_sdk_release.c -Destination (Join-Path $target 'verification/consumer.c') -Force
$metadata = Get-Content -LiteralPath (Join-Path $base 'template.json') -Raw | ConvertFrom-Json
$metadata.version = $version
$metadata.sourceVersion = $sourceVersion
$metadata.description = "基于根 APL $sourceVersion 快照；修正纯净化自动风速，保留加湿与一体机现有规则。"
$metadata.directories = @($metadata.directories + 'verification' | Select-Object -Unique)
$metadata.staticFiles = @($metadata.staticFiles + 'source-snapshot.json' | Select-Object -Unique)
$verification = [ordered]@{ sourceDirectories=@('src','port','examples'); includeDirectories=@('include','config','port');
  consumer='verification/consumer.c'; scope='主控运行、手动风速；纯净化 PM2.5 自动档边界和 Auto/sleep 语义（其他机型规则尚未按新表修订）' }
$metadata | Add-Member -NotePropertyName verification -NotePropertyValue $verification -Force
$metadata | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $target 'template.json') -Encoding utf8
[ordered]@{ formatVersion=1; templateVersion=$version; sourceVersion=$sourceVersion; rootFiles=$hashes;
  validationSource=[ordered]@{ path='tests/test_sdk_release.c'; sha256=(Get-FileHash -LiteralPath tests/test_sdk_release.c).Hash.ToLowerInvariant() }
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $target 'source-snapshot.json') -Encoding utf8
```

包装骨架的adapter、port和examples继续沿用现有接口，本轮没有重写业务。新版本在验证/提交前属于候选；若检查发现问题，只同步当前候选，绝不改写已冻结旧版。验证至少覆盖：根C三机型、新旧版本列表、临时产品显式绑定、真实C传感器输入/负载、SDK编译和消费、manifest/文件哈希、下载，以及旧版行为保留。若桥接不支持新的sourceVersion，应明确增加兼容并验证，不放宽为任意版本。

## 显式选择或升级

服务启动时读取模板；新增本地目录后需重启Web服务才能出现在版本列表。测试和验收使用独立临时数据，不能为升级模板而改写真实`.data`。

新产品：在“源码模板”选择明确标为 `apl.air-care@0.1.1` 的卡片创建产品。旧产品：在“产品概览”显式选择0.1.1，确认草稿规范化提示，再保存为新修订；未保存时仍使用旧修订。重启仿真后才应用新快照，SDK任务固定各自产品修订/模板版本。旧成功归档保留原版本，可以继续下载。没有自动迁移、隐式升级或外部发布步骤。

## 本地接入检查点（2026-10-29）

- 根C库三机型的核心、联动、可选键、SDK消费程序共12组通过。
- 当前工作树完整check通过34项，包含4项仍暂停且未提交的组合键测试，不作为其新规格验收。
- 仅导出拟提交暂存内容到独立临时目录，排除组合键界面/动作/测试，复用已安装依赖；类型检查、生产构建和30项测试全部通过，失败0、跳过0。暂存的桥接变更只有sourceVersion兼容行。
- 临时产品分别绑定0.1.0/0.1.1，通过真实HTTP运行各自C进程、比较PM边界和Auto/sleep状态，再生成并消费各自SDK、校验来源/归档哈希与下载。故意篡改生成配置时编译被拦截，证明专项消费验证保留产品配置静态断言。
- 独立导出首次暴露Git自动换行导致的来源哈希不一致；增加新版本目录的精确字节属性后，导出9个根文件的哈希和完整检查均通过。0.1.0目录未修改，真实产品数据库未打开或迁移。
