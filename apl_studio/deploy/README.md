# 公网部署

APL Studio可部署到服务器，通过独立域名的根路径访问，例如 `https://apl.example.com/`。API、Excel/源码下载和WebSocket使用同一域名，仿真C程序在服务器运行，访问者无需安装编译器。

当前提供一个共享工作区和一个部署级访问账号。账号通过HTTP Basic Auth保护网页、API、下载和WebSocket；所有获授权访问者看到同一份产品及修订，不是独立用户或多租户空间。公网使用HTTPS保护访问凭据；无需开放Vite开发端口5174。程序已兼容HTTP下缺少`crypto.randomUUID()`的浏览器，改用安全随机数生成标识；HTTP适合受控测试，不替代公网HTTPS。

## 环境变量

| 变量 | 缺省值 | 用途 |
|---|---|---|
| `APL_STUDIO_HOST` | `127.0.0.1` | Node监听地址；容器中设为`0.0.0.0` |
| `APL_STUDIO_PORT` | `9190` | Node监听端口，1～65535；不自动换端口 |
| `APL_STUDIO_PUBLIC_URL` | 未设置 | 浏览器实际访问的完整origin，如`https://apl.example.com`；非默认端口需写入，不含`/apl`等前缀、查询或片段 |
| `APL_STUDIO_USERNAME` | 未设置 | 部署级访问用户名 |
| `APL_STUDIO_PASSWORD` | 未设置 | 此部署独有的密码，与用户名成对设置 |
| `APL_STUDIO_DATA` | `apl_studio/.data` | 数据库、源码产物及验收资料目录；容器使用`/var/lib/apl-studio` |
| `APL_STUDIO_CC` | 检测本机GCC | 服务端C11编译器；容器为`/usr/bin/gcc` |
| `APL_STUDIO_TEMPLATES` | 相邻`apl_sdk_template` | 受信任的模板目录，更新后重启服务 |

非本机监听或设置公网URL时，需要同时设置公网URL、用户名和密码；无效配置在打开数据库前拒绝启动。本地默认启动仍为 `127.0.0.1:9190`，无需账号。`/health`不要求账号，用于本机就绪检查；其他路径保持认证。

服务按配置的公网域名及origin精确校验请求，不从任意 `X-Forwarded-Host` 或 `X-Forwarded-Proto` 推导可信来源。代理必须保留浏览器的Host、Origin和Authorization；不要把Host统一改成127.0.0.1，也不要删除Origin来绕过校验。

## Docker Compose部署

在装有Docker Engine及Compose插件的Linux服务器上，将**整个APL仓库**放到同一目录。构建需要 `apl_studio`、`apl_lib` 和 `apl_sdk_template`，仅上传前端dist不能运行仿真或生成配置包。镜像使用官方 `node:22-bookworm-slim`，安装GCC和C运行库，以非root用户启动；依赖由仓库lock文件安装。[Node官方镜像](https://hub.docker.com/_/node)列出了此Debian变体。

以下命令均从APL仓库根目录执行：

```sh
cp apl_studio/.env.example apl_studio/.env
```

编辑 `.env`，填写自己的公网域名、用户名和独有密码。密码可用 `node -e "console.log(require('node:crypto').randomBytes(24).toString('base64url'))"` 生成；不要沿用其他服务的密码。环境文件若包含`$`，用单引号包住整个值。不要提交实际 `.env`，构建上下文也会排除它。

```sh
docker compose --env-file apl_studio/.env -f apl_studio/deploy/compose.yaml config --quiet
docker compose --env-file apl_studio/.env -f apl_studio/deploy/compose.yaml up -d --build
docker compose --env-file apl_studio/.env -f apl_studio/deploy/compose.yaml ps
curl --fail http://127.0.0.1:9190/health
```

Compose文件对公网URL和账号使用必填变量，缺少值即报错；`--env-file`显式指定使用的文件，`config --quiet`检查配置而不打印凭据。[Docker变量插值说明](https://docs.docker.com/compose/how-tos/environment-variables/variable-interpolation/)介绍了对应语法及覆盖顺序。若同名变量已在终端环境中设置，以终端值优先。

容器内监听0.0.0.0，但宿主只映射 `127.0.0.1:9190`。外部流量交给下面的HTTPS代理；Compose没有直接把Node端口开放到公网。`studio-data`命名卷保存产品库和构建资料，更新镜像或重建容器不会主动删除它；卷的生命周期见[Docker持久卷说明](https://docs.docker.com/engine/storage/volumes/)。不要为这份共享SQLite数据启动多个服务副本。

这份Compose固定使用容器与宿主9190端口，`.env`中的HOST/PORT只供裸Node启动使用。修改Docker端口时，要同时修改Compose的`environment`、`ports`和Nginx的上游端口；公开网址的非默认端口也须写入`APL_STUDIO_PUBLIC_URL`。

## HTTPS反向代理

先为自己的域名配置DNS和有效TLS证书，再将 [nginx.conf.example](nginx.conf.example) 的域名、证书路径改成实际值。示例文件应放在Nginx的`http {}`上下文中，例如由 `conf.d/*.conf` 引入；不要再套一层`http {}`。

```sh
sudo nginx -t
sudo systemctl reload nginx
```

示例将网页、API和`/ws`一起代理到 `127.0.0.1:9190`，保留Host和Authorization，并显式转发Upgrade/Connection。WebSocket的升级头需要代理显式传递，参见[Nginx官方WebSocket说明](https://nginx.org/en/docs/http/websocket.html)。服务已经发送心跳，示例同时将代理读取超时设为300秒。

浏览器打开配置的HTTPS网址，输入部署账号。API和下载复用浏览器认证，旧版工程界面的WebSocket使用`wss`。若修改公开域名或端口，同时更新`APL_STUDIO_PUBLIC_URL`与代理配置并重建容器。当前不支持 `https://example.com/apl/` 子目录部署；请使用独立子域名或该域名根路径。

## 不使用Docker

安装Node.js ≥22.19.0、npm和能生成服务器本机可执行文件的GCC，保留上述三个源码目录。以下从 `apl_studio` 执行：

```sh
npm ci
npm run build:web
cp .env.example .env
# 编辑 .env：填入公网URL和账号；反向代理同机时保留 HOST=127.0.0.1
node --env-file=.env --import tsx apps/server/src/main.ts
```

Windows PowerShell也可使用同一Node命令：

```powershell
Set-Location apl_studio
npm ci
npm run build:web
Copy-Item .env.example .env
# 编辑 .env 后启动
node --env-file=.env --import tsx apps/server/src/main.ts
```

`npm start`和`start.cmd`读取已有进程环境变量，不自动加载`.env`；上面的Node命令显式加载文件。[Node命令行说明](https://nodejs.org/download/release/v22.21.0/docs/api/cli.html#--env-filefile)定义了`--env-file`和环境变量优先级。`tsx`是当前服务的运行时加载器，不能通过`npm ci --omit=dev`将它移除。把相同命令交给服务器的进程管理器时，保持工作目录和数据目录一致，只启动一个服务实例。

Node自身提供HTTP监听，HTTPS仍由同机反向代理处理。对外域名、访问账号和认证行为与Docker方式一致。

## 更新、备份和检查

拉取或上传新源码后重新执行Compose的`up -d --build`。更新不会改写产品记录的APL来源绑定；如源码或参数定义变化，界面仍需显式确认绑定新版本。镜像不携带宿主的`.git`元数据，源码仍通过内容指纹标识，不能把容器中的开发快照当成已验证的Git发布包。

备份前停止服务，完整备份 `APL_STUDIO_DATA` 或Compose数据卷，包含SQLite及相关文件；恢复后使用同一APL源码版本启动。不要只复制正在写入的`studio.db`，也不要使用`docker compose down -v`作为普通更新命令。

部署后检查：

1. 本机 `/health` 返回正常；未认证访问网页/API应出现401认证要求。
2. 通过公网HTTPS登录，新建产品、保存配置，再进入仿真台，确认服务器C编译和实际负载状态正常。
3. 导出Excel及C源码ZIP；重启容器后确认产品和修订仍保留。
4. 若使用旧版工程入口，确认浏览器WebSocket收到连接消息，使用HTTPS时协议为`wss`。

这些文件提供部署入口和配置示例；仍需在目标服务器执行镜像构建、Nginx配置验证、DNS/TLS及外网访问检查。本机没有Docker/Nginx时，不能将应用层HTTP测试当作已完成公网部署。
