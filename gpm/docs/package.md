# .gpm 包格式

英文版: [package.en.md](package.en.md)

本文档定义公开 `.gpm` 包布局。

## 容器

`.gpm` 文件本质上是一个 zip 压缩包。
它被当作可安装的包制品，而不是通用应用容器。

## 必要布局

压缩包里应包含这些顶层路径：

- `core/` - 需要安装的载荷文件。
- `Scripts/` - GS 安装和卸载脚本。

可以增加额外路径，但包逻辑不应依赖未公开约定的目录。

## 元数据

包元数据以 JSON 形式存放在 zip comment 中。
公开元数据至少应包含：

- `name`
- `version`
- `author`

推荐字段：

- `category`
- `description`
- `size`
- `sha256`
- `filename`
- `key_id`
- `public_key_url`
- `public_key_signature_url`

示例：

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
  "key_id": "ArthurX/7-Zip",
  "public_key_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.sig"
}
```

## 文件名约定

当前公开构建器使用方括号文件名风格：

```text
[Name,Version,Author,Size].gpm
```

其中 `Size` 用人类可读格式表示，例如 `1.55MB`。

## 构建流程

根目录 `../../build.py` 是公开 `.gpm` 包构建器。
它会把载荷复制到 `core/`，把 `Scripts/install.gs` 复制或生成出来，
然后打包压缩包、把元数据写进 zip comment，并在默认配置下更新
`../../server/packages.json`。

## 校验预期

包宿主只应校验公开且必要的内容：

- 压缩包结构。
- 元数据是否存在。
- 当索引或本地记录提供 `sha256` 时进行校验。
- 当包带有 `GPM2SIG0` 签名尾部，或发布策略要求签名时，按 [信任链规范](trust-chain.md) 验证。
- 解压时的路径安全。

服务端不要替代客户端做最终包验签；下载后的 `.gpm` 应在本地完成解析和验证。
