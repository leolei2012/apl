// Excel is generated from the APL-owned field definition. No Studio dependency.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const modules = process.env.APL_ARTIFACT_MODULES;
if (!modules) throw new Error('Set APL_ARTIFACT_MODULES to the bundled node_modules directory.');
const require = createRequire(path.join(path.resolve(modules), '..', 'apl-workbook.cjs'));
const { Workbook, SpreadsheetFile, FileBlob } = await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const directory = path.join(root, 'apl_lib/parameters');
const schema = JSON.parse(await fs.readFile(path.join(directory, 'profile.schema.json'), 'utf8'));
if (process.argv.includes('--preview-existing')) {
  const existing = await SpreadsheetFile.importXlsx(await FileBlob.load(path.join(directory, 'APL参数表.xlsx')));
  const destination = process.env.APL_WORKBOOK_PREVIEW;
  if (!destination) throw new Error('Set APL_WORKBOOK_PREVIEW for read-only previews.');
  await fs.mkdir(destination, { recursive: true });
  for (const sheetName of ['参数配置', '产品信息', '选项说明']) {
    const preview = await existing.render({ sheetName, autoCrop: 'all', scale: 1, format: 'png' });
    await fs.writeFile(path.join(destination, `${sheetName}.png`), new Uint8Array(await preview.arrayBuffer()));
  }
  console.log('Rendered existing template without editing.');
  process.exit(0);
}
const workbook = Workbook.create();
const params = workbook.worksheets.add('参数配置');
const metadata = workbook.worksheets.add('产品信息');
const options = workbook.worksheets.add('选项说明');

function base(sheet, title, end, widths) {
  sheet.showGridLines = false;
  const body = sheet.getRange(`A1:${end}`);
  body.format.font.name = 'Arial';
  body.format.font.size = 11;
  body.format.font.color = '#202A36';
  body.format.rowHeight = 30;
  body.format.verticalAlignment = 'center';
  body.format.wrapText = false;
  sheet.getRange('A2').values = [[title]];
  sheet.getRange('A2').format.font.size = 16;
  sheet.getRange('A2').format.font.bold = true;
  widths.forEach((width, index) => {
    sheet.getRangeByIndexes(0, index, Number(end.match(/\d+/)[0]), 1).format.columnWidthPx = width;
  });
}
function header(sheet, address, values) {
  const range = sheet.getRange(address);
  range.values = [values];
  range.format.fill = '#253B55';
  range.format.font.color = '#FFFFFF';
  range.format.font.bold = true;
  range.format.horizontalAlignment = 'center';
  range.format.rowHeight = 32;
}

const lastParameterRow = 5 + schema.parameters.length;
base(params, 'APL 产品参数表', `I${lastParameterRow}`, [100, 205, 235, 90, 90, 110, 145, 90, 500]);
params.tabColor = '#253B55';
params.getRange('A3').values = [[`APL ${schema.aplVersion} 开发版 · 参数格式 v${schema.schemaVersion} · 定义修订 ${schema.schemaRevision}`]];
params.getRange('A4').values = [['黄色为可填写参数；产品身份与版本见“产品信息”，位掩码见“选项说明”。']];
params.getRange('A4').format.font.color = '#586579';
header(params, 'A5:I5', ['分组', '参数名称', '参数编号', '参数值', '单位', 'C 类型', '允许范围', '固件默认值', '说明']);
params.getRange(`A6:I${lastParameterRow}`).values = schema.parameters.map(p => [p.group, p.name, p.key, p.default,
  p.unit || '—', p.cType, p.min === p.max ? String(p.min) : `${p.min}～${p.max}`, p.default, p.description]);
params.getRange(`D6:D${lastParameterRow}`).setNumberFormat('0');
params.getRange(`H6:H${lastParameterRow}`).setNumberFormat('0');
params.getRange(`D6:D${lastParameterRow}`).format.horizontalAlignment = 'right';
params.getRange(`H6:H${lastParameterRow}`).format.horizontalAlignment = 'right';
for (let i = 0; i < schema.parameters.length; i++) {
  const p = schema.parameters[i], row = i + 6;
  if (i % 2 === 1) params.getRange(`A${row}:I${row}`).format.fill = '#F3F5F8';
  const cell = params.getRange(`D${row}`);
  cell.format.fill = p.min === p.max ? '#E8ECF1' : '#FFF0BE';
  cell.format.font.color = p.min === p.max ? '#202A36' : '#174EA6';
  params.dataValidations.add({ range: `D${row}`, rule: { type: 'whole', operator: 'between', formula1: p.min, formula2: p.max } });
}
params.freezePanes.freezeRows(5);
params.freezePanes.freezeColumns(3);

base(metadata, '产品与版本信息', 'C22', [220, 490, 570]);
metadata.getRange('A3').values = [['当前是全功能固件默认参数模板；复制后填写产品名称和配置修订号。']];
header(metadata, 'A5:C5', ['字段', '内容', '说明']);
const info = [
  ['product_name', '待填写', '产品名称，由产品负责人填写'],
  ['config_revision', 1, '本产品参数修订号，修改并发布后递增'],
  ['schema_id', schema.schemaId, '参数模型标识'],
  ['schema_version', schema.schemaVersion, '决定字段含义和解码规则'],
  ['schema_revision', schema.schemaRevision, '同一参数格式的定义修订'],
  ['apl_version', schema.aplVersion, '目标 APL 业务版本'],
  ['release_status', schema.releaseStatus, 'development 表示开发中，尚未发布'],
  ['repository', schema.source.repository, '源码与参数表在同一 Git 仓库管理'],
  ['git_tag', schema.source.tag, '仓库模板留空；Studio 导出产品表时填写所选 Tag'],
  ['git_commit', schema.source.commit, '仓库模板留空；Studio 导出产品表时记录所选完整提交号'],
  ['transport', schema.transport.candidate, '计划使用 Modbus RTU，通信实现尚未接入'],
  ['register_map', schema.transport.registerMapStatus, '地址尚未分配；本表不能直接用于烧录'],
  ['source', `${schema.source.repository} · apl_lib/include/apl_core.h`, '字段和约束对应 apl_profile、apl_profile_validate'],
  ['dependency', '净化、加湿至少启用一项', '空气传感器依赖净化；湿度传感器依赖加湿'],
  ['apply', '冷启动加载', '整表校验后保存；传输、掉电安全与重启由宿主负责'],
  ['storage', '参数与运行记忆分开', '本表不包含已用滤网寿命、临时状态或 Flash 地址'],
  ['pid', schema.identity.pid.default, schema.identity.pid.description],
];
metadata.getRange('A6:C22').values = info;
metadata.getRange('B6:B7').format.fill = '#FFF0BE';
metadata.getRange('B6:B7').format.font.color = '#174EA6';
metadata.getRange('B7').setNumberFormat('0');
metadata.getRange('B22').setNumberFormat('@');
metadata.getRange('B22').format.fill = '#FFF0BE';
metadata.getRange('B22').format.font.color = '#174EA6';
metadata.getRange('A22:C22').format.wrapText = true;
metadata.getRange('A22:C22').format.rowHeight = 60;
metadata.getRange('B9:B10').setNumberFormat('0');
metadata.dataValidations.add({ range: 'B7', rule: { type: 'whole', operator: 'between', formula1: 1, formula2: 2147483647 } });

base(options, '枚举与位掩码', 'D20', [205, 125, 200, 490]);
options.getRange('A3').values = [['开关用 0／1；位掩码将需要启用的位值相加，各位只计一次。']];
header(options, 'A5:D5', ['参数', '数值／位值', '含义', '使用说明']);
const choices = [
  ['开关', 0, '关闭', 'purify_enabled、humidify_enabled、turbo_enabled 等'],
  ['开关', 1, '开启', '至少保留净化或加湿之一'],
  ['风档', 0, '睡眠风速', 'default_fan_gear 同时表示进入睡眠模式'],
  ['风档', 1, '一档', ''], ['风档', 2, '二档', ''], ['风档', 3, '三档', ''],
  ['风档', 4, '自动', '仅 default_fan_gear 支持'],
  ['modules', 1, 'UV', '模块位可组合；0 为关闭全部辅助模块'],
  ['modules', 2, '负离子', '示例：UV＋Wi-Fi = 1＋4 = 5'],
  ['modules', 4, 'Wi-Fi', ''],
  ['sensors', 1, 'PM2.5', '依赖净化能力'], ['sensors', 2, 'PM10', '依赖净化能力'],
  ['sensors', 4, '甲醛', '依赖净化能力'], ['sensors', 8, 'TVOC', '依赖净化能力'],
  ['sensors', 16, '湿度', '依赖加湿能力；五类全选为 31'],
];
options.getRange('A6:D20').values = choices;
options.getRange('B6:B20').setNumberFormat('0');
options.freezePanes.freezeRows(5);

// Mapping changes semantic actions only; these are the host's fixed input slots.
const keyMapping = workbook.worksheets.add('按键映射');
const mappingSchema = schema.keyMapping;
base(keyMapping, 'APL 内部按键映射', 'J22', [110, 180, 120, 200, 120, 200, 32, 90, 200, 480]);
keyMapping.getRange('A3').values = [['自定义映射']];
keyMapping.getRange('B3').values = [[Number(mappingSchema.default.enabled)]];
keyMapping.getRange('C3').values = [['0 使用标准行为；1 使用下方映射']];
keyMapping.getRange('B3').format.fill = '#FFF0BE';
keyMapping.getRange('B3').format.font.color = '#174EA6';
keyMapping.getRange('B3').setNumberFormat('0');
keyMapping.dataValidations.add({ range: 'B3', rule: { type: 'whole', operator: 'between', formula1: 0, formula2: 1 } });
keyMapping.getRange('A4').values = [['只改变 APL 内部短按和长按动作；宿主输入编号、GPIO 绑定、组合键输入保持不变。']];
keyMapping.getRange('A4').format.font.color = '#586579';
header(keyMapping, 'A6:F6', ['输入编号', '原始输入', '短按动作 ID', '短按动作', '长按动作 ID', '长按动作']);
header(keyMapping, 'H6:J6', ['动作 ID', '动作名称', '含义']);
keyMapping.getRange('A7:F13').values = mappingSchema.slots.map(slot => [slot.id, slot.name,
  mappingSchema.default.short[slot.id], null, mappingSchema.default.long[slot.id], null]);
keyMapping.getRange('H7:J22').values = mappingSchema.actions.map(action => [action.id, action.name, action.description]);
keyMapping.getRange('D7').formulas = [['=VLOOKUP(C7,$H$7:$J$22,2,FALSE)']];
keyMapping.getRange('D7:D13').fillDown();
keyMapping.getRange('F7').formulas = [['=VLOOKUP(E7,$H$7:$J$22,2,FALSE)']];
keyMapping.getRange('F7:F13').fillDown();
for (const range of ['C7:C13', 'E7:E13']) {
  keyMapping.getRange(range).format.fill = '#FFF0BE';
  keyMapping.getRange(range).format.font.color = '#174EA6';
  keyMapping.getRange(range).setNumberFormat('0');
  keyMapping.dataValidations.add({ range, rule: { type: 'whole', operator: 'between', formula1: 0, formula2: mappingSchema.actions.length - 1 } });
}
keyMapping.getRange('A7:A13').setNumberFormat('0');
keyMapping.getRange('H7:H22').setNumberFormat('0');
keyMapping.getRange('A15').values = [['未编入或未启用的动作不执行；0 无动作不唤醒。事件仍显示原始输入名称。']];
keyMapping.getRange('A16').values = [['来源：APL 参数格式 v6；动作编号对应 apl_key_action，输入编号对应 apl_hmi_key_id。']];
keyMapping.getRange('A15:A16').format.font.color = '#586579';

workbook.recalculate();
// Verify the exact editable values before exporting.
const values = params.getRange(`C6:D${lastParameterRow}`).values;
if (JSON.stringify(values) !== JSON.stringify(schema.parameters.map(p => [p.key, p.default]))) throw new Error('Workbook parameter mismatch');
if (JSON.stringify(keyMapping.getRange('C7:C13').values.flat()) !== JSON.stringify(mappingSchema.default.short)
  || JSON.stringify(keyMapping.getRange('E7:E13').values.flat()) !== JSON.stringify(mappingSchema.default.long)) throw new Error('Workbook key mapping mismatch');
// Exercise an editable ID and restore it before delivering the default table.
keyMapping.getRange('C7').values = [[1]];
if (keyMapping.getRange('D7').values[0][0] !== mappingSchema.actions[1].name) throw new Error('Workbook action lookup did not recalculate');
keyMapping.getRange('C7').values = [[mappingSchema.default.short[0]]];
workbook.recalculate();
const formulaErrors = await workbook.inspect({ kind: 'match', searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!', options: { useRegex: true, maxResults: 20 }, maxChars: 1000 });
console.log(formulaErrors.ndjson);
const file = await SpreadsheetFile.exportXlsx(workbook);
await file.save(path.join(directory, 'APL参数表.xlsx'));
// Export diagnostics are support output, not part of the versioned parameter set.
await fs.rm(path.join(directory, 'APL参数表.xlsx.inspect.ndjson'), { force: true });
const previewDirectory = process.env.APL_WORKBOOK_PREVIEW;
if (previewDirectory) {
  await fs.mkdir(previewDirectory, { recursive: true });
  for (const sheetName of ['参数配置', '产品信息', '选项说明', '按键映射']) {
    const preview = await workbook.render({ sheetName, autoCrop: 'all', scale: 1, format: 'png' });
    await fs.writeFile(path.join(previewDirectory, `${sheetName}.png`), new Uint8Array(await preview.arrayBuffer()));
  }
}
console.log(`Exported ${schema.parameters.length} parameters to apl_lib/parameters/APL参数表.xlsx`);
