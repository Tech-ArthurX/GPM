<h1 align="center"><img src="gui/assets/glim.ico" alt="GPM" width="96" height="96"/></h1>

<p align="center">
  <a href="README.md">中文</a>&nbsp;&nbsp;&nbsp;|&nbsp;&nbsp;&nbsp;<a href="README.en.md">English</a>
</p>

GPM 是面向 Windows PE 和便携式 Windows 工具的包管理项目。
这个仓库保留发布所需的核心内容：包分发、包元数据、脚本执行，以及语言和规范文档。

## 工作方式

GPM 的服务端负责维护一个简单的 JSON 包索引，里面记录包名、版本、大小、
校验值和下载地址。客户端更新索引后，从索引里的链接下载 `.gpm` 包，
在本地完成完整性校验、签名验证、解压和安装。

这种设计让服务端保持轻量，也让包安装过程可以在 Windows PE 等环境里稳定运行。
包格式和签名链的细节见 [`.gpm` 包格式](gpm/docs/package.md) 和
[信任链规范](gpm/docs/trust-chain.md)。

## 包索引

`packages.json` 是一个数组，每一项代表一个可安装包：

```json
{
  "name": "7-Zip",
  "version": "24.09",
  "author": "ArthurX",
  "category": "Archive",
  "description": "File archiver",
  "size": 1623934,
  "sha256": "hex-encoded-sha256",
  "filename": "[7-Zip,24.09,ArthurX,1.55MB].gpm",
  "url": "https://example.test/packages/7zip.gpm",
  "key_id": "ArthurX/7-Zip",
  "public_key_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.sig"
}
```

最小可用字段是 `name`、`version`、`author` 和 `url`。
建议同时提供 `size` 和 `sha256`，方便客户端显示大小并校验下载结果。
如果启用包签名，可以再提供 `key_id`、`public_key_url` 和
`public_key_signature_url`。

## 目录

- `gpm/` - Go CLI、包安装运行时、下载器、GUI WebSocket 后端和 GS 集成。
- `gs/` - GS 解释器/编译器模块和语言规范。
- `gui/` - 原生 Direct2D 前端。
- `server/` - 包索引服务和扫描器。
- `tools/` - 给包脚本和运行时集成使用的可选 PE 工具。
- `build.py` - 根目录 `.gpm` 包构建器，默认更新 `server/packages.json`。
- `gpm/docs/package.md` / `gpm/docs/package.en.md` - `.gpm` 包格式规范。
- `gpm/docs/trust-chain.md` / `gpm/docs/trust-chain.en.md` - 密钥信任链与验证流程。
- `gs/docs/style.md` / `gs/docs/style.en.md` - GS 写法与风格说明。
- `gs/docs/reference.md` / `gs/docs/reference.en.md` - GS 语言参考。
- `gs/docs/tutorial.md` / `gs/docs/tutorial.en.md` - GS 教程。
- `gs/SPEC.md` / `gs/SPEC.en.md` - GS 规范性契约。
- `gpm/README.md` / `gpm/README.en.md` - GPM CLI 总览。
- `server/README.md` / `server/README.en.md` - 包索引服务总览。
- `gui/README.md` / `gui/README.en.md` - 原生前端总览。
